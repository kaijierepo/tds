#pragma once

#include "dtls_transport.h"
#include "srtp_protect.h"

// 每个 session 独有的 DTLS/SRTP 状态
struct SessionDtlsState {
    DtlsTransport dtls;
    SrptProtect::Context srtp_ctx;
    bool dtls_initialized = false;
    bool srtp_ready = false;
    // Per-session RTP 序列号管理：原始流的 seq 不能直接透传，
    // 每个 WebRTC 客户端需要独立连续的序列号
    uint16_t local_seq = 0;
    bool    seq_inited = false;
    // SPS/PPS 注入标记（替代 session->state 的 3→4 标记）
    bool    sps_pps_injected = false;
};
