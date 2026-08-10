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

    // 跳过 NAL header (1 byte)
    BitReader br(data + 1, size - 1);

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

// ====================================================================
// Annex B 解析：从 .h264 文件提取 NAL 单元并分组为帧
// ====================================================================

static bool parseAnnexB(const std::string& path,
                        std::vector<uint8_t>& data,
                        std::vector<NalUnit>& all_nals,
                        std::vector<VideoFrame>& frames,
                        SpsInfo& spsInfo)
{
#ifdef _WIN32
    std::ifstream ifs(utf8ToWide(path), std::ios::binary);
#else
    std::ifstream ifs(path, std::ios::binary);
#endif
    if (!ifs) {
        LOG("[MP4] Cannot open .h264 file: %s", path.c_str());
        return false;
    }

    ifs.seekg(0, std::ios::end);
    size_t fileSize = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);

    if (fileSize < 4) {
        LOG("[MP4] .h264 file too small: %zu bytes", fileSize);
        return false;
    }

    data.resize(fileSize);
    ifs.read(reinterpret_cast<char*>(data.data()), fileSize);
    ifs.close();

    // 按 start code (00 00 00 01 或 00 00 01) 分割 NAL。
    // 注意：不能先跳过 0x00 再判断起始码，否则会把起始码本身跳掉。
    std::vector<uint8_t> sps_raw, pps_raw;
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

        uint8_t nalType = data[nalStart] & 0x1F;
        NalUnit nu;
        nu.data = data.data() + nalStart;
        nu.size = nalEnd - nalStart;
        nu.type = nalType;
        all_nals.push_back(nu);

        if (nalType == MP4_NAL_TYPE_SPS) {
            sps_raw.assign(nu.data, nu.data + nu.size);
        } else if (nalType == MP4_NAL_TYPE_PPS) {
            pps_raw.assign(nu.data, nu.data + nu.size);
        }

        pos = next;
        scLen = nextScLen;
    }

    if (!sps_raw.empty()) {
        parseSPS(sps_raw.data(), sps_raw.size(), spsInfo);
    }

    // 将 NAL 分组为 access unit。SPS/PPS/SEI/AUD 等前缀 NAL 归入下一帧；
    // VCL NAL 只有在 first_mb_in_slice==0 时才表示新画面的首个 slice。
    VideoFrame curFrame;
    std::vector<NalUnit> pendingPrefix;
    bool hasVcl = false;

    for (const auto& nal : all_nals) {
        const uint8_t t = nal.type;
        const bool isVcl = (t == MP4_NAL_TYPE_IDR || t == MP4_NAL_TYPE_NON_IDR);

        if (isVcl) {
            const bool startsNewPicture = isFirstSliceOfPicture(nal);
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
            curFrame.is_keyframe = curFrame.is_keyframe || (t == MP4_NAL_TYPE_IDR);
            hasVcl = true;
        } else {
            // AUD 明确标记下一 access unit 的开始。
            if (t == MP4_NAL_TYPE_AUD && hasVcl) {
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
// Box 构建
// ====================================================================

static std::vector<uint8_t> buildFtyp() {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "ftyp");
    w4cc(b, "isom");
    w32be(b, 0x200);
    w4cc(b, "isom");
    w4cc(b, "iso2");
    w4cc(b, "avc1");
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

static std::vector<uint8_t> buildStsd(const std::vector<uint8_t>& avcC,
                                      uint32_t width, uint32_t height) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stsd");
    w32be(b, 0);                    // version
    w32be(b, 1);                    // entry_count

    size_t offAvc1 = boxBegin(b, "avc1");
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

    // avcC sub-box
    size_t offAvcC = boxBegin(b, "avcC");
    b.insert(b.end(), avcC.begin(), avcC.end());
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

bool convertH264toMP4(const std::string& h264Path, const std::string& mp4Path,
                      int recordDurationSec) {
    // ---------- 第 1 步：解析 Annex B .h264 文件 ----------
    std::vector<uint8_t> fileData; // 持有输入文件，保证 NalUnit::data 在整个转换期间有效
    std::vector<NalUnit>    allNals;
    std::vector<VideoFrame> frames;
    SpsInfo                 spsInfo;

    if (!parseAnnexB(h264Path, fileData, allNals, frames, spsInfo)) return false;
    if (frames.empty()) {
        LOG("[MP4] No video frames in .h264 file");
        return false;
    }

    bool hasSps = false, hasPps = false;
    for (const auto& n : allNals) {
        hasSps = hasSps || n.type == MP4_NAL_TYPE_SPS;
        hasPps = hasPps || n.type == MP4_NAL_TYPE_PPS;
    }
    if (!hasSps || !hasPps) {
        LOG("[MP4] Missing SPS/PPS in H.264 stream (SPS=%d, PPS=%d)",
            hasSps ? 1 : 0, hasPps ? 1 : 0);
        return false;
    }

    // ---------- 第 2 步：收集帧元数据 ----------
    std::vector<size_t>   syncIdx;       // 关键帧索引（0-based）
    std::vector<uint32_t> sampleSizes;   // 每帧 AVC 字节数

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
    //   1) 用实际录制时长推算（最可靠，不依赖摄像头 SPS）
    //   2) SPS VUI timing_info
    //   3) fallback 25fps
    uint32_t frameRate = 25u;
    if (recordDurationSec > 0 && !frames.empty()) {
        frameRate = static_cast<uint32_t>(frames.size()) / static_cast<uint32_t>(recordDurationSec);
        if (frameRate == 0) frameRate = 1u;
        LOG("[MP4] frameRate from record duration: %u fps (%zu frames / %d sec)",
            frameRate, frames.size(), recordDurationSec);
    } else if (spsInfo.fps > 0.0) {
        frameRate = static_cast<uint32_t>(spsInfo.fps + 0.5);
        LOG("[MP4] frameRate from SPS VUI: %u fps", frameRate);
    } else {
        LOG("[MP4] frameRate fallback to default: %u fps", frameRate);
    }

    if (frameRate == 0) frameRate = 25u;
    uint32_t delta      = timescale / frameRate;
    if (delta == 0) delta = 1;
    uint32_t duration   = static_cast<uint32_t>(frames.size()) * delta;
    if (duration == 0) duration = 1;

    // ---------- 第 3 步：构建 avcC ----------
    std::vector<uint8_t> avcC;
    buildAvcC(allNals, avcC);

    auto ftyp = buildFtyp();

    // moov 构建 lambda
    auto buildMoov = [&](const std::vector<uint64_t>& offsets) {
        auto stcoBuf = buildStco(offsets);
        auto sttsBuf = buildStts(static_cast<uint32_t>(frames.size()), delta);
        auto stssBuf = buildStss(syncIdx);
        auto stszBuf = buildStsz(sampleSizes);
        auto stscBuf = buildStsc(1);  // 每个chunk恰好1个sample
        auto stsdBuf = buildStsd(avcC, spsInfo.width, spsInfo.height);

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

        auto tkhdBuf  = buildTkhd(spsInfo.width, spsInfo.height, duration);
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
                     20 + 36 + 8 + 8 + 20 + avcC.size() + 8 + 8 + 20 +
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
        h264Path.c_str(), mp4Path.c_str(),
        frames.size(), syncIdx.size(), duration * 1000 / timescale);
    return true;
}

} // namespace mp4
