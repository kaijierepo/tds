// ============================================================================
// streamSession.cpp — STREAM_SESSION 方法实现
// ============================================================================

#include "streamSession.h"
#include <logger.h>

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
