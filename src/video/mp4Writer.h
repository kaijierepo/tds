#ifndef TDS_VIDEO_MP4WRITER_H
#define TDS_VIDEO_MP4WRITER_H

// ============================================================================
// mp4Writer.h - 轻量 MP4 容器封装器
// 将 H.264/H.265 Annex B 裸流文件 (.h264/.h265) 转换为 MP4 容器 (.mp4)
// 零外部依赖，纯 C++ 标准库实现
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>

// NAL 类型定义（与 streamNode.h 保持一致）
#define MP4_NAL_TYPE_NON_IDR  1
#define MP4_NAL_TYPE_IDR      5
#define MP4_NAL_TYPE_SEI      6
#define MP4_NAL_TYPE_SPS      7
#define MP4_NAL_TYPE_PPS      8
#define MP4_NAL_TYPE_AUD      9

// H.265 (HEVC) NAL unit type (RFC 7798)
#define MP4_NAL_TYPE_H265_VPS  32
#define MP4_NAL_TYPE_H265_SPS  33
#define MP4_NAL_TYPE_H265_PPS  34
#define MP4_NAL_TYPE_H265_AUD  35

namespace mp4 {

// H.264 SPS 解析结果
struct SpsInfo {
    uint32_t width       = 640;
    uint32_t height      = 480;
    uint32_t profile_idc = 0x42;  // baseline
    uint32_t level_idc   = 0x1E;  // 3.0
    double   fps         = 0.0;   // 0=未从VUI解析到，需fallback
    bool     valid       = false;
};

// H.265 SPS 解析结果：宽高、VUI 帧率，以及构建 hvcC 所需字段
struct HevcSpsInfo {
    uint32_t width       = 0;
    uint32_t height      = 0;
    double   fps         = 0.0;   // 0=未从VUI解析到，需fallback
    bool     valid       = false;
    // profile_tier_level 的 general 部分前 12 字节（HEVCDecoderConfigurationRecord 直接用）
    uint8_t  general_profile_tier_level[12] = { 0 };
    uint8_t  chroma_format_idc       = 1;
    uint8_t  bit_depth_luma_minus8   = 0;
    uint8_t  bit_depth_chroma_minus8 = 0;
    uint8_t  max_sub_layers_minus1   = 0;   // hvcC 的 numTemporalLayers
    uint8_t  temporal_id_nesting_flag = 1;  // hvcC 的 temporalIdNested
};

// 从 SPS NAL（含 NAL header）解析 VUI timing_info 帧率，失败或无 VUI 返回 0.0
double parseSpsFps(const uint8_t* spsNal, size_t size);

// 从 H.265 SPS NAL（含 2 字节 NAL header）解析宽高与 VUI 帧率
bool parseHevcSps(const uint8_t* spsNal, size_t size, HevcSpsInfo& info);

// 从 H.265 SPS 解析 VUI timing_info 帧率，失败或无 VUI 返回 0.0
double parseHevcSpsFps(const uint8_t* spsNal, size_t size);

// NAL 单元（引用 .h264 文件数据，不持有内存）
struct NalUnit {
    const uint8_t* data;
    size_t         size;
    uint8_t        type;
};

// 视频帧（access unit），包含一组 NAL 单元
struct VideoFrame {
    std::vector<NalUnit> nals;
    bool is_keyframe = false;
};

// 将 H.264 Annex B 裸流文件转换为 MP4 容器文件
// h264Path: 输入 .h264 文件路径（UTF-8）
// mp4Path:  输出 .mp4 文件路径（UTF-8）
// recordDurationSec: 录像真实媒体时长（秒，浮点精度）
// spsFps: 录制时从 SPS VUI 解析的帧率（>0 时最高优先级，丢包场景帧率仍正确）
// 帧率优先级：显式 spsFps > 文件内 SPS VUI > 帧数/时长推算 > 25fps
// 返回 true 表示转换成功
bool convertH264toMP4(const std::string& h264Path, const std::string& mp4Path,
                      double recordDurationSec = 0, double spsFps = 0);

// 将 H.265 Annex B 裸流文件（.h265，含 VPS/SPS/PPS 与 2 字节 NAL 头）转换为 MP4
// 参数语义与 convertH264toMP4 一致
bool convertH265toMP4(const std::string& h265Path, const std::string& mp4Path,
                      double recordDurationSec = 0, double spsFps = 0);

} // namespace mp4

#endif /* TDS_VIDEO_MP4WRITER_H */
