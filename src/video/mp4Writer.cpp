// ============================================================================
// mp4Writer.cpp - 轻量 MP4 容器封装器实现
// ============================================================================

#include "mp4Writer.h"
#include <fstream>
#include <cstring>
#include <logger.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mp4 {

// ====================================================================
// 平台工具
// ====================================================================

#ifdef _WIN32
static std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], len);
    return w;
}
#endif

// ====================================================================
// 大端序写入辅助
// ====================================================================

static void w16be(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void w32be(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void w32beAt(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    buf[off]     = static_cast<uint8_t>((v >> 24) & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[off + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[off + 3] = static_cast<uint8_t>(v & 0xFF);
}

static void w4cc(std::vector<uint8_t>& buf, const char* cc) {
    buf.insert(buf.end(), cc, cc + 4);
}

// 开始一个 box：写入 size 占位 + type，返回 size 偏移（用于 boxEnd 回填）
static size_t boxBegin(std::vector<uint8_t>& buf, const char* type) {
    size_t off = buf.size();
    w32be(buf, 0);   // size 占位
    w4cc(buf, type);
    return off;
}

// 结束一个 box：回填实际 size
static void boxEnd(std::vector<uint8_t>& buf, size_t off) {
    uint32_t sz = static_cast<uint32_t>(buf.size() - off);
    w32beAt(buf, off, sz);
}

// ====================================================================
// Exp-Golomb 解码器（用于解析 SPS）
// ====================================================================

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : d_(data), sz_(size), bp_(0), bits_(7) {}

    uint32_t readBits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) {
            v = (v << 1) | getBit();
        }
        return v;
    }

    uint32_t readUE() {
        int zeros = 0;
        while (getBit() == 0 && bp_ < sz_) zeros++;
        if (bp_ >= sz_) return 0;
        return (1u << zeros) - 1 + readBits(zeros);
    }

    int32_t readSE() {
        uint32_t ue = readUE();
        return (ue & 1) ? static_cast<int32_t>((ue >> 1) + 1)
                        : -static_cast<int32_t>(ue >> 1);
    }

private:
    uint8_t getBit() {
        if (bp_ >= sz_) return 0;
        uint8_t b = (d_[bp_] >> bits_) & 1;
        if (bits_ == 0) { bits_ = 7; bp_++; }
        else bits_--;
        return b;
    }

    const uint8_t* d_;
    size_t  sz_;
    size_t  bp_;
    int     bits_;
};

// ====================================================================
// SPS 解析：提取 width, height, profile, level
// ====================================================================

static bool parseSPS(const uint8_t* data, size_t size, SpsInfo& info) {
    if (size < 4) return false;

    // 去除 emulation prevention bytes（0x00 0x00 0x03 中的 0x03 是编码时插入的）。
    // 时序字段（num_units_in_tick/time_scale）含连续 0x00 时必然触发插入，
    // 不去除会导致位流错位、帧率解析错误（如 20fps 被解析成 0）
    std::vector<uint8_t> rbsp;
    rbsp.reserve(size);
    int zeroCount = 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = data[i];
        if (zeroCount >= 2 && byte == 0x03) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        if (byte == 0x00) ++zeroCount;
        else zeroCount = 0;
    }

    // 跳过 NAL header (1 byte)
    BitReader br(rbsp.data() + 1, rbsp.size() - 1);

    info.profile_idc = br.readBits(8);
    br.readBits(8);          // constraint flags + reserved
    info.level_idc   = br.readBits(8);
    br.readUE();             // seq_parameter_set_id

    uint32_t chroma_fmt = 1;
    // 高 profile 有额外字段
    if (info.profile_idc >= 100) {
        chroma_fmt = br.readUE();
        if (chroma_fmt == 3) br.readBits(1); // separate_colour_plane_flag
        br.readUE();  // bit_depth_luma_minus8
        br.readUE();  // bit_depth_chroma_minus8
        br.readBits(1); // qpprime_y_zero_transform_bypass_flag
        int scaling = br.readBits(1); // seq_scaling_matrix_present_flag
        if (scaling) {
            int n = (chroma_fmt != 3) ? 8 : 12;
            for (int i = 0; i < n; i++) {
                if (br.readBits(1)) { // seq_scaling_list_present_flag[i]
                    int last = 8, next = 8;
                    int sz = (i < 6) ? 16 : 64;
                    for (int j = 0; j < sz; j++) {
                        if (next != 0) {
                            int32_t d = br.readSE();
                            next = (last + d + 256) % 256;
                        }
                        last = (next == 0) ? last : next;
                    }
                }
            }
        }
    }

    br.readUE();  // log2_max_frame_num_minus4
    uint32_t poc_type = br.readUE();
    if (poc_type == 0) {
        br.readUE();  // log2_max_pic_order_cnt_lsb_minus4
    } else if (poc_type == 1) {
        br.readBits(1);  // delta_pic_order_always_zero_flag
        br.readSE();     // offset_for_non_ref_pic
        br.readSE();     // offset_for_top_to_bottom_field
        uint32_t n = br.readUE();
        for (uint32_t i = 0; i < n; i++) br.readSE();
    }

    br.readUE();  // num_ref_frames
    br.readBits(1); // gaps_in_frame_num_value_allowed_flag
    uint32_t pic_w_mbs   = br.readUE() + 1;
    uint32_t pic_h_units = br.readUE() + 1;
    uint32_t frame_mbs_only = br.readBits(1);
    if (!frame_mbs_only)
        br.readBits(1);  // mb_adaptive_frame_field_flag

    info.width  = pic_w_mbs * 16;
    info.height = pic_h_units * 16 * (2 - frame_mbs_only);

    // ---- VUI timing info 解析 ----
    br.readBits(1);  // direct_8x8_inference_flag
    if (br.readBits(1)) {  // frame_cropping_flag
        br.readUE();  // frame_crop_left_offset
        br.readUE();  // frame_crop_right_offset
        br.readUE();  // frame_crop_top_offset
        br.readUE();  // frame_crop_bottom_offset
    }
    if (!br.readBits(1)) { info.valid = true; return true; }  // vui_parameters_present_flag=0
    // aspect_ratio_info_present_flag
    if (br.readBits(1)) {
        uint32_t aspect_ratio_idc = br.readBits(8);
        if (aspect_ratio_idc == 255) { br.readBits(16); br.readBits(16); }
    }
    if (br.readBits(1)) br.readBits(1);  // overscan_info
    if (br.readBits(1)) {                // video_signal_type_present_flag
        br.readBits(3); br.readBits(1);  // video_format + video_full_range_flag
        if (br.readBits(1)) { br.readBits(8); br.readBits(8); br.readBits(8); }
    }
    if (br.readBits(1)) { br.readUE(); br.readUE(); }  // chroma_loc_info
    // timing_info
    if (!br.readBits(1)) { info.valid = true; return true; }  // timing_info_present_flag=0
    uint32_t num_units_in_tick = br.readBits(32);
    uint32_t time_scale        = br.readBits(32);
    if (num_units_in_tick > 0 && time_scale > 0) {
        info.fps = static_cast<double>(time_scale) / (2.0 * static_cast<double>(num_units_in_tick));
        if (info.fps < 1.0 || info.fps > 120.0) info.fps = 0.0;
    }

    info.valid  = true;
    return true;
}

double parseSpsFps(const uint8_t* spsNal, size_t size) {
    SpsInfo info;
    if (!parseSPS(spsNal, size, info)) return 0.0;
    return info.fps;
}

// ====================================================================
// H.265 SPS 解析：提取 width, height, VUI 帧率与 hvcC 所需字段
// 参考 ISO/IEC 23008-2 (Rec. ITU-T H.265) SPS 语法
// ====================================================================

static bool parseHevcSpsImpl(const uint8_t* data, size_t size, HevcSpsInfo& info) {
    if (size < 6) return false;

    // 去除 emulation prevention bytes
    std::vector<uint8_t> rbsp;
    rbsp.reserve(size);
    int zeroCount = 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t byte = data[i];
        if (zeroCount >= 2 && byte == 0x03) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        if (byte == 0x00) ++zeroCount;
        else zeroCount = 0;
    }
    if (rbsp.size() < 5) return false;

    // 跳过 2 字节 NAL header（forbidden_bit + nal_unit_type + nuh_layer_id + nuh_temporal_id_plus1）
    BitReader br(rbsp.data() + 2, rbsp.size() - 2);

    br.readUE();                    // sps_video_parameter_set_id
    uint32_t max_sub_layers_minus1 = br.readBits(3);
    uint32_t temporal_id_nesting   = br.readBits(1);
    info.max_sub_layers_minus1    = static_cast<uint8_t>(max_sub_layers_minus1);
    info.temporal_id_nesting_flag = static_cast<uint8_t>(temporal_id_nesting);

    // profile_tier_level(): general 部分 96 bits（8 profile/tier + 32 compat +
    // 4 constraint + 44 reserved + 8 level），正是 hvcC 需要的 general_profile_tier_level
    for (int i = 0; i < 12; i++) {
        info.general_profile_tier_level[i] = static_cast<uint8_t>(br.readBits(8));
    }

    // sub_layer_profile/level_present_flag（多子层时）
    if (max_sub_layers_minus1 > 0) {
        std::vector<uint32_t> sub_profile(max_sub_layers_minus1), sub_level(max_sub_layers_minus1);
        for (uint32_t i = 0; i < max_sub_layers_minus1; i++) sub_profile[i] = br.readBits(1);
        for (uint32_t i = 0; i < max_sub_layers_minus1; i++) sub_level[i] = br.readBits(1);
        br.readBits(2);  // reserved_zero_2bits
        for (uint32_t i = 0; i < max_sub_layers_minus1; i++) {
            if (sub_profile[i]) {
                // sub_layer profile_tier_level() 不含 level_idc：8+32+4+44 = 88 bits
                for (int j = 0; j < 11; j++) br.readBits(8);
            }
            if (sub_level[i]) br.readBits(8);
        }
    }

    br.readUE();  // sps_seq_parameter_set_id
    uint32_t chroma_format_idc = br.readUE();
    info.chroma_format_idc    = static_cast<uint8_t>(chroma_format_idc);
    if (chroma_format_idc == 3) br.readBits(1);  // separate_colour_plane_flag

    uint32_t width  = br.readUE();   // pic_width_in_luma_samples
    uint32_t height = br.readUE();   // pic_height_in_luma_samples
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return false;
    info.width  = width;
    info.height = height;

    if (br.readBits(1)) {  // conformance_window_flag
        br.readUE(); br.readUE(); br.readUE(); br.readUE();  // conf_win_left/right/top/bottom
    }
    info.bit_depth_luma_minus8   = static_cast<uint8_t>(br.readUE());
    info.bit_depth_chroma_minus8 = static_cast<uint8_t>(br.readUE());
    uint32_t log2_max_poc_lsb_minus4 = br.readUE();

    uint32_t sub_layer_ordering = br.readBits(1);  // sps_sub_layer_ordering_info_present_flag
    uint32_t start_layer = sub_layer_ordering ? 0 : max_sub_layers_minus1;
    for (uint32_t i = start_layer; i <= max_sub_layers_minus1; i++) {
        br.readUE();  // sps_max_dec_pic_buffering_minus1
        br.readUE();  // sps_max_num_reorder_pics
        br.readUE();  // sps_max_latency_increase_plus1
    }
    br.readUE();  // log2_min_luma_coding_block_size_minus3
    br.readUE();  // log2_diff_max_min_luma_coding_block_size
    br.readUE();  // log2_min_luma_transform_block_size_minus2
    br.readUE();  // log2_diff_max_min_luma_transform_block_size
    br.readUE();  // max_transform_hierarchy_depth_inter
    br.readUE();  // max_transform_hierarchy_depth_intra

    // scaling_list_data()
    if (br.readBits(1)) {  // scaling_list_enabled_flag
        if (br.readBits(1)) {  // sps_scaling_list_data_present_flag
            for (uint32_t size_id = 0; size_id < 4; size_id++) {
                for (uint32_t matrix_id = 0; matrix_id < 6; matrix_id += (size_id == 3) ? 3 : 1) {
                    uint32_t pred_mode = br.readBits(1);  // scaling_list_pred_mode_flag
                    if (pred_mode) {
                        br.readUE();  // scaling_list_pred_matrix_id_delta
                    } else {
                        uint32_t coef_num = (size_id == 0) ? 16 : 64;
                        if (size_id > 1) br.readSE();  // scaling_list_dc_coef_minus8
                        for (uint32_t j = 0; j < coef_num; j++) br.readSE();
                    }
                }
            }
        }
    }

    br.readBits(1);  // amp_enabled_flag
    br.readBits(1);  // sample_adaptive_offset_enabled_flag
    if (br.readBits(1)) {  // pcm_enabled_flag
        br.readBits(4);  // pcm_sample_bit_depth_luma_minus1
        br.readBits(4);  // pcm_sample_bit_depth_chroma_minus1
        br.readUE();     // log2_min_pcm_luma_coding_block_size_minus3
        br.readUE();     // log2_diff_max_min_pcm_luma_coding_block_size
        br.readBits(1);  // pcm_loop_filter_disabled_flag
    }

    // short_term_ref_pic_set()（需要维护 NumDeltaPocs 以跳过 inter 预测分支）
    std::vector<uint32_t> num_delta_pocs;
    uint32_t num_st_rps = br.readUE();
    for (uint32_t i = 0; i < num_st_rps; i++) {
        uint32_t inter_pred = br.readBits(1);  // inter_ref_pic_set_prediction_flag
        if (inter_pred) {
            if (i == 0) return false;  // 首项不允许 inter 预测
            uint32_t delta_idx_minus1 = br.readUE();
            uint32_t ref_idx = (delta_idx_minus1 + 1 <= i) ? i - (delta_idx_minus1 + 1) : 0;
            br.readBits(1);  // delta_rps_sign
            br.readUE();     // abs_delta_rps_minus1
            uint32_t ref_delta_pocs = (ref_idx < num_delta_pocs.size()) ? num_delta_pocs[ref_idx] : 0;
            for (uint32_t j = 0; j <= ref_delta_pocs; j++) {
                if (!br.readBits(1)) br.readBits(1);  // used_by_curr_pic_flag + use_delta_flag
            }
            num_delta_pocs.push_back(ref_delta_pocs);
        } else {
            uint32_t num_neg = br.readUE();
            uint32_t num_pos = br.readUE();
            for (uint32_t j = 0; j < num_neg; j++) {
                br.readUE();        // delta_poc_s0_minus1
                br.readBits(1);     // used_by_curr_pic_s0_flag
            }
            for (uint32_t j = 0; j < num_pos; j++) {
                br.readUE();        // delta_poc_s1_minus1
                br.readBits(1);     // used_by_curr_pic_s1_flag
            }
            num_delta_pocs.push_back(num_neg + num_pos);
        }
    }

    if (br.readBits(1)) {  // long_term_ref_pics_present_flag
        uint32_t num_lt = br.readUE();
        uint32_t lsb_bits = log2_max_poc_lsb_minus4 + 4;
        for (uint32_t i = 0; i < num_lt; i++) {
            br.readBits(lsb_bits);  // lt_ref_pic_poc_lsb_sps
            br.readBits(1);         // used_by_curr_pic_lt_sps_flag
        }
    }
    br.readBits(1);  // sps_temporal_mvp_enabled_flag
    br.readBits(1);  // strong_intra_smoothing_enabled_flag

    // vui_parameters()
    if (br.readBits(1)) {  // vui_parameters_present_flag
        if (br.readBits(1)) {  // aspect_ratio_info_present_flag
            uint32_t idc = br.readBits(8);
            if (idc == 255) { br.readBits(16); br.readBits(16); }  // sar_width/height
        }
        if (br.readBits(1)) br.readBits(1);  // overscan_info_present_flag + overscan_appropriate_flag
        if (br.readBits(1)) {                // video_signal_type_present_flag
            br.readBits(3); br.readBits(1);  // video_format + video_full_range_flag
            if (br.readBits(1)) { br.readBits(8); br.readBits(8); br.readBits(8); }  // colour_description
        }
        if (br.readBits(1)) { br.readUE(); br.readUE(); }  // chroma_loc_info
        br.readBits(1);  // neutral_chroma_indication_flag
        br.readBits(1);  // field_seq_flag
        br.readBits(1);  // frame_field_info_present_flag
        if (br.readBits(1)) { br.readUE(); br.readUE(); br.readUE(); br.readUE(); }  // default_display_window
        // vui_timing_info_present_flag
        if (br.readBits(1)) {
            uint32_t num_units_in_tick = br.readBits(32);
            uint32_t time_scale        = br.readBits(32);
            if (num_units_in_tick > 0 && time_scale > 0) {
                double fps = static_cast<double>(time_scale) / static_cast<double>(num_units_in_tick);
                if (fps >= 1.0 && fps <= 120.0) info.fps = fps;
            }
        }
    }

    info.valid = true;
    return true;
}

bool parseHevcSps(const uint8_t* spsNal, size_t size, HevcSpsInfo& info) {
    return parseHevcSpsImpl(spsNal, size, info);
}

double parseHevcSpsFps(const uint8_t* spsNal, size_t size) {
    HevcSpsInfo info;
    if (!parseHevcSpsImpl(spsNal, size, info)) return 0.0;
    return info.fps;
}

// slice_header 的第一个字段 first_mb_in_slice 用于判断一个 VCL NAL
// 是否是一帧（access unit）的第一个 slice。一个视频帧可能由多个 slice NAL 组成，
// 不能简单地把每个 type 1/5 NAL 都当成独立帧。
static bool isFirstSliceOfPicture(const NalUnit& nal) {
    if (nal.size <= 1) return true;

    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal.size - 1);
    int zeroCount = 0;
    for (size_t i = 1; i < nal.size; ++i) {
        uint8_t byte = nal.data[i];
        if (zeroCount >= 2 && byte == 0x03) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        if (byte == 0x00) ++zeroCount;
        else zeroCount = 0;
    }
    if (rbsp.empty()) return true;
    BitReader br(rbsp.data(), rbsp.size());
    return br.readUE() == 0;
}

// H.265 slice header 第一个字段 first_slice_segment_in_pic_flag（1 bit），
// ==1 表示该 slice 是一帧的第一个 slice
static bool isFirstSliceOfPictureHevc(const NalUnit& nal) {
    if (nal.size <= 2) return true;

    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal.size - 2);
    int zeroCount = 0;
    for (size_t i = 2; i < nal.size; ++i) {
        uint8_t byte = nal.data[i];
        if (zeroCount >= 2 && byte == 0x03) {
            zeroCount = 0;
            continue;
        }
        rbsp.push_back(byte);
        if (byte == 0x00) ++zeroCount;
        else zeroCount = 0;
    }
    if (rbsp.empty()) return true;
    BitReader br(rbsp.data(), rbsp.size());
    return br.readBits(1) == 1;
}

// H.265 NAL unit type 的 VCL 判断：0-31 均为 VCL（10-15 为保留）
static bool isHevcVcl(uint8_t t) {
    return t <= 31;
}

// H.265 IRAP（可随机访问）NAL 类型：16-23（BLA/IDR/CRA/RSV_IRAP）
static bool isHevcKeyframe(uint8_t t) {
    return t >= 16 && t <= 23;
}

// ====================================================================
// Annex B 解析：从 .h264 文件提取 NAL 单元并分组为帧
// ====================================================================

static bool parseAnnexB(const std::string& path,
                        std::vector<uint8_t>& data,
                        std::vector<NalUnit>& all_nals,
                        std::vector<VideoFrame>& frames,
                        SpsInfo& spsInfo,
                        HevcSpsInfo& hevcSpsInfo,
                        const std::string& codec)
{
    const bool isHevc = (codec == "H265" || codec == "HEVC");
#ifdef _WIN32
    std::ifstream ifs(utf8ToWide(path), std::ios::binary);
#else
    std::ifstream ifs(path, std::ios::binary);
#endif
    if (!ifs) {
        LOG("[MP4] Cannot open .h265/.h264 file: %s", path.c_str());
        return false;
    }

    ifs.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    if (fileSize < 4) {
        LOG("[MP4] .h265/.h264 file too small: %zu bytes", fileSize);
        return false;
    }

    data.resize(fileSize);
    ifs.read(reinterpret_cast<char*>(data.data()), fileSize);
    ifs.close();

    // 按 start code (00 00 00 01 或 00 00 01) 分割 NAL。
    // 注意：不能先跳过 0x00 再判断起始码，否则会把起始码本身跳掉。
    std::vector<uint8_t> sps_raw, pps_raw, vps_raw;
    auto findStartCode = [&](size_t from, size_t& scLen) -> size_t {
        for (size_t i = from; i + 2 < fileSize; ++i) {
            if (i + 3 < fileSize && data[i] == 0x00 && data[i + 1] == 0x00 &&
                data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                scLen = 4;
                return i;
            }
            if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
                scLen = 3;
                return i;
            }
        }
        scLen = 0;
        return fileSize;
    };

    size_t scLen = 0;
    size_t pos = findStartCode(0, scLen);
    while (pos < fileSize) {
        size_t nalStart = pos + scLen;
        size_t nextScLen = 0;
        size_t next = findStartCode(nalStart, nextScLen);
        size_t nalEnd = next;
        // 去掉尾部零
        while (nalEnd > nalStart && data[nalEnd - 1] == 0) nalEnd--;

        if (nalEnd <= nalStart) {
            pos = next;
            scLen = nextScLen;
            continue;
        }

        // H.264 NAL type 在首字节低 5 位；H.265 在 2 字节 NAL 头的 6 bit 位域
        uint8_t nalType = isHevc ? ((data[nalStart] >> 1) & 0x3F)
                                 : (data[nalStart] & 0x1F);
        NalUnit nu;
        nu.data = data.data() + nalStart;
        nu.size = nalEnd - nalStart;
        nu.type = nalType;
        all_nals.push_back(nu);

        if (isHevc) {
            if (nalType == MP4_NAL_TYPE_H265_VPS) {
                vps_raw.assign(nu.data, nu.data + nu.size);
            } else if (nalType == MP4_NAL_TYPE_H265_SPS) {
                sps_raw.assign(nu.data, nu.data + nu.size);
            } else if (nalType == MP4_NAL_TYPE_H265_PPS) {
                pps_raw.assign(nu.data, nu.data + nu.size);
            }
        } else {
            if (nalType == MP4_NAL_TYPE_SPS) {
                sps_raw.assign(nu.data, nu.data + nu.size);
            } else if (nalType == MP4_NAL_TYPE_PPS) {
                pps_raw.assign(nu.data, nu.data + nu.size);
            }
        }

        pos = next;
        scLen = nextScLen;
    }

    if (isHevc) {
        if (!sps_raw.empty()) parseHevcSpsImpl(sps_raw.data(), sps_raw.size(), hevcSpsInfo);
    } else {
        if (!sps_raw.empty()) parseSPS(sps_raw.data(), sps_raw.size(), spsInfo);
    }

    // 将 NAL 分组为 access unit。SPS/PPS/SEI/AUD 等前缀 NAL 归入下一帧；
    // VCL NAL 只有在表示新画面首个 slice 时才开启新帧。
    VideoFrame curFrame;
    std::vector<NalUnit> pendingPrefix;
    bool hasVcl = false;
    const uint8_t audType = isHevc ? MP4_NAL_TYPE_H265_AUD : MP4_NAL_TYPE_AUD;

    for (const auto& nal : all_nals) {
        const uint8_t t = nal.type;
        const bool isVcl = isHevc ? isHevcVcl(t)
                                  : (t == MP4_NAL_TYPE_IDR || t == MP4_NAL_TYPE_NON_IDR);

        if (isVcl) {
            const bool startsNewPicture = isHevc ? isFirstSliceOfPictureHevc(nal)
                                                 : isFirstSliceOfPicture(nal);
            if (hasVcl && startsNewPicture) {
                frames.push_back(curFrame);
                curFrame = VideoFrame();
                hasVcl = false;
            }
            if (!hasVcl && !pendingPrefix.empty()) {
                curFrame.nals.insert(curFrame.nals.end(), pendingPrefix.begin(), pendingPrefix.end());
                pendingPrefix.clear();
            }
            curFrame.nals.push_back(nal);
            curFrame.is_keyframe = curFrame.is_keyframe ||
                                   (isHevc ? isHevcKeyframe(t) : (t == MP4_NAL_TYPE_IDR));
            hasVcl = true;
        } else {
            // AUD 明确标记下一 access unit 的开始。
            if (t == audType && hasVcl) {
                frames.push_back(curFrame);
                curFrame = VideoFrame();
                hasVcl = false;
            }
            pendingPrefix.push_back(nal);
        }
    }
    if (hasVcl) frames.push_back(curFrame);

    return true;
}

// ====================================================================
// avcC (AVCDecoderConfigurationRecord) 构建
// ====================================================================

static void buildAvcC(const std::vector<NalUnit>& all_nals, std::vector<uint8_t>& out) {
    const NalUnit* sps = nullptr;
    const NalUnit* pps = nullptr;

    for (auto& n : all_nals) {
        if (!sps && n.type == MP4_NAL_TYPE_SPS) sps = &n;
        if (!pps && n.type == MP4_NAL_TYPE_PPS) pps = &n;
        if (sps && pps) break;
    }

    out.push_back(0x01);  // configurationVersion
    out.push_back(sps ? sps->data[1] : 0x42);  // profile
    out.push_back(sps ? sps->data[2] : 0x00);  // compat
    out.push_back(sps ? sps->data[3] : 0x1E);  // level
    out.push_back(0xFF);  // lengthSizeMinusOne: 4 bytes

    out.push_back(0xE0 | (sps ? 1 : 0));
    if (sps) {
        w16be(out, static_cast<uint16_t>(sps->size));
        out.insert(out.end(), sps->data, sps->data + sps->size);
    }
    out.push_back(pps ? 1 : 0);
    if (pps) {
        w16be(out, static_cast<uint16_t>(pps->size));
        out.insert(out.end(), pps->data, pps->data + pps->size);
    }
}

// ====================================================================
// hvcC (HEVCDecoderConfigurationRecord) 构建
// ====================================================================

static void buildHvcC(const std::vector<NalUnit>& all_nals,
                      const HevcSpsInfo& spsInfo,
                      std::vector<uint8_t>& out) {
    const NalUnit* vps = nullptr;
    const NalUnit* sps = nullptr;
    const NalUnit* pps = nullptr;

    for (auto& n : all_nals) {
        if (!vps && n.type == MP4_NAL_TYPE_H265_VPS) vps = &n;
        if (!sps && n.type == MP4_NAL_TYPE_H265_SPS) sps = &n;
        if (!pps && n.type == MP4_NAL_TYPE_H265_PPS) pps = &n;
        if (vps && sps && pps) break;
    }

    out.push_back(0x01);  // configurationVersion

    // general_profile_tier_level: 来自 SPS 解析（12 字节）
    out.insert(out.end(), spsInfo.general_profile_tier_level,
               spsInfo.general_profile_tier_level + 12);

    // reserved(4) + min_spatial_segmentation_idc(12)
    w16be(out, 0);
    // reserved(6) + parallelismType(2)
    out.push_back(0);
    // reserved(6) + chromaFormat(2)
    out.push_back(static_cast<uint8_t>(spsInfo.chroma_format_idc & 0x03));
    // reserved(5) + bitDepthLumaMinus8(3)
    out.push_back(static_cast<uint8_t>(spsInfo.bit_depth_luma_minus8 & 0x07));
    // reserved(5) + bitDepthChromaMinus8(3)
    out.push_back(static_cast<uint8_t>(spsInfo.bit_depth_chroma_minus8 & 0x07));
    // avgFrameRate(16)
    w16be(out, 0);
    // constantFrameRate(2)=0 + numTemporalLayers(3) + temporalIdNested(1) + lengthSizeMinusOne(2)=3
    // numTemporalLayers = sps_max_sub_layers_minus1 + 1
    out.push_back(static_cast<uint8_t>(
        (((spsInfo.max_sub_layers_minus1 + 1) & 0x07) << 3) |
        ((spsInfo.temporal_id_nesting_flag & 0x01) << 2) |
        0x03));

    // numOfArrays = 3（VPS/SPS/PPS）
    out.push_back(3);

    // array: array_completeness(1) + reserved(1) + NAL_unit_type(6)
    if (vps) {
        out.push_back(static_cast<uint8_t>(0x80 | MP4_NAL_TYPE_H265_VPS));
        w16be(out, 1);  // numNalus
        w16be(out, static_cast<uint16_t>(vps->size));
        out.insert(out.end(), vps->data, vps->data + vps->size);
    }
    if (sps) {
        out.push_back(static_cast<uint8_t>(0x80 | MP4_NAL_TYPE_H265_SPS));
        w16be(out, 1);
        w16be(out, static_cast<uint16_t>(sps->size));
        out.insert(out.end(), sps->data, sps->data + sps->size);
    }
    if (pps) {
        out.push_back(static_cast<uint8_t>(0x80 | MP4_NAL_TYPE_H265_PPS));
        w16be(out, 1);
        w16be(out, static_cast<uint16_t>(pps->size));
        out.insert(out.end(), pps->data, pps->data + pps->size);
    }
}

// ====================================================================
// Box 构建
// ====================================================================

static std::vector<uint8_t> buildFtyp(bool isHevc) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "ftyp");
    w4cc(b, "isom");
    w32be(b, 0x200);
    w4cc(b, "isom");
    w4cc(b, "iso2");
    w4cc(b, isHevc ? "hev1" : "avc1");
    w4cc(b, "mp41");
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildMvhd(uint32_t timescale,
                                      uint32_t duration)
{
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "mvhd");

    w32be(b, 0);                    // version + flags
    w32be(b, 0);                    // creation_time
    w32be(b, 0);                    // modification_time
    w32be(b, timescale);
    w32be(b, duration);

    w32be(b, 0x00010000);           // rate = 1.0
    w16be(b, 0x0100);               // volume = 1.0
    w16be(b, 0);                    // reserved

    // 必须存在的 reserved[2]，缺少这 8 字节
    w32be(b, 0);
    w32be(b, 0);

    // identity matrix
    w32be(b, 0x00010000);
    w32be(b, 0);
    w32be(b, 0);

    w32be(b, 0);
    w32be(b, 0x00010000);
    w32be(b, 0);

    w32be(b, 0);
    w32be(b, 0);
    w32be(b, 0x40000000);

    // pre_defined[6]
    for (int i = 0; i < 6; ++i) {
        w32be(b, 0);
    }

    // track_id=1 已被使用，下一个可用 track id 应该是 2
    w32be(b, 2);

    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildTkhd(uint32_t width,
                                      uint32_t height,
                                      uint32_t duration)
{
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "tkhd");

    w32be(b, 0x00000007);           // version=0, flags=7
    w32be(b, 0);                    // creation_time
    w32be(b, 0);                    // modification_time
    w32be(b, 1);                    // track_id
    w32be(b, 0);                    // reserved
    w32be(b, duration);

    // reserved[2]
    w32be(b, 0);
    w32be(b, 0);

    w16be(b, 0);                    // layer
    w16be(b, 0);                    // alternate_group
    w16be(b, 0);                    // video track volume必须为0
    w16be(b, 0);                    // reserved：注意是16位，不是32位

    // identity matrix
    w32be(b, 0x00010000);
    w32be(b, 0);
    w32be(b, 0);

    w32be(b, 0);
    w32be(b, 0x00010000);
    w32be(b, 0);

    w32be(b, 0);
    w32be(b, 0);
    w32be(b, 0x40000000);

    // 16.16 fixed point
    w32be(b, width << 16);
    w32be(b, height << 16);

    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildMdhd(uint32_t timescale, uint32_t duration) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "mdhd");
    w32be(b, 0);                    // version+flags
    w32be(b, 0); w32be(b, 0);      // ctime+mtime
    w32be(b, timescale);
    w32be(b, duration);
    w16be(b, 0x55C4);               // language: und
    w16be(b, 0);                    // pre_defined
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildHdlr() {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "hdlr");
    w32be(b, 0);
    w4cc(b, "\0\0\0\0");
    w4cc(b, "vide");
    w32be(b, 0); w32be(b, 0); w32be(b, 0); // reserved
    const char* name = "VideoHandler";
    b.insert(b.end(), name, name + std::strlen(name) + 1);
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildVmhd() {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "vmhd");
    w32be(b, 0x00000001);           // version+flags
    w16be(b, 0);                    // graphicsmode
    w16be(b, 0); w16be(b, 0); w16be(b, 0); // opcolor
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildDinf() {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "dinf");
    size_t offDref = boxBegin(b, "dref");
    w32be(b, 0);                    // version+flags
    w32be(b, 1);                    // entry_count
    size_t offUrl = boxBegin(b, "url ");
    w32be(b, 0x00000001);           // flags: self-contained
    boxEnd(b, offUrl);
    boxEnd(b, offDref);
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildStsd(const std::vector<uint8_t>& decCfg,
                                      uint32_t width, uint32_t height, bool isHevc) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stsd");
    w32be(b, 0);                    // version
    w32be(b, 1);                    // entry_count

    size_t offAvc1 = boxBegin(b, isHevc ? "hev1" : "avc1");
    w32be(b, 0); w16be(b, 0);      // reserved
    w16be(b, 1);                    // data_ref_index
    w16be(b, 0); w16be(b, 0);      // pre_defined+reserved
    w32be(b, 0); w32be(b, 0); w32be(b, 0); // pre_defined
    w16be(b, static_cast<uint16_t>(width));
    w16be(b, static_cast<uint16_t>(height));
    w32be(b, 0x00480000);           // horiz resolution 72dpi
    w32be(b, 0x00480000);           // vert resolution
    w32be(b, 0);                    // reserved
    w16be(b, 1);                    // frame_count
    // compressor name (32 bytes)
    char compr[32] = {0};
    b.insert(b.end(), compr, compr + 32);
    w16be(b, 0x0018);               // depth
    w16be(b, 0xFFFF);               // pre_defined

    // avcC / hvcC sub-box
    size_t offAvcC = boxBegin(b, isHevc ? "hvcC" : "avcC");
    b.insert(b.end(), decCfg.begin(), decCfg.end());
    boxEnd(b, offAvcC);

    boxEnd(b, offAvc1);
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildStts(uint32_t frameCount, uint32_t sampleDelta) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stts");
    w32be(b, 0);
    w32be(b, 1);                    // one entry: all same duration
    w32be(b, frameCount);
    w32be(b, sampleDelta);
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildStss(const std::vector<size_t>& syncIdx) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stss");
    w32be(b, 0);
    w32be(b, static_cast<uint32_t>(syncIdx.size()));
    for (auto idx : syncIdx) {
        w32be(b, static_cast<uint32_t>(idx + 1)); // 1-based
    }
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildStsz(const std::vector<uint32_t>& sizes) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stsz");
    w32be(b, 0);
    w32be(b, 0);                    // variable sample size
    w32be(b, static_cast<uint32_t>(sizes.size()));
    for (auto sz : sizes) w32be(b, sz);
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildStsc(uint32_t samplesPerChunk) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stsc");
    w32be(b, 0);
    w32be(b, 1);                    // one entry covers all chunks
    w32be(b, 1);                    // first_chunk
    w32be(b, samplesPerChunk);      // samples_per_chunk
    w32be(b, 1);                    // sample_description_index
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildStco(const std::vector<uint64_t>& offsets) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stco");
    w32be(b, 0);
    w32be(b, static_cast<uint32_t>(offsets.size()));
    for (auto o : offsets) w32be(b, static_cast<uint32_t>(o));
    boxEnd(b, off);
    return b;
}

// ====================================================================
// 主转换函数
// ====================================================================

static bool convertAnnexBtoMP4(const std::string& inPath, const std::string& mp4Path,
                               double recordDurationSec, double spsFps,
                               const std::string& codec) {
    const bool isHevc = (codec == "H265" || codec == "HEVC");
    // ---------- 第 1 步：解析 Annex B 文件（.h264/.h265） ----------
    std::vector<uint8_t> fileData; // 持有输入文件，保证 NalUnit::data 在整个转换期间有效
    std::vector<NalUnit>    allNals;
    std::vector<VideoFrame> frames;
    SpsInfo      h264Info;
    HevcSpsInfo  hevcInfo;

    if (!parseAnnexB(inPath, fileData, allNals, frames, h264Info, hevcInfo, codec)) return false;
    if (frames.empty()) {
        LOG("[MP4] No video frames in file: %s", inPath.c_str());
        return false;
    }

    bool hasSps = false, hasPps = false, hasVps = false;
    for (const auto& n : allNals) {
        if (isHevc) {
            hasVps = hasVps || n.type == MP4_NAL_TYPE_H265_VPS;
            hasSps = hasSps || n.type == MP4_NAL_TYPE_H265_SPS;
            hasPps = hasPps || n.type == MP4_NAL_TYPE_H265_PPS;
        } else {
            hasSps = hasSps || n.type == MP4_NAL_TYPE_SPS;
            hasPps = hasPps || n.type == MP4_NAL_TYPE_PPS;
        }
    }
    if (!hasSps || !hasPps || (isHevc && !hasVps)) {
        LOG("[MP4] Missing SPS/PPS%s in %s stream (SPS=%d, PPS=%d, VPS=%d)",
            isHevc ? "/VPS" : "", codec.c_str(),
            hasSps ? 1 : 0, hasPps ? 1 : 0, hasVps ? 1 : 0);
        return false;
    }

    // ---------- 第 2 步：收集帧元数据 ----------
    std::vector<size_t>   syncIdx;       // 关键帧索引（0-based）
    std::vector<uint32_t> sampleSizes;   // 每帧 NAL 字节数

    for (size_t i = 0; i < frames.size(); i++) {
        if (frames[i].is_keyframe) syncIdx.push_back(i);

        uint32_t sz = 0;
        for (auto& n : frames[i].nals)
            sz += 4 + static_cast<uint32_t>(n.size); // 4 字节长度前缀
        sampleSizes.push_back(sz);
    }

    // 时间参数
    uint32_t timescale  = 90000u;

    // 帧率计算优先级：
    //   1) 显式传入的 SPS 帧率（录制时从摄像头 SPS 解析，丢包时帧数/时长推算会偏低）
    //   2) 文件内 SPS VUI timing_info（rpc_remux 等未显式传帧率场景）
    //   3) 用实际媒体时长推算（无任何 SPS 帧率信息时的兜底，无丢包时准确）
    //   4) fallback 25fps
    const double parsedFps = isHevc ? hevcInfo.fps : h264Info.fps;
    double frameRateD = 0.0;
    if (spsFps > 0.0) {
        frameRateD = spsFps;
        LOG("[MP4] frameRate from SPS VUI (explicit): %.2f fps", frameRateD);
    } else if (parsedFps > 0.0) {
        frameRateD = parsedFps;
        LOG("[MP4] frameRate from SPS VUI: %.2f fps", frameRateD);
    } else if (recordDurationSec > 0.0 && !frames.empty()) {
        frameRateD = static_cast<double>(frames.size()) / recordDurationSec;
        LOG("[MP4] frameRate from record duration: %.2f fps (%zu frames / %.3f sec)",
            frameRateD, frames.size(), recordDurationSec);
    } else {
        LOG("[MP4] frameRate fallback to default: 25 fps");
    }
    if (frameRateD < 1.0 || frameRateD > 120.0) frameRateD = 25.0;

    // 帧间隔（stts 用整数值）；duration 用非取整帧率计算，
    // 保证总时长与真实媒体时长一致（避免帧率取整引入 1-2s 误差）
    double deltaD     = static_cast<double>(timescale) / frameRateD;
    uint32_t delta    = static_cast<uint32_t>(deltaD + 0.5);
    if (delta == 0) delta = 1;
    uint32_t duration = static_cast<uint32_t>(
        static_cast<double>(frames.size()) * deltaD + 0.5);
    if (duration == 0) duration = 1;

    // 画面尺寸：优先取解析到的 SPS 宽高，异常时 fallback
    uint32_t picWidth  = isHevc ? hevcInfo.width  : h264Info.width;
    uint32_t picHeight = isHevc ? hevcInfo.height : h264Info.height;
    if (picWidth == 0 || picHeight == 0) { picWidth = 640; picHeight = 480; }

    // ---------- 第 3 步：构建 avcC / hvcC ----------
    std::vector<uint8_t> decCfg;
    if (isHevc) buildHvcC(allNals, hevcInfo, decCfg);
    else        buildAvcC(allNals, decCfg);

    auto ftyp = buildFtyp(isHevc);

    // moov 构建 lambda
    auto buildMoov = [&](const std::vector<uint64_t>& offsets) {
        auto stcoBuf = buildStco(offsets);
        auto sttsBuf = buildStts(static_cast<uint32_t>(frames.size()), delta);
        auto stssBuf = buildStss(syncIdx);
        auto stszBuf = buildStsz(sampleSizes);
        auto stscBuf = buildStsc(1);  // 每个chunk恰好1个sample
        auto stsdBuf = buildStsd(decCfg, picWidth, picHeight, isHevc);

        std::vector<uint8_t> stbl;
        size_t stblOff = boxBegin(stbl, "stbl");
        stbl.insert(stbl.end(), stsdBuf.begin(), stsdBuf.end());
        stbl.insert(stbl.end(), sttsBuf.begin(), sttsBuf.end());
        stbl.insert(stbl.end(), stssBuf.begin(), stssBuf.end());
        stbl.insert(stbl.end(), stszBuf.begin(), stszBuf.end());
        stbl.insert(stbl.end(), stscBuf.begin(), stscBuf.end());
        stbl.insert(stbl.end(), stcoBuf.begin(), stcoBuf.end());
        boxEnd(stbl, stblOff);

        auto vmhdBuf  = buildVmhd();
        auto dinfBuf  = buildDinf();
        std::vector<uint8_t> minf;
        size_t minfOff = boxBegin(minf, "minf");
        minf.insert(minf.end(), vmhdBuf.begin(), vmhdBuf.end());
        minf.insert(minf.end(), dinfBuf.begin(), dinfBuf.end());
        minf.insert(minf.end(), stbl.begin(), stbl.end());
        boxEnd(minf, minfOff);

        auto mdhdBuf  = buildMdhd(timescale, duration);
        auto hdlrBuf  = buildHdlr();
        std::vector<uint8_t> mdia;
        size_t mdiaOff = boxBegin(mdia, "mdia");
        mdia.insert(mdia.end(), mdhdBuf.begin(), mdhdBuf.end());
        mdia.insert(mdia.end(), hdlrBuf.begin(), hdlrBuf.end());
        mdia.insert(mdia.end(), minf.begin(), minf.end());
        boxEnd(mdia, mdiaOff);

        auto tkhdBuf  = buildTkhd(picWidth, picHeight, duration);
        std::vector<uint8_t> trak;
        size_t trakOff = boxBegin(trak, "trak");
        trak.insert(trak.end(), tkhdBuf.begin(), tkhdBuf.end());
        trak.insert(trak.end(), mdia.begin(), mdia.end());
        boxEnd(trak, trakOff);

        auto mvhdBuf  = buildMvhd(timescale, duration);
        std::vector<uint8_t> moov;
        size_t moovOff = boxBegin(moov, "moov");
        moov.insert(moov.end(), mvhdBuf.begin(), mvhdBuf.end());
        moov.insert(moov.end(), trak.begin(), trak.end());
        boxEnd(moov, moovOff);

        return moov;
    };

    // ---------- 第 4 步：构建 mdat ----------
    // 先估算 moov 大小作为 mdat 偏移量的初始值
    size_t estMoov = 8 + 108 + 8 + 92 + 8 + 32 + 8 + 29 + 8 +
                     20 + 36 + 8 + 8 + 20 + decCfg.size() + 8 + 8 + 20 +
                     8 + 16 + 8 + 4 + syncIdx.size() * 4 +
                     8 + 8 + sampleSizes.size() * 4 +
                     8 + 16 + 8 + 8 + frames.size() * 4;
    uint64_t fileBase = ftyp.size() + estMoov + 8;

    std::vector<uint8_t> mdat;
    size_t mdatOff = boxBegin(mdat, "mdat");
    std::vector<uint64_t> chunkOffsets;

    for (size_t i = 0; i < frames.size(); i++) {
        chunkOffsets.push_back(fileBase + (mdat.size() - 8));
        for (auto& n : frames[i].nals) {
            w32be(mdat, static_cast<uint32_t>(n.size));
            mdat.insert(mdat.end(), n.data, n.data + n.size);
        }
    }
    boxEnd(mdat, mdatOff);

    // ---------- 第 5 步：构建 moov 并迭代修正 stco 偏移量 ----------
    auto moov = buildMoov(chunkOffsets);

    // 比较实际 moov 大小与估算值，修正偏移量直到稳定
    for (int iter = 0; iter < 3; iter++) {
        int64_t offsetDelta = static_cast<int64_t>(ftyp.size() + moov.size() + 8)
                            - static_cast<int64_t>(chunkOffsets[0]);
        if (offsetDelta == 0) break;
        LOG("[MP4] stco offset correction iter=%d, delta=%lld bytes",
            iter, (long long)offsetDelta);
        for (auto& off : chunkOffsets)
            off = static_cast<uint64_t>(static_cast<int64_t>(off) + offsetDelta);
        moov = buildMoov(chunkOffsets);
    }

    // ---------- 第 6 步：写入输出文件 ----------
#ifdef _WIN32
    std::ofstream ofs(utf8ToWide(mp4Path), std::ios::binary | std::ios::trunc);
#else
    std::ofstream ofs(mp4Path, std::ios::binary | std::ios::trunc);
#endif
    if (!ofs) {
        LOG("[MP4] Cannot create output file: %s", mp4Path.c_str());
        return false;
    }

    ofs.write(reinterpret_cast<const char*>(ftyp.data()), ftyp.size());
    ofs.write(reinterpret_cast<const char*>(moov.data()), moov.size());
    ofs.write(reinterpret_cast<const char*>(mdat.data()), mdat.size());
    ofs.close();

    LOG("[MP4] Converted: %s -> %s, %zu frames, %zu keyframes, duration=%u ms",
        inPath.c_str(), mp4Path.c_str(),
        frames.size(), syncIdx.size(), duration * 1000 / timescale);
    return true;
}

bool convertH264toMP4(const std::string& h264Path, const std::string& mp4Path,
                      double recordDurationSec, double spsFps) {
    return convertAnnexBtoMP4(h264Path, mp4Path, recordDurationSec, spsFps, "H264");
}

bool convertH265toMP4(const std::string& h265Path, const std::string& mp4Path,
                      double recordDurationSec, double spsFps) {
    return convertAnnexBtoMP4(h265Path, mp4Path, recordDurationSec, spsFps, "H265");
}

} // namespace mp4
