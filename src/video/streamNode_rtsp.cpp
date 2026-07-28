// ============================================================================
// streamNode_rtsp.cpp - RTSP 控制层
// 包含：SDP 解析/生成、控制流（controlThread）
// ============================================================================

#include "streamNode.h"
#include "streamServer.h"
#include <logger.h>
#include <sstream>
#include <algorithm>
#include <cstring>

// ============================================================================
// 控制流
// ============================================================================

void StreamNode::controlThread() {
    while (running_ && !stopping_) {
        // 启动拉流与推流
        if (isPulling_ == false) {
            if (session_origin_pull_.open(*this)) {
                rtp_handle_thread_ = std::thread(&StreamNode::OriginRtpHandleThread,this);
                rtp_handle_thread_.detach();
                open_time_ = std::chrono::system_clock::now();
                isPulling_ = true;
            }
            else {
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.reconnect_count++;
                }
                session_origin_pull_.setState(SESSION_STATE::SESSION_RECONNECTING);
                session_origin_pull_.doReconnect();
                session_origin_pull_.close();
            }
        }
 
        if (isPulling_ == true && isPushing_ == false && session_relay_push_.server_url_ != "") {
            if (session_relay_push_.open(*this)) {
                isPushing_ = true;
            }
            else {
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.reconnect_count++;
                }
                session_relay_push_.setState(SESSION_STATE::SESSION_RECONNECTING);
                session_relay_push_.doReconnect();
                session_relay_push_.close();
            }
        }

        // 心跳保活
        if (isPulling_) {
            if (session_origin_pull_.conn_ && !session_origin_pull_.rtsp_session_id_.empty()) {
                if (!session_origin_pull_.rtspGetParameter(*this, session_origin_pull_.server_url_, session_origin_pull_.rtsp_session_id_)) {
                    session_origin_pull_.setState(SESSION_STATE::SESSION_ERROR);
                    isPulling_ = false;
                    session_origin_pull_.close();
                    session_relay_push_.close();
                }
            }
        }

        if (isPushing_) {
            if (session_relay_push_.conn_ && !session_relay_push_.rtsp_session_id_.empty()) {
                if (!session_relay_push_.rtspGetParameter(*this, session_relay_push_.server_url_, session_relay_push_.rtsp_session_id_)) {
                    session_relay_push_.setState(SESSION_STATE::SESSION_ERROR);
                    isPushing_ = false;
                    session_origin_pull_.close();
                    session_relay_push_.close();
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}



// ============================================================================
// SDP处理函数
// ============================================================================

// 前向声明 base64Decode（定义在 streamNode.cpp 中）
extern std::vector<uint8_t> base64Decode(const std::string& input);

bool StreamNode::parseSDP(const std::string & sdp, STREAM_SESSION & video_info, STREAM_SESSION & audio_info) {
    std::istringstream ss(sdp);
    std::string line;
    STREAM_SESSION* current_info = nullptr;

    while (std::getline(ss, line)) {
        if (line.length() < 2 || line[1] != '=') continue;

        char type = line[0];
        std::string value = line.substr(2, line.length() - 3);

        switch (type) {
        case 'm': {
            std::istringstream mstream(value);
            std::string media_type, port_str, proto, fmt;
            mstream >> media_type >> port_str >> proto >> fmt;

            if (media_type == "video") {
                current_info = &video_info;
                video_info.payload_type = std::stoi(fmt);
            }
            else if (media_type == "audio") {
                current_info = &audio_info;
                audio_info.payload_type = std::stoi(fmt);
            }
            else {
                current_info = nullptr;
            }
            break;
        }

        case 'a':
            if (!current_info) break;

            if (value.find("rtpmap:") == 0) {
                size_t colon = value.find(':');
                size_t space = value.find(' ', colon);
                size_t slash = value.find('/', space);

                if (slash != std::string::npos) {
                    std::string codec_str = value.substr(space + 1, slash - space - 1);
                    current_info->codec = codec_str;

                    std::string rate_str = value.substr(slash + 1);
                    size_t second_slash = rate_str.find('/');
                    if (second_slash != std::string::npos) {
                        rate_str = rate_str.substr(0, second_slash);
                    }
                    current_info->clock_rate = std::stoi(rate_str);
                }
            }
            else if (value.find("fmtp:") == 0) {
                size_t fmtp_start = value.find(' ');
                if (fmtp_start != std::string::npos) {
                    current_info->fmtp = value.substr(fmtp_start + 1);

                    // 解析 sprop-parameter-sets（如果存在），格式类似：sprop-parameter-sets=Z0IAH5WoFAFuQA==,aM48gA==
                    size_t sprop_pos = current_info->fmtp.find("sprop-parameter-sets=");
                    if (sprop_pos != std::string::npos) {
                        size_t start = sprop_pos + strlen("sprop-parameter-sets=");
                        size_t end = current_info->fmtp.find(';', start);
                        std::string sprop = (end == std::string::npos) ? current_info->fmtp.substr(start) : current_info->fmtp.substr(start, end - start);

                        // 去掉可能的空格
                        while (!sprop.empty() && sprop.front() == ' ') sprop.erase(sprop.begin());

                        // sprop 通常为 base64_sps,base64_pps
                        size_t comma = sprop.find(',');
                        if (comma != std::string::npos) {
                            std::string sps_b64 = sprop.substr(0, comma);
                            std::string pps_b64 = sprop.substr(comma + 1);
                            auto sps_dec = base64Decode(sps_b64);
                            auto pps_dec = base64Decode(pps_b64);
                            if (!sps_dec.empty()) current_info->sps = std::move(sps_dec);
                            if (!pps_dec.empty()) current_info->pps = std::move(pps_dec);
                        }
                    }
                }
            }
            else if (value.find("control:") == 0) {
                current_info->control_url = value.substr(8);
            }
            break;
        }
    }

    return true;
}

std::string StreamNode::generateSDP(const STREAM_SESSION & video_info, const STREAM_SESSION & audio_info) {
    std::stringstream sdp;

    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 0.0.0.0\r\n"
        << "s=RTSP Relay Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n"
        << "a=control:*\r\n";

    if (video_info.payload_type > 0) {
        sdp << "m=video 0 RTP/AVP " << video_info.payload_type << "\r\n"
            << "a=rtpmap:" << video_info.payload_type << " "
            << video_info.codec << "/" << video_info.clock_rate << "\r\n";

        if (!video_info.fmtp.empty()) {
            sdp << "a=fmtp:" << video_info.payload_type << " " << video_info.fmtp << "\r\n";
        }

        sdp << "a=control:" << video_info.control_url << "\r\n";
    }

    return sdp.str();
}
