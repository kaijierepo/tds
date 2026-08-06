// ============================================================================
// mp4Writer.h - 轻量 MP4 容器封装器
// 将 H.264 Annex B 裸流 (.h264) 转换为 MP4 容器 (.mp4)
// 零外部依赖，纯 C++ 标准库实现
// ============================================================================
#pragma once

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
// 返回 true 表示转换成功
bool convertH264toMP4(const std::string& h264Path, const std::string& mp4Path);

} // namespace mp4
