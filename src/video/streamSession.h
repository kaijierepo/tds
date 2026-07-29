#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>

#include "streamCommon.h"
#include "streamSession_webrtc.h"

// ============================================================================
// Connection — TCP 连接（通用，无 StreamNode 依赖）
// ============================================================================

class Connection {
public:
    Connection();
    ~Connection();

    bool connect(const std::string& host, int port, int timeout_ms = 5000);
    void disconnect();
    bool isConnected() const;
    int lastError() const { return last_error_; }

    int send(const void* data, size_t size, int timeout_ms = 5000);
    int receive(void* buffer, size_t size, int timeout_ms = 5000);
    int receiveHttpResp(std::string& response, int timeout_ms = 5000);

    // RTSP CSeq 计数器（每条信令连接独立递增，从 1 开始）
    std::string nextCSeq() { return std::to_string(++cseq_); }

    SocketHandle getSocket() const { return sockfd_; }

private:
    SocketHandle sockfd_ = kInvalidSocket;
    int cseq_ = 0;
    std::string host_;
    int port_ = 0;
    int last_error_ = 0;
    bool setSocketTimeout(int timeout_ms);
};

// Forward declaration（仅指针/引用使用，无需完整定义）
class SrptProtect;

// ============================================================================
// Session 类型与状态枚举
// ============================================================================

enum STREAM_SESSION_TYPE {
    ORIGIN_PULL,             // 自身作为客户端，向服务端拉流
    RELAY_PUSH,              // 自身作为客户端，向服务端推流
    CLIENT_RTSP_PULL,        // 自身作为服务端，接收rtsp客户端拉流
    CLIENT_RTSP_PUBLISH,     // 自身作为服务端，接收rtsp客户端推流
    CLIENT_WEBRTC_PULL,      // 自身作为服务端，接收webrtc客户端拉流
    CLIENT_WEBRTC_PUBLISH    // 自身作为服务端，接收webrtc客户端推流
};

enum SESSION_STATE {
    SESSION_IDLE = 0,
    SESSION_CONNECTING,
    SESSION_HANDSHAKING,
    SESSION_STREAMING,
    SESSION_ERROR,
    SESSION_RECONNECTING
};

enum class TransportMode {
    NONE,
    UDP,
    TCP
};

// ============================================================================
// STREAM_SESSION — 媒体流会话
// 注意：WebRTC 会话通过 shared_ptr 管理；含 std::thread 成员，禁止值拷贝
// ============================================================================

struct STREAM_SESSION {
    std::string control_url;
    std::string codec = "H264";
    int payload_type = 96;
    int clock_rate = 90000;
    std::string fmtp;
    // 如果 SDP 中包含 sprop-parameter-sets，会把解码后的 SPS/PPS 保存到这里
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    std::string sdp;
    STREAM_SESSION_TYPE session_type_;
    std::string tag_;
    std::string stream_url_;
    std::chrono::system_clock::time_point last_stun_bind_req_time;
    std::chrono::system_clock::time_point open_time_;

    // 传输信息
    TransportMode transport_mode = TransportMode::UDP;
    std::string transport;
    std::string remote_host;
    std::string client_port;
    std::string server_port;
    int client_rtp_port = 0;
    int client_rtcp_port = 0;
    int server_rtp_port = 0;
    int server_rtcp_port = 0;
    SocketHandle rtp_socket = kInvalidSocket;
    SocketHandle rtcp_socket = kInvalidSocket;
    SocketHandle tcp_socket = kInvalidSocket;  // TCP interleaved 模式使用的 RTSP 连接
    int interleaved_rtp = -1;      // TCP interleaved RTP 通道号
    int interleaved_rtcp = -1;     // TCP interleaved RTCP 通道号

    long long rtpBytesSended = 0;

    // ICE-Lite (WebRTC) 字段
    bool is_webrtc = false;
    std::string ice_ufrag;
    std::string ice_pwd;
    SESSION_STATE state_ = SESSION_STATE::SESSION_IDLE;

    // 从实际 RTP 流中捕获的视频 SSRC（用于 SDP 声明）
    uint32_t      video_ssrc = 0;

    // DTLS/SRTP 状态（per-session，由 ice 线程管理）
    std::shared_ptr<SessionDtlsState> dtls_transport_ = nullptr;
    SrptProtect::Context* srtp_context_   = nullptr;  // 指向 SrptProtect::Context 实例

    // TCP 控制连接（RTSP 信令连接），ORIGIN_PULL/RELAY_PUSH 使用
    std::unique_ptr<Connection> conn_;
    std::string rtsp_session_id_;  // RTSP Session ID（SETUP 响应返回）
    std::string server_url_;       // 远端 RTSP 服务地址
    std::string server_username_;  // 远端认证用户名
    std::string server_password_;  // 远端认证密码

    // 远端认证运行时状态（Digest/Basic，RTSP 信令过程中动态填充）
    std::string server_auth_realm_;
    std::string server_auth_nonce_;
    std::string server_auth_algorithm_;
    std::string server_auth_header_;   // 缓存的 Authorization header
    bool server_auth_use_digest_ = false;

    bool hasAuthCredentials() const {
        return !server_username_.empty() && !server_password_.empty();
    }

    void clearAuthRuntime() {
        server_auth_realm_.clear();
        server_auth_nonce_.clear();
        server_auth_algorithm_.clear();
        server_auth_header_.clear();
        server_auth_use_digest_ = false;
    }

    // 重连与超时配置（per-session）
    int retry_interval_ = 3000;   // 重试间隔(ms)
    int max_retries_ = 10;        // 最大重试次数，0=无限重试
    int rtp_timeout_ = 5000;      // RTP超时(ms)
    int buffer_size_ = 65536;     // 接收缓冲区大小
    int retry_count_ = 0;         // 当前重试次数
    std::chrono::steady_clock::time_point last_reconnect_time_;  // 上次重连时间

    // UDP 传输配置（per-session）
    int udp_recv_buffer_size = 4194304;  // UDP接收缓冲区(4MB)，避免4K高码流内核丢包
    int udp_send_buffer_size = 0;        // UDP发送缓冲区大小(0=系统默认)
    int udp_ttl = 64;                    // TTL生存时间
    int udp_tos = 0xC0;                  // Type of Service (Default: AF41 低延迟)
    bool udp_multicast_loop = false;     // 组播回环

    // 新会话首次发送数据标记：首次先发 SPS/PPS + 缓存的关键帧，再开始转发实时流
    bool is_first_send_ = true;

    // ---- 以下成员仅 WebRTC (is_webrtc=true) 使用 ----
    // ICE 处理线程（由 startRtcSessionHandleThread 创建，stopAllIceThreads 回收）
    std::thread rtc_handle_thread_;
    std::atomic<bool> rtc_handle_thread_running_{true};

    STREAM_SESSION() = default;
    STREAM_SESSION(STREAM_SESSION&&) = default;
    STREAM_SESSION& operator=(STREAM_SESSION&&) = default;

    // 禁止拷贝：session 含 conn_ (unique_ptr)、socket 句柄、线程等不可安全拷贝的资源
    STREAM_SESSION(const STREAM_SESSION&) = delete;
    STREAM_SESSION& operator=(const STREAM_SESSION&) = delete;

    // 复制视频流元数据（codec/sps/pps/control_url 等），不复制 socket/连接/认证/状态
    void copyStreamInfoFrom(const STREAM_SESSION& other);

    // 复制 client pull 会话所需字段（流信息 + 传输 + socket 句柄）。
    // 注意：socket 句柄会被共享，调用方需确保生命周期管理正确。
    void copyClientSessionFrom(const STREAM_SESSION& other);

    // ---- UDP socket 操作（实现见 streamSession_socket.cpp）----
    // 配置 UDP socket（超时/TTL/ToS/缓冲区/地址重用）
    bool configureUDPSocket(SocketHandle sock, bool is_multicast);
    // 创建连续的 RTP+RTCP socket 对
    // isServer: true=设置 server_rtp_port/server_rtcp_port, false=设置 client_rtp_port/client_rtcp_port
    bool createUDPConsecutiveSockets(bool isServer);
    // 关闭本 session 的 RTP/RTCP socket
    void closeSockets();
    // 发送 UDP 数据到本 session 的远端
    bool sendUDPData(const uint8_t* data, size_t size);
    // 从本 session 的 RTP socket 接收 UDP 数据
    int receiveUDPData(uint8_t* buffer, size_t size, std::string& src_ip, int& src_port);

    // 设置会话状态
    void setState(SESSION_STATE new_state);
    // 重连策略判断：未超 max_retries_ 且距上次重连超过 retry_interval_
    bool shouldReconnect() const;
    // 执行重连：计数+记录时间+按 retry_interval_ 退避
    void doReconnect();

    // 解析 SDP：视频轨写入本 session，音频轨写入 audio_info
    bool parseSDP(const std::string& sdp, STREAM_SESSION& audio_info);
    // 由本 session（视频轨）生成 SDP
    std::string generateSDP() const;

    std::string getSessionStateDesc();
    std::string getTypeDesc() const;

	// 关闭会话：发送 RTSP TEARDOWN + 断开连接 + 清除状态 + 关闭 socket
    void close();

    // 打开会话：根据 session_type_ 执行 ORIGIN_PULL（DESCRIBE→SETUP→PLAY）
    // 或 RELAY_PUSH（ANNOUNCE→SETUP→RECORD）
    // RELAY_PUSH 分支需要 origin_session 复制视频流信息（codec/sps/pps）
    bool open(const STREAM_SESSION* origin_session = nullptr);

public:
    // 认证辅助函数（RTSP Digest/Basic）
    std::string calcBasicAuth() const;
    std::string calcDigestAuth(const std::string& method, const std::string& uri) const;
    void buildAuthHeader(const std::string& method, const std::string& uri);

    // RTSP 控制方法
    bool rtspDescribeReq(const std::string& url,
        std::string& sdp, std::string& session);
    bool rtspSetupReq(const std::string& url,
        std::string& session, STREAM_SESSION& stream, bool record_mode = false);
    bool rtspPlayReq(const std::string& url,
        const std::string& session);
    bool rtspTeardownReq(const std::string& url,
        const std::string& session);
    bool rtspAnnounceReq(const std::string& url,
        const std::string& sdp, std::string& session);
    bool rtspRecordReq(const std::string& url,
        const std::string& session);
    bool rtspGetParameterReq(const std::string& url,
        const std::string& session);

private:
    // RTSP 辅助函数
    std::string extractSessionID(const std::string& response);
    std::string extractTransport(const std::string& response);
    bool parseWWWAuthenticate(const std::string& response, STREAM_SESSION& session);
};
