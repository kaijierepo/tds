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

    info.width  = pic_w_mbs * 16;
    info.height = pic_h_units * 16 * (2 - frame_mbs_only);
    info.valid  = true;
    return true;
}

// ====================================================================
// Annex B 解析：从 .h264 文件提取 NAL 单元并分组为帧
// ====================================================================

static bool parseAnnexB(const std::string& path,
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

    std::vector<uint8_t> data(fileSize);
    ifs.read(reinterpret_cast<char*>(data.data()), fileSize);
    ifs.close();

    // 按 start code (00 00 00 01 或 00 00 01) 分割 NAL
    std::vector<uint8_t> sps_raw, pps_raw;
    size_t pos = 0;

    while (pos + 3 <= fileSize) {
        // 跳过前导零
        while (pos < fileSize && data[pos] == 0) pos++;
        if (pos + 3 > fileSize) break;

        size_t scLen;
        if (data[pos] == 0x00 && data[pos + 1] == 0x00) {
            if (data[pos + 2] == 0x01)
                scLen = 3;
            else if (pos + 4 <= fileSize && data[pos + 2] == 0x00 && data[pos + 3] == 0x01)
                scLen = 4;
            else { pos++; continue; }
        } else { pos++; continue; }

        size_t nalStart = pos + scLen;

        // 找下一个 start code
        size_t next = nalStart;
        while (next + 2 < fileSize) {
            if (data[next] == 0x00 && data[next + 1] == 0x00) {
                if (data[next + 2] == 0x01) break;
                if (next + 3 < fileSize && data[next + 2] == 0x00 && data[next + 3] == 0x01) break;
            }
            next++;
        }
        size_t nalEnd = (next + 2 >= fileSize) ? fileSize : next;
        // 去掉尾部零
        while (nalEnd > nalStart && data[nalEnd - 1] == 0) nalEnd--;

        if (nalEnd <= nalStart) { pos = next; continue; }

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

        pos = nalEnd;
    }

    if (!sps_raw.empty()) {
        parseSPS(sps_raw.data(), sps_raw.size(), spsInfo);
    }

    // 将 NAL 分组为帧（access unit）
    // 规则：VCL NAL (type 1 或 5) 开始新帧，前面的非 VCL NAL 归入下一帧
    VideoFrame curFrame;

    for (size_t i = 0; i < all_nals.size(); i++) {
        uint8_t t = all_nals[i].type;

        if (t == MP4_NAL_TYPE_IDR || t == MP4_NAL_TYPE_NON_IDR) {
            // VCL NAL 开始新帧
            bool hasVcl = false;
            for (auto& n : curFrame.nals) {
                if (n.type == MP4_NAL_TYPE_IDR || n.type == MP4_NAL_TYPE_NON_IDR) {
                    hasVcl = true;
                    break;
                }
            }
            if (hasVcl) {
                frames.push_back(curFrame);
                curFrame = VideoFrame();
            }
            curFrame.nals.push_back(all_nals[i]);
            curFrame.is_keyframe = (t == MP4_NAL_TYPE_IDR);
        } else {
            curFrame.nals.push_back(all_nals[i]);
        }
    }
    // 最后一帧
    if (!curFrame.nals.empty()) {
        bool hasVcl = false;
        for (auto& n : curFrame.nals) {
            if (n.type == MP4_NAL_TYPE_IDR || n.type == MP4_NAL_TYPE_NON_IDR) {
                hasVcl = true;
                break;
            }
        }
        if (hasVcl) {
            frames.push_back(curFrame);
        }
    }

    return true;
}

// ====================================================================
// avcC (AVCDecoderConfigurationRecord) 构建
// ====================================================================

static void buildAvcC(const std::vector<NalUnit>& all_nals, std::vector<uint8_t>& out) {
    std::vector<const uint8_t*> sps_ptrs;
    std::vector<size_t>        sps_lens;
    std::vector<const uint8_t*> pps_ptrs;
    std::vector<size_t>        pps_lens;

    for (auto& n : all_nals) {
        if (n.type == MP4_NAL_TYPE_SPS) {
            sps_ptrs.push_back(n.data);
            sps_lens.push_back(n.size);
        } else if (n.type == MP4_NAL_TYPE_PPS) {
            pps_ptrs.push_back(n.data);
            pps_lens.push_back(n.size);
        }
    }

    out.push_back(0x01);  // configurationVersion
    out.push_back(sps_ptrs.empty() ? 0x42 : sps_ptrs[0][1]);  // profile
    out.push_back(sps_ptrs.empty() ? 0x00 : sps_ptrs[0][2]);  // compat
    out.push_back(sps_ptrs.empty() ? 0x1E : sps_ptrs[0][3]);  // level
    out.push_back(0xFF);  // lengthSizeMinusOne: 4 bytes

    // SPS
    out.push_back(0xE0 | static_cast<uint8_t>(sps_ptrs.size() & 0x1F));
    for (size_t i = 0; i < sps_ptrs.size(); i++) {
        w16be(out, static_cast<uint16_t>(sps_lens[i]));
        out.insert(out.end(), sps_ptrs[i], sps_ptrs[i] + sps_lens[i]);
    }
    // PPS
    out.push_back(static_cast<uint8_t>(pps_ptrs.size()));
    for (size_t i = 0; i < pps_ptrs.size(); i++) {
        w16be(out, static_cast<uint16_t>(pps_lens[i]));
        out.insert(out.end(), pps_ptrs[i], pps_ptrs[i] + pps_lens[i]);
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

static std::vector<uint8_t> buildMvhd(uint32_t timescale, uint32_t duration) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "mvhd");
    w32be(b, 0);                    // version+flags
    w32be(b, 0); w32be(b, 0);      // ctime+mtime
    w32be(b, timescale);
    w32be(b, duration);
    w32be(b, 0x00010000);          // rate 1.0
    w16be(b, 0x0100); w16be(b, 0); // volume
    // matrix (9×4 = 36 bytes)
    w32be(b, 0x00010000); w32be(b, 0); w32be(b, 0); w32be(b, 0);
    w32be(b, 0x00010000); w32be(b, 0); w32be(b, 0); w32be(b, 0);
    w32be(b, 0x40000000);
    // pre_defined (6×4 = 24 bytes)
    for (int i = 0; i < 6; i++) w32be(b, 0);
    w32be(b, 1);  // next_track_id
    boxEnd(b, off);
    return b;
}

static std::vector<uint8_t> buildTkhd(uint32_t width, uint32_t height) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "tkhd");
    w32be(b, 0x07);                 // flags: track_enabled
    w32be(b, 0); w32be(b, 0);      // ctime+mtime
    w32be(b, 1);                    // track_id
    w32be(b, 0);                    // reserved
    w32be(b, 0);                    // duration (from mvhd)
    w32be(b, 0); w32be(b, 0);      // reserved
    w16be(b, 0); w16be(b, 0);      // layer+alt_group
    w16be(b, 0x0100);               // volume
    w32be(b, 0);                    // reserved
    // matrix
    w32be(b, 0x00010000); w32be(b, 0); w32be(b, 0); w32be(b, 0);
    w32be(b, 0x00010000); w32be(b, 0); w32be(b, 0); w32be(b, 0);
    w32be(b, 0x40000000);
    // width/height (16.16 fixed point)
    w32be(b, width  << 16);
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

static std::vector<uint8_t> buildStsd(const std::vector<uint8_t>& avcC) {
    std::vector<uint8_t> b;
    size_t off = boxBegin(b, "stsd");
    w32be(b, 0);                    // version
    w32be(b, 1);                    // entry_count

    size_t offAvc1 = boxBegin(b, "avc1");
    w32be(b, 0); w16be(b, 0);      // reserved
    w16be(b, 1);                    // data_ref_index
    w16be(b, 0); w16be(b, 0);      // pre_defined+reserved
    w32be(b, 0); w32be(b, 0); w32be(b, 0); // pre_defined
    w16be(b, 0); w16be(b, 0);      // width+height (placeholder, tkhd has real values)
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

bool convertH264toMP4(const std::string& h264Path, const std::string& mp4Path) {
    // ---------- 第 1 步：解析 Annex B .h264 文件 ----------
    std::vector<NalUnit>    allNals;
    std::vector<VideoFrame> frames;
    SpsInfo                 spsInfo;

    if (!parseAnnexB(h264Path, allNals, frames, spsInfo)) return false;
    if (frames.empty()) {
        LOG("[MP4] No video frames in .h264 file");
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
    uint32_t frameRate  = 25u;  // 默认 25fps
    uint32_t delta      = timescale / frameRate;
    uint32_t duration   = static_cast<uint32_t>(frames.size()) * delta;
    if (duration == 0) duration = 1;

    // ---------- 第 3 步：构建 avcC ----------
    std::vector<uint8_t> avcC;
    buildAvcC(allNals, avcC);

    // ---------- 第 4 步：构建 mdat（记录 chunk offset） ----------
    std::vector<uint8_t> mdat;
    size_t mdatOff = boxBegin(mdat, "mdat");
    std::vector<uint64_t> chunkOffsets;

    // mdat 在文件中的起始位置
    uint64_t fileBase = 0;
    {
        // ftyp 大小固定
        std::vector<uint8_t> dummyFtyp = buildFtyp();
        fileBase = dummyFtyp.size();

        // moov 预估大小：mvhd + trak(tkhd + mdhd + hdlr + vmhd + dinf + stbl(stsd+stts+stss+stsz+stsc+stco)) + 8
        // 保守估计用固定值
        // stco 条目数 = frames.size()，每个 4 字节
        size_t estMoov = 8 +                                  // moov header
                          108 +                                // mvhd
                          8 +                                  // trak header
                          92 +                                 // tkhd
                          8 +                                  // mdia header
                          32 +                                 // mdhd
                          8 + 29 +                             // hdlr
                          8 +                                  // minf header
                          20 +                                 // vmhd
                          36 +                                 // dinf
                          8 +                                  // stbl header
                          8 + 20 + avcC.size() + 8 + 8 + 20 +  // stsd
                          8 + 16 +                             // stts
                          8 + 4 + syncIdx.size() * 4 +         // stss
                          8 + 8 + sampleSizes.size() * 4 +     // stsz
                          8 + 16 +                             // stsc
                          8 + 8 + frames.size() * 4;           // stco

        fileBase += estMoov + 8;  // +8 for mdat header (size+type)
    }

    for (size_t i = 0; i < frames.size(); i++) {
        chunkOffsets.push_back(fileBase + (mdat.size() - 8));
        for (auto& n : frames[i].nals) {
            w32be(mdat, static_cast<uint32_t>(n.size));
            mdat.insert(mdat.end(), n.data, n.data + n.size);
        }
    }
    boxEnd(mdat, mdatOff);

    // ---------- 第 5 步：构建 stco（含实际 mdat 偏移） ----------
    auto stcoBuf       = buildStco(chunkOffsets);
    auto sttsBuf       = buildStts(static_cast<uint32_t>(frames.size()), delta);
    auto stssBuf       = buildStss(syncIdx);
    auto stszBuf       = buildStsz(sampleSizes);
    auto stscBuf       = buildStsc(static_cast<uint32_t>(frames.size()));  // 所有帧一帧一 chunk
    auto stsdBuf       = buildStsd(avcC);

    // ---------- 第 6 步：组装 box 树 ----------
    // stbl = stsd + stts + stss + stsz + stsc + stco
    std::vector<uint8_t> stbl;
    size_t stblOff = boxBegin(stbl, "stbl");
    stbl.insert(stbl.end(), stsdBuf.begin(), stsdBuf.end());
    stbl.insert(stbl.end(), sttsBuf.begin(), sttsBuf.end());
    stbl.insert(stbl.end(), stssBuf.begin(), stssBuf.end());
    stbl.insert(stbl.end(), stszBuf.begin(), stszBuf.end());
    stbl.insert(stbl.end(), stscBuf.begin(), stscBuf.end());
    stbl.insert(stbl.end(), stcoBuf.begin(), stcoBuf.end());
    boxEnd(stbl, stblOff);

    // minf = vmhd + dinf + stbl
    auto vmhdBuf  = buildVmhd();
    auto dinfBuf  = buildDinf();
    std::vector<uint8_t> minf;
    size_t minfOff = boxBegin(minf, "minf");
    minf.insert(minf.end(), vmhdBuf.begin(), vmhdBuf.end());
    minf.insert(minf.end(), dinfBuf.begin(), dinfBuf.end());
    minf.insert(minf.end(), stbl.begin(), stbl.end());
    boxEnd(minf, minfOff);

    // mdia = mdhd + hdlr + minf
    auto mdhdBuf  = buildMdhd(timescale, duration);
    auto hdlrBuf  = buildHdlr();
    std::vector<uint8_t> mdia;
    size_t mdiaOff = boxBegin(mdia, "mdia");
    mdia.insert(mdia.end(), mdhdBuf.begin(), mdhdBuf.end());
    mdia.insert(mdia.end(), hdlrBuf.begin(), hdlrBuf.end());
    mdia.insert(mdia.end(), minf.begin(), minf.end());
    boxEnd(mdia, mdiaOff);

    // trak = tkhd + mdia
    auto tkhdBuf  = buildTkhd(spsInfo.width, spsInfo.height);
    std::vector<uint8_t> trak;
    size_t trakOff = boxBegin(trak, "trak");
    trak.insert(trak.end(), tkhdBuf.begin(), tkhdBuf.end());
    trak.insert(trak.end(), mdia.begin(), mdia.end());
    boxEnd(trak, trakOff);

    // moov = mvhd + trak
    auto mvhdBuf  = buildMvhd(timescale, duration);
    std::vector<uint8_t> moov;
    size_t moovOff = boxBegin(moov, "moov");
    moov.insert(moov.end(), mvhdBuf.begin(), mvhdBuf.end());
    moov.insert(moov.end(), trak.begin(), trak.end());
    boxEnd(moov, moovOff);

    // ftyp
    auto ftyp = buildFtyp();

    // 验证偏移量
    if (ftyp.size() + moov.size() + 8 != chunkOffsets[0]) {
        LOG("[MP4] Warning: moov size estimation off by %lld bytes",
            (long long)(chunkOffsets[0] - ftyp.size() - moov.size() - 8));
    }

    // ---------- 第 7 步：写入输出文件 ----------
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
