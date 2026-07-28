// ============================================================================
// streamSession.cpp — STREAM_SESSION 方法实现
// ============================================================================

#include "streamSession.h"
#include "streamNode.h"
#include <logger.h>
#include <sstream>
#include <algorithm>

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

bool STREAM_SESSION::open(StreamNode& sn)
{
    if (session_type_ == STREAM_SESSION_TYPE::ORIGIN_PULL) {
        // ================================================================
        // ORIGIN_PULL：从源 RTSP 服务器拉流（DESCRIBE → SETUP → PLAY）
        // ================================================================
        setState(SESSION_STATE::SESSION_CONNECTING);

        // 解析源URL
        StreamNode::URLComponents src_url;
        if (!StreamNode::URLComponents::parse(server_url_, src_url)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 连接到源服务器
        conn_ = std::make_unique<Connection>();
        if (!conn_->connect(src_url.host, src_url.port)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        LOG("[StreamNode]tag=%s,Connect to source success,%s", tag_.c_str(), (src_url.host + ":" + std::to_string(src_url.port)).c_str());

        // 发送DESCRIBE
        std::string sdp;
        if (!rtspDescribe(sn, server_url_, sdp, rtsp_session_id_)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 解析SDP
        if (!sn.parseSDP(sdp, *this, sn.pull_audio_session_)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 调试：打印收到的 SDP 内容
        std::string sdp_for_log = sdp;
        std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\r', '~');
        std::replace(sdp_for_log.begin(), sdp_for_log.end(), '\n', '~');
        LOG("[StreamNode]tag=%s, sdp received: %s,streamInfo:%s,audioControl:%s",
            tag_.c_str(),
            sdp_for_log.c_str(),
            control_url.c_str(),
            sn.pull_audio_session_.control_url.c_str());

        setState(SESSION_STATE::SESSION_HANDSHAKING);

        // 清理之前的UDP sockets
        closeSockets();

        // 根据拉流模式决定是否创建UDP socket
        bool pullUseUDP = (transport_mode == TransportMode::UDP);

        if (pullUseUDP) {
            // 创建专用的UDP socket用于拉流（接收RTP）
            if (!createUDPConsecutiveSockets(false)) {
                logError("Failed to create UDP pull socket");
                pullUseUDP = false;
            }
        }

        if (pullUseUDP) {
            client_port = std::to_string(client_rtp_port) + "-" + std::to_string(client_rtcp_port);
        }
        else {
            client_port = "0-0";  // TCP模式不需要client_port
        }

        LOG("[StreamNode]tag=%s,SETUP,mode=%s,local rtp/rtcp port=%s",
            tag_.c_str(),
            pullUseUDP ? "udp" : "tcp",
            client_port.c_str()
        );

        // 发送SETUP到源
        if (!rtspSetup(sn, server_url_, rtsp_session_id_, *this)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 解析传输信息
        std::istringstream transport_stream(transport);
        std::string token;
        while (std::getline(transport_stream, token, ';')) {
            if (token.find("server_port=") != std::string::npos) {
                size_t pos = token.find('=');
                server_port = token.substr(pos + 1);

                // 解析RTP端口
                size_t dash = server_port.find('-');
                if (dash != std::string::npos) {
                    server_rtp_port = std::stoi(server_port.substr(0, dash));
                }
            }
            else if (token.find("source=") != std::string::npos) {
                size_t pos = token.find('=');
                remote_host = token.substr(pos + 1);
            }
        }


        LOG("[StreamNode]tag=%s,SETUP success,mode=%s,server port=%s",
            tag_.c_str(),
            pullUseUDP ? "udp" : "tcp",
            server_port.c_str()
        );

        // 发送PLAY
        if (!rtspPlay(sn, server_url_, rtsp_session_id_)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        return true;
    }
    else if (session_type_ == STREAM_SESSION_TYPE::RELAY_PUSH) {
        // ================================================================
        // RELAY_PUSH：推流到目标 RTSP 服务器（ANNOUNCE → SETUP → RECORD）
        // ================================================================
        state_ = SESSION_STATE::SESSION_CONNECTING;

        // 连接到目标服务器
        StreamNode::URLComponents relay_push_url;
        if (!StreamNode::URLComponents::parse(server_url_, relay_push_url)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        conn_ = std::make_unique<Connection>();
        if (!conn_->connect(relay_push_url.host, relay_push_url.port)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 从源 SESSION 复制视频流信息，保留 relay push 自身状态
        {
            SESSION_STATE saved_state = state_;
            STREAM_SESSION_TYPE saved_type = session_type_;
            *this = sn.session_origin_pull_;
            state_ = saved_state;
            session_type_ = saved_type;
        }
        {
            std::string track_control = "trackID=0";
            if (!sn.session_origin_pull_.control_url.empty()) {
                std::string src = sn.session_origin_pull_.control_url;
                size_t pos = src.find_last_of('/');
                track_control = (pos == std::string::npos) ? src : src.substr(pos + 1);
                if (track_control.empty() || track_control == "*" ||
                    track_control.rfind("rtsp://", 0) == 0 ||
                    track_control.rfind("rtsps://", 0) == 0) {
                    track_control = "trackID=0";
                }
            }
            control_url = track_control;
        }

        std::string target_sdp = sn.generateSDP(*this, STREAM_SESSION());

        // 发送ANNOUNCE到目标
        if (!rtspAnnounce(sn, server_url_, target_sdp, rtsp_session_id_)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 如果使用 UDP 推流，应先创建并绑定本地 RTP/RTCP sockets，
        // 并将 client_port 写入 target_video_info_，再发送 SETUP。
        if (transport_mode == TransportMode::UDP) {
            if (!createUDPConsecutiveSockets(false)) {
                logError("Failed to create UDP push socket, falling back to TCP");
                transport_mode = TransportMode::TCP;
            }
            else {
                // 填写 client_port，格式 "RTP-RTCP"
                client_port = std::to_string(client_rtp_port) + "-" + std::to_string(client_rtcp_port);
                logInfo("Push stream: Using UDP mode, client_port=" + client_port);
            }
        }

        if (transport_mode == TransportMode::TCP) {
            logInfo("Push stream: Using TCP mode (RTP over RTSP)");
        }

        // 发送SETUP到目标
        if (!rtspSetup(sn, server_url_, rtsp_session_id_, *this)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 解析目标服务器端口
        {
            logInfo("Target SETUP Transport: " + transport);
            const std::string key = "server_port=";
            size_t pos = transport.find(key);
            if (pos != std::string::npos) {
                pos += key.size();
                while (pos < transport.size() &&
                    (transport[pos] == ' ' || transport[pos] == '\t')) {
                    ++pos;
                }
                int port = 0;
                while (pos < transport.size() &&
                    transport[pos] >= '0' && transport[pos] <= '9') {
                    port = port * 10 + (transport[pos] - '0');
                    ++pos;
                }
                if (port > 0 && port <= 65535) {
                    server_rtp_port = port;
                }
            }
        }

        logInfo("Target RTP port: " + std::to_string(server_rtp_port));
        if (server_rtp_port == 0) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }


        // 发送RECORD到目标
        if (!rtspRecord(sn, server_url_, rtsp_session_id_)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        LOG("[keyinfo][StreamNode]tag=%s,stream forward success,pushToUrl:%s", tag_.c_str(), server_url_.c_str());
        state_ = SESSION_STATE::SESSION_STREAMING;
        open_time_ = std::chrono::system_clock::now();

        return true;
    }

    return false;
}

// =============================================================================
// RTSP 辅助函数
// =============================================================================

std::string STREAM_SESSION::extractSessionID(const std::string& response) {
	size_t pos = response.find("Session: ");
	if (pos == std::string::npos) return "";

	size_t end = response.find("\r\n", pos);
	std::string session_line = response.substr(pos, end - pos);

	std::string session = session_line.substr(9);

	size_t timeout_pos = session.find(';');
	if (timeout_pos != std::string::npos) {
		session = session.substr(0, timeout_pos);
	}

	return session;
}

std::string STREAM_SESSION::extractTransport(const std::string& response) {
	size_t pos = response.find("Transport: ");
	if (pos == std::string::npos) return "";

	size_t end = response.find("\r\n", pos);
	return response.substr(pos + 11, end - pos - 11);
}

bool STREAM_SESSION::parseWWWAuthenticate(const std::string& response, STREAM_SESSION& session) {
	// 查找WWW-Authenticate头
	size_t www_auth_pos = response.find("WWW-Authenticate: ");
	if (www_auth_pos == std::string::npos) {
		return false;
	}

	size_t line_end = response.find("\r\n", www_auth_pos);
	std::string auth_line = response.substr(www_auth_pos + 18, line_end - www_auth_pos - 18);

	logVerbose("WWW-Authenticate: " + auth_line);

	// 检查认证类型
	if (auth_line.find("Digest") == 0) {
		session.server_auth_use_digest_ = true;

		// 解析Digest参数
		size_t realm_pos = auth_line.find("realm=\"");
		if (realm_pos != std::string::npos) {
			size_t realm_end = auth_line.find("\"", realm_pos + 7);
			if (realm_end != std::string::npos) {
				session.server_auth_realm_ = auth_line.substr(realm_pos + 7, realm_end - realm_pos - 7);
			}
		}

		size_t nonce_pos = auth_line.find("nonce=\"");
		if (nonce_pos != std::string::npos) {
			size_t nonce_end = auth_line.find("\"", nonce_pos + 7);
			if (nonce_end != std::string::npos) {
				session.server_auth_nonce_ = auth_line.substr(nonce_pos + 7, nonce_end - nonce_pos - 7);
			}
		}

		size_t algorithm_pos = auth_line.find("algorithm=\"");
		if (algorithm_pos != std::string::npos) {
			size_t algorithm_end = auth_line.find("\"", algorithm_pos + 11);
			if (algorithm_end != std::string::npos) {
				session.server_auth_algorithm_ = auth_line.substr(algorithm_pos + 11, algorithm_end - algorithm_pos - 11);
			}
		}
		else {
			session.server_auth_algorithm_ = "MD5";
		}

		return true;
	}
	else if (auth_line.find("Basic") == 0) {
		session.server_auth_use_digest_ = false;

		size_t realm_pos = auth_line.find("realm=\"");
		if (realm_pos != std::string::npos) {
			size_t realm_end = auth_line.find("\"", realm_pos + 7);
			if (realm_end != std::string::npos) {
				session.server_auth_realm_ = auth_line.substr(realm_pos + 7, realm_end - realm_pos - 7);
			}
		}

		return true;
	}

	return false;
}

// =============================================================================
// RTSP 控制方法
// =============================================================================

bool STREAM_SESSION::rtspDescribe(StreamNode& sn, const std::string& url,
	std::string& sdp, std::string& session) {
	StreamNode::URLComponents url_components;
	if (!StreamNode::URLComponents::parse(url, url_components)) {
		logError("DESCRIBE failed: invalid url format: " + url);
		return false;
	}

	std::string host_header = url_components.host;
	if (host_header.find(':') != std::string::npos) {
		host_header = "[" + host_header + "]";
	}
	host_header += ":" + std::to_string(url_components.port);

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_origin_pull_.server_url_ || url.find(sn.session_origin_pull_.server_url_) == 0) {
		auth_session = &sn.session_origin_pull_;
	}
	else {
		auth_session = &sn.session_relay_push_;
	}

	auto do_describe = [&](const std::string& request_uri, bool include_host,
		bool use_auth, std::string& response) -> bool {
		std::stringstream request;
		request << "DESCRIBE " << request_uri << " RTSP/1.0\r\n"
			<< "CSeq: " << conn_->nextCSeq() << "\r\n"
			<< "User-Agent: StreamNode/1.0\r\n";

		if (include_host) {
			request << "Host: " << host_header << "\r\n";
		}

		// 添加认证头
		if (use_auth && auth_session && auth_session->hasAuthCredentials()) {
			auth_session->buildAuthHeader("DESCRIBE", request_uri);
			if (!auth_session->server_auth_header_.empty()) {
				request << auth_session->server_auth_header_ << "\r\n";
			}
		}

		request << "Accept: application/sdp\r\n"
			<< "\r\n";

		const std::string req = request.str();
		logVerbose(">> DESCRIBE " + request_uri +
			(include_host ? "" : " (no Host)") +
			(use_auth ? " (with auth)" : ""));

		int sent = conn_->send(req.c_str(), req.size());
		if (sent != static_cast<int>(req.size())) {
			setState(SESSION_STATE::SESSION_ERROR);
			response.clear();
			return false;
		}

		response.clear();
		int rc = conn_->receiveHttpResp(response, 1000);
		if (rc <= 0) {
			setState(SESSION_STATE::SESSION_ERROR);
			response.clear();
			return false;
		}

		if (response.find("200 OK") != std::string::npos) {
			// 成功
			size_t sdp_start = response.find("\r\n\r\n");
			if (sdp_start != std::string::npos) {
				sdp = response.substr(sdp_start + 4);
			}
			else {
				sdp.clear();
			}
			// 注意：虽然 RTSP RFC 规定 Session 应该在 SETUP 响应中返回，
			// 但 ZLMediaKit 在 DESCRIBE 响应中也包含 Session。
			// 为了兼容 ZLM，我们需要从 DESCRIBE 响应中提取 Session。
			// 这样在后续的 SETUP 请求中可以带上 Session。
			std::string session_in_response = extractSessionID(response);
			if (!session_in_response.empty()) {
				session = session_in_response;
				logVerbose("Session from DESCRIBE: " + session);
			}
			return true;
		}
		else if (response.find("401 Unauthorized") != std::string::npos) {
			// 需要认证
			if (auth_session && auth_session->hasAuthCredentials()) {
				// 解析WWW-Authenticate头
				if (parseWWWAuthenticate(response, *auth_session)) {
					logInfo("Authentication required, retrying with credentials");
				}
				else {
					logError("Failed to parse WWW-Authenticate header");
				}
			}
			else {
				logError("Authentication required but no credentials provided");
			}
			return false;
		}
		else {
			LOG("[StreamNode]tag=%s,DESCRIBE failed:%s", tag_.c_str(), response.substr(0, 200).c_str());
			return false;
		}
	};

	// 尝试顺序：无认证 -> 带认证
	std::string response;

	// 第一次尝试：不带认证
	if (do_describe(url, true, false, response)) {
		return true;
	}

	// 第二次尝试：带认证（如果提供了凭据）
	if (auth_session && auth_session->hasAuthCredentials()) {
		if (do_describe(url, true, true, response)) {
			return true;
		}
	}

	// 如果上面失败，尝试不带Host头
	if (do_describe(url, false, false, response)) {
		return true;
	}

	// 如果提供了凭据，尝试不带Host头但带认证
	if (auth_session && auth_session->hasAuthCredentials()) {
		if (do_describe(url, false, true, response)) {
			return true;
		}
	}

	// 一些RTSP服务器期望路径格式的URI
	if (!url_components.path.empty() && url_components.path != url) {
		logVerbose("Retry DESCRIBE with path-only URI: " + url_components.path);

		if (do_describe(url_components.path, true, false, response)) {
			return true;
		}

		if (auth_session && auth_session->hasAuthCredentials()) {
			if (do_describe(url_components.path, true, true, response)) {
				return true;
			}
		}
	}

	return false;
}

bool STREAM_SESSION::rtspSetup(StreamNode& sn, const std::string& url,
	std::string& session, STREAM_SESSION& stream, bool record_mode) {
	StreamNode::URLComponents url_components;
	std::string host_header;
	if (StreamNode::URLComponents::parse(url, url_components)) {
		host_header = url_components.host;
		if (host_header.find(':') != std::string::npos) {
			host_header = "[" + host_header + "]";
		}
		host_header += ":" + std::to_string(url_components.port);
	}

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_origin_pull_.server_url_ || url.find(sn.session_origin_pull_.server_url_) == 0) {
		auth_session = &sn.session_origin_pull_;
	}
	else {
		auth_session = &sn.session_relay_push_;
	}

	// 根据 ZLM 的 SDP 格式，正确拼接 SETUP URL
	// ZLM SDP: a=control:* 表示 base URL, a=control:streamid=0 表示相对路径
	std::string setup_url = url;

	if (!stream.control_url.empty()) {
		if (stream.control_url.rfind("rtsp://", 0) == 0 ||
			stream.control_url.rfind("rtsps://", 0) == 0) {
			// 绝对 URL：直接使用
			setup_url = stream.control_url;
		}
		else if (stream.control_url.front() == '/') {
			// 以 / 开头的绝对路径：rtsp://host:port/path
			StreamNode::URLComponents src_url;
			if (StreamNode::URLComponents::parse(url, src_url)) {
				setup_url = src_url.protocol + "://" + src_url.host + ":" +
					std::to_string(src_url.port) + stream.control_url;
			}
		}
		else {
			// 相对路径（如 streamid=0）：拼接到原始 URL 后面
			// ZLM 格式：/stream/1 + streamid=0 = /stream/1/streamid=0
			if (!url.empty()) {
				if (url.back() == '*') {
					// a=control:* 表示用 base URL
					setup_url = url.substr(0, url.length() - 1) + stream.control_url;
				}
				else if (url.back() == '/') {
					setup_url = url + stream.control_url;
				}
				else {
					setup_url = url + "/" + stream.control_url;
				}
			}
			else {
				setup_url = stream.control_url;
			}
		}
	}

	std::stringstream request;
	request << "SETUP " << setup_url << " RTSP/1.0\r\n"
		<< "CSeq: " << conn_->nextCSeq() << "\r\n"
		<< "User-Agent: StreamNode/1.0\r\n";

	(void)host_header;  // 抑制未使用变量警告

	if (!session.empty()) {
		request << "Session: " << session << "\r\n";
	}

	// 添加认证头
	if (auth_session && auth_session->hasAuthCredentials() && !auth_session->server_auth_header_.empty()) {
		auth_session->buildAuthHeader("SETUP", setup_url);
		request << auth_session->server_auth_header_ << "\r\n";
	}

	// 传输模式
	if (record_mode) {
		// 推流（发送）：服务端接收
		if (sn.session_relay_push_.transport_mode == TransportMode::UDP) {
			request << "Transport: RTP/AVP/UDP;unicast;mode=record;"
				<< "client_port=" << stream.client_port;
			if (sn.session_relay_push_.udp_ttl != 64) {
				request << ";ttl=" << sn.session_relay_push_.udp_ttl;
			}
			request << "\r\n";
		}
		else {
			// TCP推流
			request << "Transport: RTP/AVP/TCP;unicast;mode=record;interleaved=0-1\r\n";
		}
	}
	else {
		// 拉流（接收）：客户端接收
		if (sn.session_origin_pull_.transport_mode == TransportMode::UDP) {
			request << "Transport: RTP/AVP/UDP;unicast;"
				<< "client_port=" << stream.client_port;
			if (sn.session_origin_pull_.udp_ttl != 64) {
				request << ";ttl=" << sn.session_origin_pull_.udp_ttl;
			}
			request << "\r\n";
		}
		else {
			// TCP拉流 - 尝试多种格式
			// 格式1: 标准 RFC 格式
			request << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
		}
	}

	request << "\r\n";

	const std::string req = request.str();
	// 打印完整请求内容，便于调试
	std::string req_for_log = req;
	std::replace(req_for_log.begin(), req_for_log.end(), '\r', '~');
	std::replace(req_for_log.begin(), req_for_log.end(), '\n', '~');
	logVerbose(">> SETUP REQUEST:\n" + req_for_log);

	int sent = conn_->send(req.c_str(), req.size());
	if (sent != static_cast<int>(req.size())) {
		logError("SETUP send failed: sent=" + std::to_string(sent) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	std::string response;
	// 增加超时时间，因为 ZLM 可能延迟发送 SETUP 响应
	int rc = conn_->receiveHttpResp(response, 10000);
	if (rc <= 0) {
		// 调试：打印收到的原始数据
		if (!response.empty()) {
			std::string resp_for_log = response;
			std::replace(resp_for_log.begin(), resp_for_log.end(), '\r', '~');
			std::replace(resp_for_log.begin(), resp_for_log.end(), '\n', '~');
			logVerbose("<< SETUP PARTIAL DATA: " + resp_for_log);
		}
		logError("SETUP recv failed: rc=" + std::to_string(rc) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	// 调试：打印收到的响应
	{
		std::string resp_for_log = response;
		std::replace(resp_for_log.begin(), resp_for_log.end(), '\r', '~');
		std::replace(resp_for_log.begin(), resp_for_log.end(), '\n', '~');
		logVerbose("<< SETUP RESPONSE (rc=" + std::to_string(rc) + "): " + resp_for_log);
	}

	if (response.find("200 OK") == std::string::npos) {
		if (response.find("401 Unauthorized") != std::string::npos) {
			logError("SETUP authentication failed");
		}
		else {
			logError("SETUP failed: " + response.substr(0, 200));
		}
		return false;
	}

	stream.transport = extractTransport(response);

	std::string new_session = extractSessionID(response);
	if (!new_session.empty() && session.empty()) {
		session = new_session;
	}

	return true;
}

bool STREAM_SESSION::rtspPlay(StreamNode& sn, const std::string& url,
	const std::string& session) {
	StreamNode::URLComponents url_components;
	std::string host_header;
	if (StreamNode::URLComponents::parse(url, url_components)) {
		host_header = url_components.host;
		if (host_header.find(':') != std::string::npos) {
			host_header = "[" + host_header + "]";
		}
		host_header += ":" + std::to_string(url_components.port);
	}

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_origin_pull_.server_url_ || url.find(sn.session_origin_pull_.server_url_) == 0) {
		auth_session = &sn.session_origin_pull_;
	}
	else {
		auth_session = &sn.session_relay_push_;
	}

	std::stringstream request;
	request << "PLAY " << url << " RTSP/1.0\r\n"
		<< "CSeq: " << conn_->nextCSeq() << "\r\n"
		<< "User-Agent: StreamNode/1.0\r\n"
		<< (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

	// 添加认证头
	if (auth_session && auth_session->hasAuthCredentials() && !auth_session->server_auth_header_.empty()) {
		auth_session->buildAuthHeader("PLAY", url);
		request << auth_session->server_auth_header_ << "\r\n";
	}

	request << "Session: " << session << "\r\n"
		<< "Range: npt=0.000-\r\n"
		<< "\r\n";

	const std::string req = request.str();
	logVerbose(">> PLAY " + url);

	int sent = conn_->send(req.c_str(), req.size());
	if (sent != static_cast<int>(req.size())) {
		logError("PLAY send failed: sent=" + std::to_string(sent) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	std::string response;
	int rc = conn_->receiveHttpResp(response, 5000);
	if (rc <= 0) {
		logError("PLAY recv failed: rc=" + std::to_string(rc) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	if (response.find("200 OK") == std::string::npos) {
		logError("PLAY failed: " + response.substr(0, 200));
		return false;
	}

	return true;
}

bool STREAM_SESSION::rtspTeardown(StreamNode& sn, const std::string& url,
	const std::string& session) {
	StreamNode::URLComponents url_components;
	std::string host_header;
	if (StreamNode::URLComponents::parse(url, url_components)) {
		host_header = url_components.host;
		if (host_header.find(':') != std::string::npos) {
			host_header = "[" + host_header + "]";
		}
		host_header += ":" + std::to_string(url_components.port);
	}

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_origin_pull_.server_url_ || url.find(sn.session_origin_pull_.server_url_) == 0) {
		auth_session = &sn.session_origin_pull_;
	}
	else {
		auth_session = &sn.session_relay_push_;
	}

	std::stringstream request;
	request << "TEARDOWN " << url << " RTSP/1.0\r\n"
		<< "CSeq: " << conn_->nextCSeq() << "\r\n"
		<< "User-Agent: StreamNode/1.0\r\n"
		<< (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

	// 添加认证头
	if (auth_session && auth_session->hasAuthCredentials() && !auth_session->server_auth_header_.empty()) {
		auth_session->buildAuthHeader("TEARDOWN", url);
		request << auth_session->server_auth_header_ << "\r\n";
	}

	request << "Session: " << session << "\r\n"
		<< "\r\n";

	const std::string req = request.str();
	logVerbose(">> TEARDOWN " + url);

	int sent = conn_->send(req.c_str(), req.size());
	if (sent != static_cast<int>(req.size())) {
		logError("TEARDOWN send failed: sent=" + std::to_string(sent) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	std::string response;
	conn_->receiveHttpResp(response, 5000);

	return true;
}

bool STREAM_SESSION::rtspAnnounce(StreamNode& sn, const std::string& url,
	const std::string& sdp, std::string& session) {
	StreamNode::URLComponents url_components;
	std::string host_header;
	if (StreamNode::URLComponents::parse(url, url_components)) {
		host_header = url_components.host;
		if (host_header.find(':') != std::string::npos) {
			host_header = "[" + host_header + "]";
		}
		host_header += ":" + std::to_string(url_components.port);
	}

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_relay_push_.server_url_ || url.find(sn.session_relay_push_.server_url_) == 0) {
		auth_session = &sn.session_relay_push_;
	}

	std::stringstream request;
	request << "ANNOUNCE " << url << " RTSP/1.0\r\n"
		<< "CSeq: " << conn_->nextCSeq() << "\r\n"
		<< "User-Agent: StreamNode/1.0\r\n"
		<< (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

	// 添加认证头
	if (auth_session && auth_session->hasAuthCredentials() && !auth_session->server_auth_header_.empty()) {
		auth_session->buildAuthHeader("ANNOUNCE", url);
		request << auth_session->server_auth_header_ << "\r\n";
	}

	request << "Content-Type: application/sdp\r\n"
		<< "Content-Length: " << sdp.size() << "\r\n"
		<< "\r\n"
		<< sdp;

	const std::string req = request.str();
	logVerbose(">> ANNOUNCE " + url);

	int sent = conn_->send(req.c_str(), req.size());
	if (sent != static_cast<int>(req.size())) {
		logError("ANNOUNCE send failed: sent=" + std::to_string(sent) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	std::string response;
	int rc = conn_->receiveHttpResp(response, 5000);
	if (rc <= 0) {
		logError("ANNOUNCE recv failed: rc=" + std::to_string(rc) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	if (response.find("200 OK") == std::string::npos) {
		logError("ANNOUNCE failed: " + response.substr(0, 200));
		return false;
	}

	session = extractSessionID(response);

	return true;
}

bool STREAM_SESSION::rtspRecord(StreamNode& sn, const std::string& url,
	const std::string& session) {
	StreamNode::URLComponents url_components;
	std::string host_header;
	if (StreamNode::URLComponents::parse(url, url_components)) {
		host_header = url_components.host;
		if (host_header.find(':') != std::string::npos) {
			host_header = "[" + host_header + "]";
		}
		host_header += ":" + std::to_string(url_components.port);
	}

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_relay_push_.server_url_ || url.find(sn.session_relay_push_.server_url_) == 0) {
		auth_session = &sn.session_relay_push_;
	}

	std::stringstream request;
	request << "RECORD " << url << " RTSP/1.0\r\n"
		<< "CSeq: " << conn_->nextCSeq() << "\r\n"
		<< "User-Agent: StreamNode/1.0\r\n"
		<< (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

	// 添加认证头
	if (auth_session && auth_session->hasAuthCredentials() && !auth_session->server_auth_header_.empty()) {
		auth_session->buildAuthHeader("RECORD", url);
		request << auth_session->server_auth_header_ << "\r\n";
	}

	request << "Session: " << session << "\r\n"
		<< "Range: npt=0.000-\r\n"
		<< "\r\n";

	const std::string req = request.str();
	logVerbose(">> RECORD " + url);

	int sent = conn_->send(req.c_str(), req.size());
	if (sent != static_cast<int>(req.size())) {
		logError("RECORD send failed: sent=" + std::to_string(sent) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	std::string response;
	int rc = conn_->receiveHttpResp(response, 5000);
	if (rc <= 0) {
		logError("RECORD recv failed: rc=" + std::to_string(rc) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	if (response.find("200 OK") == std::string::npos) {
		logError("RECORD failed: " + response.substr(0, 200));
		return false;
	}

	return true;
}

bool STREAM_SESSION::rtspGetParameter(StreamNode& sn, const std::string& url,
	const std::string& session) {
	StreamNode::URLComponents url_components;
	std::string host_header;
	if (StreamNode::URLComponents::parse(url, url_components)) {
		host_header = url_components.host;
		if (host_header.find(':') != std::string::npos) {
			host_header = "[" + host_header + "]";
		}
		host_header += ":" + std::to_string(url_components.port);
	}

	// 确定认证信息
	STREAM_SESSION* auth_session = nullptr;
	if (url == sn.session_origin_pull_.server_url_ || url.find(sn.session_origin_pull_.server_url_) == 0) {
		auth_session = &sn.session_origin_pull_;
	}
	else {
		auth_session = &sn.session_relay_push_;
	}

	std::stringstream request;
	request << "GET_PARAMETER " << url << " RTSP/1.0\r\n"
		<< "CSeq: " << conn_->nextCSeq() << "\r\n"
		<< "User-Agent: StreamNode/1.0\r\n"
		<< (host_header.empty() ? "" : ("Host: " + host_header + "\r\n"));

	// 添加认证头
	if (auth_session && auth_session->hasAuthCredentials() && !auth_session->server_auth_header_.empty()) {
		auth_session->buildAuthHeader("GET_PARAMETER", url);
		request << auth_session->server_auth_header_ << "\r\n";
	}

	request << "Session: " << session << "\r\n"
		<< "Content-Length: 0\r\n"
		<< "\r\n";

	const std::string req = request.str();
	logVerbose(">> GET_PARAMETER " + url);

	int sent = conn_->send(req.c_str(), req.size());
	if (sent != static_cast<int>(req.size())) {
		logError("GET_PARAMETER send failed: sent=" + std::to_string(sent) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	std::string response;
	int rc = conn_->receiveHttpResp(response, 5000);
	if (rc <= 0) {
		logError("GET_PARAMETER recv failed: rc=" + std::to_string(rc) +
			" err=" + std::to_string(conn_->lastError()));
		return false;
	}

	if (response.find("200 OK") == std::string::npos) {
		logError("GET_PARAMETER failed: " + response.substr(0, 200));
		return false;
	}

	return true;
}
