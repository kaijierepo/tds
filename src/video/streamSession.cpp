// ============================================================================
// streamSession.cpp — STREAM_SESSION 通用方法实现
// ============================================================================

#include "streamSession.h"

std::string STREAM_SESSION::getSessionStateDesc()
{
    if (state_ == SESSION_STATE::SESSION_IDLE) {
        return "idle";
    }
    else if(state_ == SESSION_STATE::SESSION_CONNECTING) {
        return "connecting";
    }
    else if(state_ == SESSION_STATE::SESSION_HANDSHAKING) {
        return "handshaking";
    }
    else if(state_ == SESSION_STATE::SESSION_STREAMING) {
        return "streaming";
    }
    else if(state_ == SESSION_STATE::SESSION_ERROR) {
        return "error";
    }
    else if(state_ == SESSION_STATE::SESSION_RECONNECTING) {
        return "reconnecting";
    }
    return "unknown";
}

std::string STREAM_SESSION::getTypeDesc() const
{
    if (session_type_ == STREAM_SESSION_TYPE::ORIGIN_PULL) {
        return "origin_pull";
    }
    else if (session_type_ == STREAM_SESSION_TYPE::RELAY_PUSH) {
        return "relay_push";
    }
    else if (session_type_ == STREAM_SESSION_TYPE::CLIENT_RTSP_PULL) {
        return "client_rtsp_pull";
    }
    else if (session_type_ == STREAM_SESSION_TYPE::CLIENT_RTSP_PUBLISH) {
        return "client_rtsp_publish";
    }
    else if (session_type_ == STREAM_SESSION_TYPE::CLIENT_WEBRTC_PULL) {
        return "client_webrtc_pull";
    }
    else if (session_type_ == STREAM_SESSION_TYPE::CLIENT_WEBRTC_PUBLISH) {
        return "client_webrtc_publish";
    }
    return "unknown";
}

void STREAM_SESSION::setState(SESSION_STATE new_state)
{
    state_ = new_state;
}

bool STREAM_SESSION::shouldReconnect() const
{
    if (max_retries_ > 0 && retry_count_ >= max_retries_) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_reconnect_time_ < std::chrono::milliseconds(retry_interval_)) {
        return false;
    }

    return true;
}

void STREAM_SESSION::doReconnect()
{
    retry_count_++;
    last_reconnect_time_ = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_));
}

void STREAM_SESSION::copyStreamInfoFrom(const STREAM_SESSION& other)
{
    control_url = other.control_url;
    codec = other.codec;
    payload_type = other.payload_type;
    clock_rate = other.clock_rate;
    fmtp = other.fmtp;
    sps = other.sps;
    pps = other.pps;
    sdp = other.sdp;
    video_ssrc = other.video_ssrc;
}

void STREAM_SESSION::copyClientSessionFrom(const STREAM_SESSION& other)
{
    copyStreamInfoFrom(other);

    session_type_ = other.session_type_;
    is_webrtc = other.is_webrtc;

    transport_mode = other.transport_mode;
    transport = other.transport;
    remote_host = other.remote_host;
    client_port = other.client_port;
    server_port = other.server_port;
    client_rtp_port = other.client_rtp_port;
    client_rtcp_port = other.client_rtcp_port;
    server_rtp_port = other.server_rtp_port;
    server_rtcp_port = other.server_rtcp_port;

    rtp_socket = other.rtp_socket;
    rtcp_socket = other.rtcp_socket;
    tcp_socket = other.tcp_socket;
    interleaved_rtp = other.interleaved_rtp;
    interleaved_rtcp = other.interleaved_rtcp;
}
