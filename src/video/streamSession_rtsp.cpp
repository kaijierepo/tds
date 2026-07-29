// ============================================================================
// streamSession_rtsp.cpp — STREAM_SESSION 的 RTSP 协议方法实现
// 包含：open/close、RTSP 信令（DESCRIBE/SETUP/PLAY/TEARDOWN/ANNOUNCE/RECORD/
//       GET_PARAMETER）、认证、SDP 解析/生成
// ============================================================================

#include "streamSession.h"
#include "streamNode.h"
#include <logger.h>
#include <sstream>
#include <algorithm>
#include <cstring>

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

bool STREAM_SESSION::open(const STREAM_SESSION* origin_session)
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
        if (!rtspDescribeReq(server_url_, sdp, rtsp_session_id_)) {
            setState(SESSION_STATE::SESSION_ERROR);
            return false;
        }

        // 解析SDP
        STREAM_SESSION audio_scratch;
        if (!parseSDP(sdp, audio_scratch)) {
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
            audio_scratch.control_url.c_str());

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
        if (!rtspSetupReq(server_url_, rtsp_session_id_, *this)) {
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
        if (!rtspPlayReq(server_url_, rtsp_session_id_)) {
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

        // 从源 SESSION 复制视频流元数据，不碰 socket/连接/认证等 relay push 自身配置
        {
            codec = origin_session->codec;
            payload_type = origin_session->payload_type;
            clock_rate = origin_session->clock_rate;
            fmtp = origin_session->fmtp;
            sps = origin_session->sps;
            pps = origin_session->pps;
            sdp = origin_session->sdp;
            video_ssrc = origin_session->video_ssrc;
        }
        {
            std::string track_control = "trackID=0";
            if (!origin_session->control_url.empty()) {
                std::string src = origin_session->control_url;
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

        std::string target_sdp = generateSDP();

        // 发送ANNOUNCE到目标
        if (!rtspAnnounceReq(server_url_, target_sdp, rtsp_session_id_)) {
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
        if (!rtspSetupReq(server_url_, rtsp_session_id_, *this)) {
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
        if (!rtspRecordReq(server_url_, rtsp_session_id_)) {
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

bool STREAM_SESSION::rtspDescribeReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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

bool STREAM_SESSION::rtspSetupReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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
		if (transport_mode == TransportMode::UDP) {
			request << "Transport: RTP/AVP/UDP;unicast;mode=record;"
				<< "client_port=" << stream.client_port;
			if (udp_ttl != 64) {
				request << ";ttl=" << udp_ttl;
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
		if (transport_mode == TransportMode::UDP) {
			request << "Transport: RTP/AVP/UDP;unicast;"
				<< "client_port=" << stream.client_port;
			if (udp_ttl != 64) {
				request << ";ttl=" << udp_ttl;
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

bool STREAM_SESSION::rtspPlayReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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

bool STREAM_SESSION::rtspTeardownReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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

bool STREAM_SESSION::rtspAnnounceReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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

bool STREAM_SESSION::rtspRecordReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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

bool STREAM_SESSION::rtspGetParameterReq(const std::string& url,
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

	// 使用自身认证信息
	STREAM_SESSION* auth_session = this;

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

// ============================================================================
// SDP处理函数
// ============================================================================

bool STREAM_SESSION::parseSDP(const std::string& sdp, STREAM_SESSION& audio_info) {
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
                current_info = this;
                payload_type = std::stoi(fmt);
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

std::string STREAM_SESSION::generateSDP() const {
    std::stringstream sdp;

    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 0.0.0.0\r\n"
        << "s=RTSP Relay Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n"
        << "a=control:*\r\n";

    if (payload_type > 0) {
        sdp << "m=video 0 RTP/AVP " << payload_type << "\r\n"
            << "a=rtpmap:" << payload_type << " "
            << codec << "/" << clock_rate << "\r\n";

        if (!fmtp.empty()) {
            sdp << "a=fmtp:" << payload_type << " " << fmtp << "\r\n";
        }

        sdp << "a=control:" << control_url << "\r\n";
    }

    return sdp.str();
}
