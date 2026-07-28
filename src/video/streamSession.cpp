// ============================================================================
// streamSession.cpp — STREAM_SESSION 方法实现
// ============================================================================

#include "streamSession.h"
#include <logger.h>
#include <sstream>

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

std::string STREAM_SESSION::calcBasicAuth() const
{
    std::string credentials = server_username_ + ":" + server_password_;
    return "Basic " + base64Encode(credentials);
}

std::string STREAM_SESSION::calcDigestAuth(const std::string& method, const std::string& uri) const
{
    std::string ha1_input = server_username_ + ":" + server_auth_realm_ + ":" + server_password_;
    std::string ha1 = md5Hex(ha1_input);

    std::string ha2_input = method + ":" + uri;
    std::string ha2 = md5Hex(ha2_input);

    std::string response_input = ha1 + ":" + server_auth_nonce_ + ":" + ha2;
    return md5Hex(response_input);
}

void STREAM_SESSION::buildAuthHeader(const std::string& method, const std::string& uri)
{
    if (!hasAuthCredentials()) {
        server_auth_header_.clear();
        return;
    }

    if (server_auth_use_digest_) {
        std::string response = calcDigestAuth(method, uri);

        std::ostringstream auth_header;
        auth_header << "Authorization: Digest "
            << "username=\"" << server_username_ << "\", "
            << "realm=\"" << server_auth_realm_ << "\", "
            << "nonce=\"" << server_auth_nonce_ << "\", "
            << "uri=\"" << uri << "\", "
            << "response=\"" << response << "\"";

        if (!server_auth_algorithm_.empty()) {
            auth_header << ", algorithm=\"" << server_auth_algorithm_ << "\"";
        }

        server_auth_header_ = auth_header.str();
    }
    else {
        server_auth_header_ = "Authorization: " + calcBasicAuth();
    }
}

void STREAM_SESSION::close()
{
    // 发送 RTSP TEARDOWN
    if (conn_ && !rtsp_session_id_.empty())
    {
        buildAuthHeader("TEARDOWN", server_url_);

        std::ostringstream request;
        request << "TEARDOWN " << server_url_ << " RTSP/1.0\r\n"
            << "CSeq: " << conn_->nextCSeq() << "\r\n";
        if (!server_auth_header_.empty()) {
            request << server_auth_header_ << "\r\n";
        }
        request << "Session: " << rtsp_session_id_ << "\r\n"
            << "\r\n";

        const std::string req = request.str();
        int sent = conn_->send(req.c_str(), req.size());
        if (sent == static_cast<int>(req.size())) {
            std::string response;
            conn_->receiveHttpResp(response, 5000);
        }
    }

    // 断开 TCP 连接
    if (conn_) {
        conn_->disconnect();
    }

    // 清除会话状态
    rtsp_session_id_.clear();
    client_rtp_port = 0;
    client_rtcp_port = 0;
    clearAuthRuntime();
    closeSockets();
}
