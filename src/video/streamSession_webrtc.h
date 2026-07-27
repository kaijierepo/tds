#pragma once

#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>

#include "dtls_transport.h"
#include "srtp_protect.h"

// NACK 重传缓存的一个 SRTP 包（已加密的原始字节，按 seq 索引）
struct RtxCachedPacket {
    uint16_t seq = 0;
    bool valid = false;
    std::vector<uint8_t> data;
};

// 每个 session 独有的 DTLS/SRTP 状态
struct SessionDtlsState {
    // NACK 重传环形缓冲大小（按 seq % N 索引，覆盖最近 N 个包）
    static const size_t kRtxBufSize = 1024;

    SessionDtlsState() : rtx_buf_(kRtxBufSize) {}

    DtlsTransport dtls;
    SrptProtect::Context srtp_ctx;
    // 接收方向上下文（client_write 主密钥），用于解密浏览器反馈的 SRTCP(NACK/PLI)
    SrptProtect::Context recv_ctx;
    bool dtls_initialized = false;
    bool srtp_ready = false;
    // Per-session RTP 序列号管理：原始流的 seq 不能直接透传，
    // 每个 WebRTC 客户端需要独立连续的序列号
    uint16_t local_seq = 0;
    bool    seq_inited = false;
    // SPS/PPS 注入标记（替代 session->state 的 3→4 标记）
    bool    sps_pps_injected = false;
    // 本会话上次向浏览器发送关键帧(IDR)的时间，用于弱网下主动重发关键帧，
    // 使解码器卡顿的浏览器在有限时间内恢复。仅由接收线程读写。
    std::chrono::steady_clock::time_point last_idr_sent_time_{};

    // 浏览器 PLI/FIR 关键帧请求标志：ICE 线程检测到反馈后置位，
    // 转发线程在发送循环中消费并重发缓存的关键帧（避免 ICE 线程并发发 SRTP）
    std::atomic<bool> request_keyframe_resend_{false};

    // NACK 重传缓存：转发线程写入已加密的 SRTP 字节，ICE 线程收到 NACK 后读取并原样重发。
    // 重发的是密文，不触碰 srtp_ctx/local_seq，无并发风险；mutex 仅保护缓冲本身。
    std::vector<RtxCachedPacket> rtx_buf_;
    std::mutex rtx_mutex_;
};

struct STREAM_SESSION;

// ICE-Lite STUN 处理：解析 Binding Request 并回复 Binding Success Response
void sessionHandleSTUN(std::shared_ptr<STREAM_SESSION> session,
                       uint8_t* buf, int len,
                       struct sockaddr_in& peer,
                       std::chrono::steady_clock::time_point& dtls_start);

// DTLS 握手处理：喂入数据并推进握手状态机
void sessionHandleDTLS(std::shared_ptr<STREAM_SESSION> session,
                       SessionDtlsState* dtls_state,
                       uint8_t* buf, int len,
                       struct sockaddr_in& peer,
                       std::chrono::steady_clock::time_point& dtls_start);

// SRTCP 反馈处理：解密浏览器的 RTCP compound，识别 PLI/FIR（请求关键帧）
// 与 NACK（按序号从重传缓存重发已加密的 SRTP 包）
void sessionHandleSRTCP(std::shared_ptr<STREAM_SESSION> session,
                        SessionDtlsState* dtls_state,
                        uint8_t* buf, int len,
                        struct sockaddr_in& peer);

// 从浏览器 SDP Offer 中提取 H264 payload type（返回 0 表示未找到）
int parseH264PTFromOffer(const std::string& sdpOffer);

// WebRTC SDP Answer 构建（设置 si.is_webrtc/ice_ufrag/ice_pwd/sdp）
void buildWebRTCSdpAnswer(STREAM_SESSION& si, const std::string& serverIp,
                          const std::string& dtlsFingerprint);
