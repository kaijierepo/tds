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
