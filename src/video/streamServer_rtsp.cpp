#include "pch.h"
#include "streamServer.h"
#include "logger.h"
#include <sstream>
#include <random>
#include <cstring>

// ============================================================================
// RTSP 服务端实现
// 支持作为 RTSP 服务端接受客户端拉流
// ============================================================================

void StreamServer::startRtspServer(int port) {
	if (m_rtspRunning_) return;
	m_rtspRunning_ = true;
	m_rtspThread_ = std::thread(&StreamServer::rtspListenLoop, this, port);
	LOG("[RTSP-Server] Started on port %d", port);
}

void StreamServer::stopRtspServer() {
	if (!m_rtspRunning_) return;
	m_rtspRunning_ = false;

	// 关闭监听 socket 以唤醒 accept
	if (m_rtspListenSock_ != StreamNode::kInvalidSocket) {
#ifdef _WIN32
		closesocket(static_cast<SOCKET>(m_rtspListenSock_));
#else
		close(m_rtspListenSock_);
#endif
		m_rtspListenSock_ = StreamNode::kInvalidSocket;
	}

	if (m_rtspThread_.joinable()) {
		m_rtspThread_.join();
	}

	// 清理所有推流会话
	{
		std::lock_guard<std::mutex> lock(m_pushSessionsMutex_);
		for (auto& pair : m_pushSessions_) {
			if (pair.second) {
				pair.second->recv_running_ = false;
				if (pair.second->rtp_sock != StreamNode::kInvalidSocket) {
#ifdef _WIN32
					closesocket(static_cast<SOCKET>(pair.second->rtp_sock));
#else
					close(pair.second->rtp_sock);
#endif
				}
				if (pair.second->rtcp_sock != StreamNode::kInvalidSocket) {
#ifdef _WIN32
					closesocket(static_cast<SOCKET>(pair.second->rtcp_sock));
#else
					close(pair.second->rtcp_sock);
#endif
				}
			}
		}
		m_pushSessions_.clear();
	}

	LOG("[RTSP-Server] Stopped");
}

void StreamServer::rtspListenLoop(int port) {
	StreamNode::SocketHandle listenSock = static_cast<StreamNode::SocketHandle>(
		socket(AF_INET, SOCK_STREAM, 0));
	if (listenSock == StreamNode::kInvalidSocket) {
		LOG("[RTSP-Server] Failed to create listen socket");
		return;
	}

	// SO_REUSEADDR
	int reuse = 1;
#ifdef _WIN32
	setsockopt(static_cast<SOCKET>(listenSock), SOL_SOCKET, SO_REUSEADDR,
		(const char*)&reuse, sizeof(reuse));
#else
	setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	if (::bind(static_cast<SOCKET_TYPE>(listenSock), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		LOG("[RTSP-Server] bind failed on port %d", port);
#ifdef _WIN32
		closesocket(static_cast<SOCKET>(listenSock));
#else
		close(listenSock);
#endif
		return;
	}

	if (listen(static_cast<SOCKET_TYPE>(listenSock), 5) < 0) {
		LOG("[RTSP-Server] listen failed");
#ifdef _WIN32
		closesocket(static_cast<SOCKET>(listenSock));
#else
		close(listenSock);
#endif
		return;
	}

	m_rtspListenSock_ = listenSock;
	LOG("[RTSP-Server] Listening on 0.0.0.0:%d", port);

	// 设置 accept 超时为 1 秒，方便停止
#ifdef _WIN32
	int timeout_ms = 1000;
	setsockopt(static_cast<SOCKET>(listenSock), SOL_SOCKET, SO_RCVTIMEO,
		(const char*)&timeout_ms, sizeof(timeout_ms));
#else
	struct timeval tv = {1, 0};
	setsockopt(listenSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

	while (m_rtspRunning_) {
		struct sockaddr_in clientAddr;
		socklen_t clientLen = sizeof(clientAddr);
		StreamNode::SocketHandle clientSock = static_cast<StreamNode::SocketHandle>(
			accept(static_cast<SOCKET_TYPE>(listenSock),
				(struct sockaddr*)&clientAddr, &clientLen));

		if (clientSock == StreamNode::kInvalidSocket) {
			continue; // 超时或停止
		}

		char clientIp[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
		LOG("[RTSP-Server] Client connected: %s:%d", clientIp, ntohs(clientAddr.sin_port));

		// 每个客户端用一个独立线程处理
		std::string ipStr(clientIp);
		std::thread([this, clientSock, ipStr]() {
			handleRtspClient(clientSock, ipStr);
		}).detach();
	}

	// 清理
	if (m_rtspListenSock_ != StreamNode::kInvalidSocket) {
#ifdef _WIN32
		closesocket(static_cast<SOCKET>(m_rtspListenSock_));
#else
		close(m_rtspListenSock_);
#endif
		m_rtspListenSock_ = StreamNode::kInvalidSocket;
	}
}

void StreamServer::handleRtspClient(StreamNode::SocketHandle clientSock, const std::string& clientIp) {
	// 设置 socket 读超时 10 秒
#ifdef _WIN32
	int timeout_ms = 10000;
	setsockopt(static_cast<SOCKET>(clientSock), SOL_SOCKET, SO_RCVTIMEO,
		(const char*)&timeout_ms, sizeof(timeout_ms));
#else
	struct timeval tv = {10, 0};
	setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

	auto sendResponse = [&](int cseq, const std::string& body) {
		std::ostringstream resp;
		resp << body;
		std::string respStr = resp.str();
		send(static_cast<SOCKET_TYPE>(clientSock), respStr.c_str(),
			(int)respStr.size(), 0);
	};

	auto readRequest = [&](std::string& out) -> bool {
		char buf[4096];
		int len = recv(static_cast<SOCKET_TYPE>(clientSock), buf, sizeof(buf) - 1, 0);
		if (len > 0) {
			buf[len] = '\0';
			out = std::string(buf, len);
			return true;
		}
		// 超时不属于断连（TCP interleaved 模式下 send/recv 分属不同线程，recv 超时应重试）
		if (len < 0) {
#ifdef _WIN32
			if (WSAGetLastError() == WSAETIMEDOUT) {
				out.clear();
				return true;
			}
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				out.clear();
				return true;
			}
#endif
		}
		return false;
	};

	// 解析 RTSP 请求行
	auto parseRequestLine = [](const std::string& req, std::string& method,
		std::string& url, int& cseq) -> bool {
		size_t pos = req.find("\r\n");
		if (pos == std::string::npos) return false;
		std::string line = req.substr(0, pos);

		// method
		size_t sp1 = line.find(' ');
		if (sp1 == std::string::npos) return false;
		method = line.substr(0, sp1);

		// url
		size_t sp2 = line.find(' ', sp1 + 1);
		if (sp2 == std::string::npos) return false;
		url = line.substr(sp1 + 1, sp2 - sp1 - 1);

		// CSeq
		size_t cseqPos = req.find("CSeq:");
		if (cseqPos == std::string::npos) return false;
		cseqPos += 5;
		while (cseqPos < req.size() && req[cseqPos] == ' ') cseqPos++;
		cseq = 0;
		while (cseqPos < req.size() && req[cseqPos] >= '0' && req[cseqPos] <= '9') {
			cseq = cseq * 10 + (req[cseqPos] - '0');
			cseqPos++;
		}
		return true;
	};

	// 解析 RTSP URL 中的路径，用于匹配 stream tag
	auto extractPathFromUrl = [](const std::string& url) -> std::string {
		// rtsp://host:port/path → /path
		size_t pos = url.find("://");
		if (pos == std::string::npos) return url;
		pos += 3;
		pos = url.find('/', pos);
		if (pos == std::string::npos) return "/";
		return url.substr(pos);
	};

	std::string sessionId;
	std::string streamTag;
	std::shared_ptr<StreamNode> streamNode = nullptr;
	StreamNode::STREAM_SESSION rtspSession;
	bool sessionSetup = false;

	// ---- RTSP 推流（接收端）状态 ----
	bool isPushMode = false;          // true=推流模式(ANNOUNCE→RECORD), false=拉流模式(DESCRIBE→PLAY)
	std::shared_ptr<RtspRecvSession> pushSession;
	rtspSession.client_rtp_port = 0;
	rtspSession.client_rtcp_port = 0;

	while (m_rtspRunning_) {
		std::string request;
		if (!readRequest(request)) {
			LOG("[RTSP-Server] Client %s disconnected", clientIp.c_str());
			break;
		}
		if (request.empty()) {
			continue;
		}

		std::string method, url;
		int cseq = 0;
		if (!parseRequestLine(request, method, url, cseq)) {
			LOG("[RTSP-Server] Failed to parse request from %s", clientIp.c_str());
			break;
		}

		LOG("[RTSP-Server] %s %s (CSeq=%d) from %s",
			method.c_str(), url.c_str(), cseq, clientIp.c_str());

		if (method == "OPTIONS") {
			std::ostringstream resp;
			resp << "RTSP/1.0 200 OK\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			resp << "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, ANNOUNCE, RECORD\r\n";
			resp << "\r\n";
			sendResponse(cseq, resp.str());
		}
		else if (method == "ANNOUNCE") {
			// ---- RTSP 推流：客户端推送 SDP 到服务端 ----
			// 重置上一个会话状态（避免跨序列污染），仅重置安全字段
			sessionSetup = false;
			streamTag.clear();

			std::string path = extractPathFromUrl(url);
			std::string tag = path;
			if (!tag.empty() && tag[0] == '/') tag = tag.substr(1);

			// 提取 ANNOUNCE body（SDP）
			std::string sdpBody;
			size_t bodyPos = request.find("\r\n\r\n");
			if (bodyPos != std::string::npos) {
				sdpBody = request.substr(bodyPos + 4);
			}
			// 也可能有 Content-Length
			size_t clPos = request.find("Content-Length:");
			if (clPos != std::string::npos) {
				clPos += 15;
				while (clPos < request.size() && request[clPos] == ' ') clPos++;
				int cl = 0;
				while (clPos < request.size() && request[clPos] >= '0' && request[clPos] <= '9') {
					cl = cl * 10 + (request[clPos] - '0');
					clPos++;
				}
				// 如果 sdpBody 长度不足，再尝试读取
				if ((int)sdpBody.size() < cl) {
					char extraBuf[4096];
					int extraLen = recv(static_cast<SOCKET_TYPE>(clientSock), extraBuf, sizeof(extraBuf) - 1, 0);
					if (extraLen > 0) {
						sdpBody.append(extraBuf, extraLen);
					}
				}
				if ((int)sdpBody.size() > cl) {
					sdpBody = sdpBody.substr(0, cl);
				}
			}

			LOG("[RTSP-Server] ANNOUNCE path=%s, SDP size=%zu", path.c_str(), sdpBody.size());

			// 解析 SDP 获取编码信息
			StreamNode::STREAM_SESSION videoInfo, audioInfo;
			StreamNode tempNode;
			if (sdpBody.empty() || !tempNode.parseSDP(sdpBody, videoInfo, audioInfo)) {
				std::ostringstream resp;
				resp << "RTSP/1.0 400 Bad Request\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());
				continue;
			}

			// 查找或创建 StreamNode
			streamNode = findStreamByRtspPath(path);
			if (!streamNode) {
				// 创建一个只用于接收的 StreamNode（不发起拉流）
				StreamNode::Config cfg;
				cfg.tag = tag;
				cfg.origin_pull_url = "";  // 没有源，纯接收端
				cfg.relay_push_url = "";
				cfg.retry_interval = 3000;
				cfg.max_retries = 0;
				cfg.rtp_timeout = 10000;

				auto node = std::make_shared<StreamNode>();
				// 直接设置 pull_session_ 信息（跳过 doStreamPull）
				node->config_ = cfg;
				node->session_origin_pull_ = videoInfo;
				node->session_origin_pull_.session_type_ = ORIGIN_PULL;
				node->isPulling_ = true;  // 标记为"有流数据"，使 DESCRIBE 不会等待
				node->running_ = true;
				node->state_ = StreamNode::State::PLAYING;

				std::lock_guard<std::mutex> lock(nodeLock_);
				m_mapStreamNodes[tag] = node;
				streamNode = m_mapStreamNodes[tag];
				LOG("[RTSP-Server] Created new StreamNode for push tag=%s, codec=%s, pt=%d",
					tag.c_str(), videoInfo.codec.c_str(), videoInfo.payload_type);
			}
			else {
				// 已存在的节点，更新编码信息
				streamNode->session_origin_pull_ = videoInfo;
				streamNode->session_origin_pull_.session_type_ = ORIGIN_PULL;
				streamNode->isPulling_ = true;
			}

			// 清理上一个推流会话（ANNOUNCE 成功，旧会话不再有效）
			if (isPushMode && !sessionId.empty()) {
				cleanupPushSession(sessionId);
			}
			sessionId.clear();
			pushSession.reset();

			streamTag = tag;
			isPushMode = true;

			// 生成 session ID
			static std::mt19937 rng(std::random_device{}());
			std::uniform_int_distribution<> dist(100000, 999999);
			sessionId = std::to_string(dist(rng));

			std::ostringstream resp;
			resp << "RTSP/1.0 200 OK\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			resp << "Session: " << sessionId << "\r\n";
			resp << "\r\n";
			sendResponse(cseq, resp.str());
		}
		else if (method == "DESCRIBE") {
			// 重置上一个拉流/推流会话状态
			sessionSetup = false;
			streamTag.clear();
			// 清理上一个推流会话（客户端已转拉流，旧推流不再需要）
			if (isPushMode && !sessionId.empty()) {
				cleanupPushSession(sessionId);
			}
			isPushMode = false;
			sessionId.clear();
			pushSession.reset();

			std::string path = extractPathFromUrl(url);

			// 尝试通过路径查找 stream（路径格式: /tag）
			// 注：serveDefaultFolder 启动时已创建所有本地文件对应的 StreamNode，
			// 此处直接查找即可，无需按需加载
			streamNode = findStreamByRtspPath(path);

			if (!streamNode) {
				// 可能已被 cleanupIdleLocalStream 清理，尝试按需重新加载
				std::string loadTag = path;
				if (!loadTag.empty() && loadTag[0] == '/') loadTag = loadTag.substr(1);
				streamNode = loadLocalFileStream(loadTag);
			}

			if (!streamNode) {
				std::ostringstream resp;
				resp << "RTSP/1.0 404 Not Found\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());
				continue;
			}

			// 等待拉流准备好（最多 10 秒）
			int waitCount = 0;
			while (streamNode->isPulling_ == false && waitCount < 50 && m_rtspRunning_) {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				waitCount++;
			}

			streamTag = streamNode->config_.tag;
			std::string sdp = buildSdpForStream(streamNode);

			std::ostringstream resp;
			resp << "RTSP/1.0 200 OK\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			resp << "Content-Type: application/sdp\r\n";
			resp << "Content-Base: " << url << "\r\n";
			resp << "Content-Length: " << sdp.size() << "\r\n";
			resp << "\r\n";
			resp << sdp;
			sendResponse(cseq, resp.str());
		}
		else if (method == "SETUP") {
			if (!streamNode) {
				std::ostringstream resp;
				resp << "RTSP/1.0 454 Session Not Found\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());
				continue;
			}

			// 解析 Transport 头，获取客户端 RTP 端口
			int clientRtpPort = 0;
			int clientRtcpPort = 0;
			int interleavedRtp = -1;
			int interleavedRtcp = -1;
			bool isTcpTransport = false;
			std::string transportMode = "play";  // 默认 play 模式
			size_t transportPos = request.find("Transport:");
			if (transportPos != std::string::npos) {
				std::string transport = request.substr(transportPos);
				size_t endPos = transport.find("\r\n");
				if (endPos != std::string::npos) {
					transport = transport.substr(0, endPos);
				}

				// 检测是否为 TCP 传输 (RTP/AVP/TCP)
				if (transport.find("RTP/AVP/TCP") != std::string::npos ||
					transport.find("RTP/AVP/TCP") != std::string::npos) {
					isTcpTransport = true;
				}

				// 检查 mode
				if (transport.find("mode=record") != std::string::npos) {
					transportMode = "record";
				}
				else if (transport.find("mode=play") != std::string::npos) {
					transportMode = "play";
				}

				// 解析 interleaved=rtpChannel-rtcpChannel (TCP 模式)
				size_t ilPos = transport.find("interleaved=");
				if (ilPos != std::string::npos) {
					ilPos += 12;
					while (ilPos < transport.size() && transport[ilPos] == ' ') ilPos++;
					int ch1 = 0, ch2 = -1;
					while (ilPos < transport.size() && transport[ilPos] >= '0'
						&& transport[ilPos] <= '9') {
						ch1 = ch1 * 10 + (transport[ilPos] - '0');
						ilPos++;
					}
					if (ilPos < transport.size() && transport[ilPos] == '-') {
						ilPos++;
						ch2 = 0;
						while (ilPos < transport.size() && transport[ilPos] >= '0'
							&& transport[ilPos] <= '9') {
							ch2 = ch2 * 10 + (transport[ilPos] - '0');
							ilPos++;
						}
					}
					interleavedRtp = ch1;
					interleavedRtcp = ch2;
				}

				// 解析 client_port=RTP_PORT-RTCP_PORT (UDP 模式)
				size_t cpPos = transport.find("client_port=");
				if (cpPos != std::string::npos) {
					cpPos += 12;
					while (cpPos < transport.size() && transport[cpPos] == ' ') cpPos++;
					int port1 = 0, port2 = 0;
					while (cpPos < transport.size() && transport[cpPos] >= '0'
						&& transport[cpPos] <= '9') {
						port1 = port1 * 10 + (transport[cpPos] - '0');
						cpPos++;
					}
					if (cpPos < transport.size() && transport[cpPos] == '-') {
						cpPos++;
						while (cpPos < transport.size() && transport[cpPos] >= '0'
							&& transport[cpPos] <= '9') {
							port2 = port2 * 10 + (transport[cpPos] - '0');
							cpPos++;
						}
					}
					clientRtpPort = port1;
					clientRtcpPort = port2 > 0 ? port2 : port1 + 1;
				}
			}

			if (clientRtpPort == 0 && interleavedRtp < 0) {
				std::ostringstream resp;
				resp << "RTSP/1.0 400 Bad Request\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());
				continue;
			}

		if (isPushMode && transportMode == "record") {
				bool isExtraTrack = (pushSession != nullptr);
				// ---- 推流模式 SETUP ----
				rtspSession.client_rtp_port = clientRtpPort;
				rtspSession.client_rtcp_port = clientRtcpPort;
				rtspSession.session_type_ = CLIENT_PUBLISH;

				if (isTcpTransport && interleavedRtp >= 0) {
					// ---- TCP interleaved 模式：RTP 数据通过 RTSP TCP 连接传输 ----
					LOG("[RTSP-Server] Push SETUP TCP interleaved: tag=%s, channels=%d-%d",
						streamTag.c_str(), interleavedRtp, interleavedRtcp);

					// 创建推流接收会话（使用 TCP 连接接收 RTP interleaved 数据）
					if (!isExtraTrack) {
					pushSession = std::make_shared<RtspRecvSession>();
					pushSession->rtp_sock = StreamNode::kInvalidSocket;  // 不使用 UDP
					pushSession->rtcp_sock = StreamNode::kInvalidSocket;
					pushSession->tcp_sock = clientSock;  // 使用当前 RTSP TCP 连接
					pushSession->is_tcp_interleaved = true;
					pushSession->interleaved_rtp = interleavedRtp;
					pushSession->interleaved_rtcp = interleavedRtcp;
					pushSession->tag = streamTag;
					pushSession->session_id = sessionId;
					pushSession->client_ip = clientIp;
					pushSession->state = RSS_WAITING_RTP;

					{
					 std::lock_guard<std::mutex> lock(m_pushSessionsMutex_);
					 m_pushSessions_[sessionId] = pushSession;
					}
					LOG("[RTSP-Server] Push SETUP TCP: tag=%s, channels=%d-%d",
					streamTag.c_str(), interleavedRtp, interleavedRtcp);
					} else {
						LOG("[RTSP-Server] Push SETUP TCP extra track ignored (tag=%s)", streamTag.c_str());
					}

					std::ostringstream resp;
					resp << "RTSP/1.0 200 OK\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "Session: " << sessionId << "\r\n";
					resp << "Transport: RTP/AVP/TCP;unicast;interleaved="
						<< interleavedRtp << "-" << interleavedRtcp
						<< ";mode=record\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());

					sessionSetup = true;
				}
				else {
				// ---- UDP 推流模式：服务端创建 UDP socket 接收客户端 RTP ----
				StreamNode::SocketHandle rtpSock = static_cast<StreamNode::SocketHandle>(
					socket(AF_INET, SOCK_DGRAM, 0));
				StreamNode::SocketHandle rtcpSock = static_cast<StreamNode::SocketHandle>(
					socket(AF_INET, SOCK_DGRAM, 0));

				if (rtpSock == StreamNode::kInvalidSocket || rtcpSock == StreamNode::kInvalidSocket) {
					LOG("[RTSP-Server] Failed to create UDP sockets for push");
					std::ostringstream resp;
					resp << "RTSP/1.0 500 Internal Server Error\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());
					continue;
				}

				// 绑定 RTP socket
				struct sockaddr_in bindAddr;
				memset(&bindAddr, 0, sizeof(bindAddr));
				bindAddr.sin_family = AF_INET;
				bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
				bindAddr.sin_port = 0;  // 随机端口

				int serverRtpPort = 0, serverRtcpPort = 0;
				if (::bind(static_cast<SOCKET_TYPE>(rtpSock), (struct sockaddr*)&bindAddr, sizeof(bindAddr)) == 0) {
					socklen_t addrLen = sizeof(bindAddr);
					getsockname(static_cast<SOCKET_TYPE>(rtpSock), (struct sockaddr*)&bindAddr, &addrLen);
					serverRtpPort = ntohs(bindAddr.sin_port);
				}
				bindAddr.sin_port = 0;  // 重置端口，否则会被 getsockname 覆写为 RTP 端口号
				if (::bind(static_cast<SOCKET_TYPE>(rtcpSock), (struct sockaddr*)&bindAddr, sizeof(bindAddr)) == 0) {
					socklen_t addrLen = sizeof(bindAddr);
					getsockname(static_cast<SOCKET_TYPE>(rtcpSock), (struct sockaddr*)&bindAddr, &addrLen);
					serverRtcpPort = ntohs(bindAddr.sin_port);
				}

				if (serverRtpPort == 0) {
					LOG("[RTSP-Server] Failed to bind push UDP sockets");
#ifdef _WIN32
					closesocket(static_cast<SOCKET>(rtpSock));
					closesocket(static_cast<SOCKET>(rtcpSock));
#else
					close(rtpSock); close(rtcpSock);
#endif
					std::ostringstream resp;
					resp << "RTSP/1.0 500 Internal Server Error\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());
					continue;
				}

				// 设置 RTP socket 超时为 1 秒
#ifdef _WIN32
				int to = 1000;
				setsockopt(static_cast<SOCKET>(rtpSock), SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&to, sizeof(to));
#else
				struct timeval tv = {1, 0};
				setsockopt(rtpSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#endif

				if (!isExtraTrack) {
					// 第一个 track（视频）：创建推流接收会话
					pushSession = std::make_shared<RtspRecvSession>();
					pushSession->rtp_sock = rtpSock;
					pushSession->rtcp_sock = rtcpSock;
					pushSession->tcp_sock = clientSock;
					pushSession->server_rtp_port = serverRtpPort;
					pushSession->server_rtcp_port = serverRtcpPort;
					pushSession->tag = streamTag;
					pushSession->session_id = sessionId;
					pushSession->client_ip = clientIp;
					pushSession->client_rtp_port = clientRtpPort;
					pushSession->client_rtcp_port = clientRtcpPort;
					pushSession->state = RSS_WAITING_RTP;

					{
					std::lock_guard<std::mutex> lock(m_pushSessionsMutex_);
					m_pushSessions_[sessionId] = pushSession;
					}
					 LOG("[RTSP-Server] Push SETUP: tag=%s, client=%s:%d-%d, server=%d-%d",
							streamTag.c_str(), clientIp.c_str(), clientRtpPort, clientRtcpPort,
							serverRtpPort, serverRtcpPort);
					} else {
					// 额外 track（音频等）：回复有效端口号但不覆写 pushSession
					LOG("[RTSP-Server] Push SETUP extra track (UDP): tag=%s, server=%d-%d",
						streamTag.c_str(), serverRtpPort, serverRtcpPort);
				}

				std::ostringstream resp;
				resp << "RTSP/1.0 200 OK\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "Session: " << sessionId << "\r\n";
				resp << "Transport: RTP/AVP/UDP;unicast;client_port="
					<< clientRtpPort << "-" << clientRtcpPort
					<< ";server_port=" << serverRtpPort << "-" << serverRtcpPort
					<< ";mode=record\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());

				sessionSetup = true;
				}
		}
		else {
				// ---- 拉流模式 SETUP ----
				// 复制流信息，设置会话参数
				rtspSession = streamNode->session_origin_pull_;
				rtspSession.session_type_ = CLIENT_PULL;
				rtspSession.is_webrtc = false;
				rtspSession.client_rtp_port = clientRtpPort;
				rtspSession.client_rtcp_port = clientRtcpPort;
				rtspSession.remote_host = clientIp;

				// 生成 session ID（如果还没有）
				if (sessionId.empty()) {
					static std::mt19937 rng(std::random_device{}());
					std::uniform_int_distribution<> dist(100000, 999999);
					sessionId = std::to_string(dist(rng));
				}

				if (isTcpTransport && interleavedRtp >= 0) {
					// ---- TCP interleaved 模式：RTP 数据通过当前 RTSP TCP 连接发送 ----
					rtspSession.transport_mode = StreamNode::TransportMode::TCP;
					rtspSession.tcp_socket = clientSock;
					rtspSession.interleaved_rtp = interleavedRtp;
					rtspSession.interleaved_rtcp = interleavedRtcp;

					LOG("[RTSP-Server] Pull SETUP TCP interleaved: tag=%s, channels=%d-%d",
						streamTag.c_str(), interleavedRtp, interleavedRtcp);

					std::ostringstream resp;
					resp << "RTSP/1.0 200 OK\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "Session: " << sessionId << "\r\n";
					resp << "Transport: RTP/AVP/TCP;unicast;interleaved="
						<< interleavedRtp << "-" << interleavedRtcp
						<< ";mode=play\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());

					sessionSetup = true;
				}
				else {
					// ---- UDP 模式 ----
					rtspSession.transport_mode = StreamNode::TransportMode::UDP;

					// 创建服务端 UDP socket（用于发送 RTP）
					streamNode->createUDPServerSocket(rtspSession);

					int serverRtpPort = rtspSession.server_rtp_port;
					int serverRtcpPort = rtspSession.server_rtcp_port;

					std::ostringstream resp;
					resp << "RTSP/1.0 200 OK\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "Session: " << sessionId << "\r\n";
					resp << "Transport: RTP/AVP/UDP;unicast;client_port="
						<< clientRtpPort << "-" << clientRtcpPort
						<< ";server_port=" << serverRtpPort << "-" << serverRtcpPort
						<< ";mode=play\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());

					sessionSetup = true;
				}
			}
		}
		else if (method == "PLAY") {
			if (!sessionSetup || !streamNode) {
				std::ostringstream resp;
				resp << "RTSP/1.0 454 Session Not Found\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());
				continue;
			}

			// 解析 Session 头确认
			size_t sessPos = request.find("Session:");
			if (sessPos != std::string::npos) {
				std::string reqSession = request.substr(sessPos + 8);
				size_t endPos = reqSession.find_first_of("\r\n;");
				if (endPos != std::string::npos) {
					reqSession = reqSession.substr(0, endPos);
					// trim
					while (!reqSession.empty() && reqSession.front() == ' ') {
						reqSession.erase(0, 1);
					}
					while (!reqSession.empty() && reqSession.back() == ' ') {
						reqSession.pop_back();
					}
				}
				if (reqSession != sessionId) {
					std::ostringstream resp;
					resp << "RTSP/1.0 454 Session Not Found\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());
					continue;
				}
			}

			std::ostringstream resp;
			resp << "RTSP/1.0 200 OK\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			resp << "Session: " << sessionId << "\r\n";
			resp << "Range: npt=0.000-\r\n";
			resp << "\r\n";
			sendResponse(cseq, resp.str());

			// 将 RTSP 拉流会话加入 client_sessions_ 列表
			auto sessionPtr = std::make_shared<StreamNode::STREAM_SESSION>(rtspSession);
			streamNode->session_list_client_pull_mutex_.lock();
			streamNode->session_list_client_pull_.push_back(sessionPtr);
			streamNode->session_list_client_pull_mutex_.unlock();

			LOG("[RTSP-Server] Stream %s started playing to %s:%d (RTP port %d)",
				streamTag.c_str(), clientIp.c_str(), rtspSession.client_rtp_port, rtspSession.server_rtp_port);

			// TCP interleaved 拉流：设 recv 超时 1s（非阻塞 recv 避免与 feed 线程 send 并发）
			if (rtspSession.transport_mode == StreamNode::TransportMode::TCP) {
#ifdef _WIN32
				int timeout_tcp_ms = 1000;  // 1s 超时，readRequest 超时重入
				setsockopt(static_cast<SOCKET>(clientSock), SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&timeout_tcp_ms, sizeof(timeout_tcp_ms));
#else
				struct timeval tv_tcp = {1, 0};
				setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&tv_tcp, sizeof(tv_tcp));
#endif
				LOG("[RTSP-Server] TCP interleaved pull: recv timeout set to 1s for %s", clientIp.c_str());
			}
		}
		else if (method == "RECORD") {
			// ---- RTSP 推流 RECORD：开始接收客户端推送的 RTP 数据 ----
			if (!sessionSetup || !streamNode || !pushSession) {
				std::ostringstream resp;
				resp << "RTSP/1.0 454 Session Not Found\r\n";
				resp << "CSeq: " << cseq << "\r\n";
				resp << "\r\n";
				sendResponse(cseq, resp.str());
				continue;
			}

			// 验证 Session
			size_t sessPos = request.find("Session:");
			if (sessPos != std::string::npos) {
				std::string reqSession = request.substr(sessPos + 8);
				size_t endPos = reqSession.find_first_of("\r\n;");
				if (endPos != std::string::npos) {
					reqSession = reqSession.substr(0, endPos);
					while (!reqSession.empty() && reqSession.front() == ' ') reqSession.erase(0, 1);
					while (!reqSession.empty() && reqSession.back() == ' ') reqSession.pop_back();
				}
				if (reqSession != sessionId) {
					std::ostringstream resp;
					resp << "RTSP/1.0 454 Session Not Found\r\n";
					resp << "CSeq: " << cseq << "\r\n";
					resp << "\r\n";
					sendResponse(cseq, resp.str());
					continue;
				}
			}

			std::ostringstream resp;
			resp << "RTSP/1.0 200 OK\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			resp << "Session: " << sessionId << "\r\n";
			resp << "\r\n";
			sendResponse(cseq, resp.str());

			pushSession->state = RSS_RECEIVING;
			pushSession->recv_running_ = true;

			if (pushSession->is_tcp_interleaved) {
				// ---- TCP interleaved 模式：在当前 RTSP 连接中接收 RTP 数据 ----
				LOG("[RTSP-Server] Push RECORD TCP interleaved: tag=%s, session=%s, channels=%d-%d",
					streamTag.c_str(), sessionId.c_str(),
					pushSession->interleaved_rtp, pushSession->interleaved_rtcp);

				// 去掉 recv 超时，避免推流间隙超时断开
#ifdef _WIN32
				int timeout_inf = 0;  // 0 = 无限等待
				setsockopt(static_cast<SOCKET>(clientSock), SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&timeout_inf, sizeof(timeout_inf));
#else
				struct timeval tv_tcp = {1, 0};
				setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&tv_tcp, sizeof(tv_tcp));
#endif
				LOG("[RTSP-TcpRecv] Removed recv timeout for TCP interleaved push, tag=%s", streamTag.c_str());

				// 在当前线程中直接处理 TCP interleaved RTP 接收
				// 循环读取 $channel length RTP_data 格式的数据
				rtpTcpRecvLoop(clientSock, pushSession, streamNode);

				// rtpTcpRecvLoop 返回后清理并退出
				cleanupPushSession(sessionId);
				break;  // 退出 RTSP 请求循环
			}
			else {
				// UDP 模式：启动 RTP 接收线程
				pushSession->recv_thread_ = std::thread(
					&StreamServer::rtpRecvThread, this, pushSession);
				pushSession->recv_thread_.detach();

				// 去掉主循环的 recv 超时，防止 10 秒超时断开 UDP 推流连接
#ifdef _WIN32
				int timeout_inf = 0;  // 0 = 无限等待
				setsockopt(static_cast<SOCKET>(clientSock), SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&timeout_inf, sizeof(timeout_inf));
#else
				struct timeval tv_tcp = {1, 0};
				setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&tv_tcp, sizeof(tv_tcp));
#endif
				LOG("[RTSP-Server] UDP push: removed recv timeout for %s to keep RTSP connection alive", clientIp.c_str());
			}

			LOG("[RTSP-Server] Push RECORD started: tag=%s, session=%s, client=%s:%d",
				streamTag.c_str(), sessionId.c_str(), clientIp.c_str(), rtspSession.client_rtp_port);
		}
		else if (method == "TEARDOWN") {
			std::ostringstream resp;
			resp << "RTSP/1.0 200 OK\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			if (!sessionId.empty()) {
				resp << "Session: " << sessionId << "\r\n";
			}
			resp << "\r\n";
			sendResponse(cseq, resp.str());

			LOG("[RTSP-Server] Client %s teardown, session=%s",
				clientIp.c_str(), sessionId.c_str());

			// 清理推流会话
			if (isPushMode && !sessionId.empty()) {
				cleanupPushSession(sessionId);
			}
			// 清理 TCP interleaved 拉流会话
			if (!isPushMode && streamNode && rtspSession.transport_mode == StreamNode::TransportMode::TCP) {
				streamNode->session_list_client_pull_mutex_.lock();
				auto& sessions = streamNode->session_list_client_pull_;
				sessions.erase(
					std::remove_if(sessions.begin(), sessions.end(),
						[&](const std::shared_ptr<StreamNode::STREAM_SESSION>& s) {
							return s->tcp_socket == clientSock;
						}),
					sessions.end());
				streamNode->session_list_client_pull_mutex_.unlock();
				// 检查本地文件流是否空闲
				cleanupIdleLocalStream(streamTag);
			}
			break;
		}
		else {
			// 未知方法
			std::ostringstream resp;
			resp << "RTSP/1.0 501 Not Implemented\r\n";
			resp << "CSeq: " << cseq << "\r\n";
			resp << "\r\n";
			sendResponse(cseq, resp.str());
		}
	}

	// 清理推流会话（如果客户端异常断开）
	if (isPushMode && !sessionId.empty()) {
		cleanupPushSession(sessionId);
	}

	// 清理 TCP interleaved 拉流会话
	if (!isPushMode && streamNode && rtspSession.transport_mode == StreamNode::TransportMode::TCP) {
		streamNode->session_list_client_pull_mutex_.lock();
		auto& sessions = streamNode->session_list_client_pull_;
		sessions.erase(
			std::remove_if(sessions.begin(), sessions.end(),
				[&](const std::shared_ptr<StreamNode::STREAM_SESSION>& s) {
					return s->tcp_socket == clientSock;
				}),
			sessions.end());
		streamNode->session_list_client_pull_mutex_.unlock();
		LOG("[RTSP-Server] TCP interleaved pull session cleaned for %s", clientIp.c_str());
		cleanupIdleLocalStream(streamTag);
	}
	// 清理 UDP 拉流会话（非 TCP interleaved 的普通拉流）
	if (!isPushMode && streamNode && rtspSession.transport_mode != StreamNode::TransportMode::TCP) {
		streamNode->session_list_client_pull_mutex_.lock();
		auto& sessions = streamNode->session_list_client_pull_;
		sessions.erase(
			std::remove_if(sessions.begin(), sessions.end(),
				[&](const std::shared_ptr<StreamNode::STREAM_SESSION>& s) {
					return s->remote_host == clientIp;
				}),
			sessions.end());
		streamNode->session_list_client_pull_mutex_.unlock();
		cleanupIdleLocalStream(streamTag);
	}

	// 清理：关闭客户端 socket
#ifdef _WIN32
	closesocket(static_cast<SOCKET>(clientSock));
#else
	close(clientSock);
#endif
	LOG("[RTSP-Server] Client handler finished for %s", clientIp.c_str());
}

std::string StreamServer::buildSdpForStream(const std::shared_ptr<StreamNode>& node) {
	const auto& si = node->session_origin_pull_;
	std::ostringstream sdp;

	// 获取本机 IP
	std::string serverIp = "0.0.0.0";

	sdp << "v=0\r\n";
	sdp << "o=- 0 0 IN IP4 " << serverIp << "\r\n";
	sdp << "s=TDS Stream\r\n";
	sdp << "t=0 0\r\n";
	sdp << "m=video 0 RTP/AVP " << si.payload_type << "\r\n";
	sdp << "c=IN IP4 " << serverIp << "\r\n";
	sdp << "a=control:" << si.control_url << "\r\n";
	sdp << "a=rtpmap:" << si.payload_type << " " << si.codec
		<< "/" << si.clock_rate << "\r\n";

	// fmtp
	if (!si.fmtp.empty()) {
		sdp << "a=fmtp:" << si.payload_type << " " << si.fmtp << "\r\n";
	}

	return sdp.str();
}

std::shared_ptr<StreamNode> StreamServer::findStreamByRtspPath(const std::string& path) {
	// 路径格式: /tag → 去除前导 / 得到 tag
	std::string tag = path;
	if (!tag.empty() && tag[0] == '/') {
		tag = tag.substr(1);
	}

	// 先精确匹配 tag
	std::shared_ptr<StreamNode> node = getStreamNodeByTag(tag);
	if (node) return node;

	// 遍历 m_mapStreamNodes 找匹配
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		for (const auto& pair : m_mapStreamNodes) {
			if (pair.second && pair.second->config_.tag == tag) {
				return pair.second;
			}
		}
	}

	return nullptr;
}

// ============================================================================
// RTSP 推流接收：TCP interleaved 模式 — 从 RTSP TCP 连接读取 $channel length RTP_data
// ============================================================================

void StreamServer::rtpTcpRecvLoop(StreamNode::SocketHandle tcpSock,
	std::shared_ptr<RtspRecvSession> session, std::shared_ptr<StreamNode> streamNode) {
	if (!session || !streamNode || tcpSock == StreamNode::kInvalidSocket) {
		LOG("[RTSP-TcpRecv] Invalid parameters, exit");
		return;
	}

	LOG("[RTSP-TcpRecv] TCP interleaved recv loop started: tag=%s, channels=%d-%d",
		session->tag.c_str(), session->interleaved_rtp, session->interleaved_rtcp);

	std::vector<uint8_t> buffer(65536);
	uint64_t packetCount = 0;

	while (session->recv_running_ && streamNode->running_) {
		// 读取 interleaved 头: $ (0x24) + channel(1B) + length(2B big-endian)
		uint8_t header[4];
		int totalRead = 0;
		while (totalRead < 4) {
			int n = recv(static_cast<SOCKET_TYPE>(tcpSock),
				(char*)header + totalRead, 4 - totalRead, 0);
			if (n <= 0) {
#ifdef _WIN32
				int err = WSAGetLastError();
				LOG("[RTSP-TcpRecv] Header recv returned %d, err=%d, totalRead=%d, packetCount=%llu — exit",
					n, (n == 0 ? 0 : err), totalRead, (unsigned long long)packetCount);
#else
				int err = errno;
				LOG("[RTSP-TcpRecv] Header recv returned %d, errno=%d, totalRead=%d, packetCount=%llu — exit",
					n, (n == 0 ? 0 : err), totalRead, (unsigned long long)packetCount);
#endif
				return;
			}
			totalRead += n;
		}

		if (header[0] != 0x24) {
			// 不是 interleaved 数据，可能是 RTSP 消息（如 TEARDOWN）
			// 读取剩余字符直到 \r\n\r\n 来检查是否是 RTSP 请求
			LOG("[RTSP-TcpRecv] Non-interleaved data received (0x%02X), checking for RTSP...", header[0]);
			std::string extra;
			extra.append((char*)header, 4);
			char c;
			while (extra.size() < 4096) {
				int n = recv(static_cast<SOCKET_TYPE>(tcpSock), &c, 1, 0);
				if (n <= 0) break;
				extra += c;
				if (extra.size() >= 4 &&
					extra[extra.size()-4] == '\r' && extra[extra.size()-3] == '\n' &&
					extra[extra.size()-2] == '\r' && extra[extra.size()-1] == '\n') {
					break;
				}
			}
			if (extra.find("TEARDOWN") != std::string::npos) {
				LOG("[RTSP-TcpRecv] TEARDOWN received, stopping");
				return;
			}
			// 其他未知数据，忽略继续
			continue;
		}

		uint8_t channel = header[1];
		uint16_t length = (header[2] << 8) | header[3];

		if (length == 0 || length > 65535) continue;

		// 读取 RTP 数据
		totalRead = 0;
		while (totalRead < (int)length) {
			int n = recv(static_cast<SOCKET_TYPE>(tcpSock),
				(char*)buffer.data() + totalRead, (int)length - totalRead, 0);
			if (n <= 0) {
#ifdef _WIN32
				int err = WSAGetLastError();
				LOG("[RTSP-TcpRecv] RTP data recv returned %d, err=%d, need=%d, got=%d, packetCount=%llu — exit",
					n, (n == 0 ? 0 : err), (int)length, totalRead, (unsigned long long)packetCount);
#else
				int err = errno;
				LOG("[RTSP-TcpRecv] RTP data recv returned %d, errno=%d, need=%d, got=%d, packetCount=%llu — exit",
					n, (n == 0 ? 0 : err), (int)length, totalRead, (unsigned long long)packetCount);
#endif
				return;
			}
			totalRead += n;
		}

		packetCount++;

		// 处理 RTP 包（仅处理 RTP 通道，跳过 RTCP 通道）
		if (channel == (uint8_t)session->interleaved_rtp && length > 12) {
			auto pPkt = std::make_shared<StreamNode::RTPPacket>();
			StreamNode::RTPPacket& packet = *pPkt;

			if (packet.parse(buffer.data(), length)) {
				if (streamNode->session_origin_pull_.video_ssrc == 0 && packet.ssrc != 0) {
					streamNode->session_origin_pull_.video_ssrc = packet.ssrc;
					LOG("[RTSP-TcpRecv] Captured video SSRC=%u from push", packet.ssrc);
				}

				streamNode->addToRtpBuffer(pPkt);
				streamNode->sendRTPPacketToClients(packet);

				{
					std::lock_guard<std::mutex> lock(streamNode->stats_mutex_);
					streamNode->stats_.bytes_received += length;
					streamNode->stats_.frames_received++;
					streamNode->stats_.last_frame_time = std::chrono::steady_clock::now();
				}
			}
		}
	}

	LOG("[RTSP-TcpRecv] Loop exited: recv_running_=%d, stream_running_=%d, packetCount=%llu",
		(int)session->recv_running_, (int)streamNode->running_, (unsigned long long)packetCount);
}

// ============================================================================
// RTSP 推流接收：从客户端 UDP socket 接收 RTP 并分发
// ============================================================================

void StreamServer::rtpRecvThread(std::shared_ptr<RtspRecvSession> session) {
	if (!session || session->rtp_sock == StreamNode::kInvalidSocket) {
		LOG("[RTSP-Recv] Invalid session, thread exit");
		return;
	}

	LOG("[RTSP-Recv] Thread started for tag=%s, session=%s, port=%d",
		session->tag.c_str(), session->session_id.c_str(), session->server_rtp_port);

	std::shared_ptr<StreamNode> streamNode = nullptr;
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		auto it = m_mapStreamNodes.find(session->tag);
		if (it != m_mapStreamNodes.end()) {
			streamNode = it->second;
		}
	}

	if (!streamNode) {
		LOG("[RTSP-Recv] StreamNode not found for tag=%s", session->tag.c_str());
		return;
	}

	// 设置 RTP 接收缓冲区
	const int recvBufSize = 524288;  // 512KB
#ifdef _WIN32
	int bufSize = recvBufSize;
	setsockopt(static_cast<SOCKET>(session->rtp_sock), SOL_SOCKET, SO_RCVBUF,
		(const char*)&bufSize, sizeof(bufSize));
#else
	int bufSize = recvBufSize;
	setsockopt(session->rtp_sock, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
#endif

	std::vector<uint8_t> buffer(65536);
	auto lastPacketTime = std::chrono::steady_clock::now();
	const int RTP_IDLE_TIMEOUT_SEC = 30;  // 30秒收不到包认为推流断开

	while (session->recv_running_ && streamNode->running_) {
		struct sockaddr_in fromAddr;
		socklen_t fromLen = sizeof(fromAddr);
		int received = recvfrom(static_cast<SOCKET_TYPE>(session->rtp_sock),
#ifdef _WIN32
			(char*)buffer.data(), (int)buffer.size(), 0,
#else
			buffer.data(), buffer.size(), 0,
#endif
			(struct sockaddr*)&fromAddr, &fromLen);

		if (received > 12) {
			lastPacketTime = std::chrono::steady_clock::now();
			// 诊断：每秒输出一次收包统计
			auto pPkt = std::make_shared<StreamNode::RTPPacket>();
			StreamNode::RTPPacket& packet = *pPkt;

			if (packet.parse(buffer.data(), received)) {
				// 更新 SSRC
				if (streamNode->session_origin_pull_.video_ssrc == 0 && packet.ssrc != 0) {
					streamNode->session_origin_pull_.video_ssrc = packet.ssrc;
					LOG("[RTSP-Recv] Captured video SSRC=%u from push", packet.ssrc);
				}

				// 放入缓存
				streamNode->addToRtpBuffer(pPkt);

				// 发送给拉流客户端
				streamNode->sendRTPPacketToClients(packet);

				// 更新统计
				{
					std::lock_guard<std::mutex> lock(streamNode->stats_mutex_);
					streamNode->stats_.bytes_received += received;
					streamNode->stats_.frames_received++;
					streamNode->stats_.last_frame_time = std::chrono::steady_clock::now();
				}

				// 录像处理（如果启用）
				{
					std::lock_guard<std::recursive_mutex> lock(streamNode->rec_mutex_);
					if (streamNode->rec_ctrl_.recording) {
						if (streamNode->rec_ctrl_.firstWrite && !streamNode->rec_ctrl_.preRecordingDone) {
							// 从 rtp_buffer_ 取出预录数据
							std::vector<std::shared_ptr<StreamNode::RTPPacket>> pre_packets;
							{
								std::lock_guard<std::mutex> qlock(streamNode->queue_mutex_);
								uint32_t _clock = (streamNode->session_origin_pull_.clock_rate > 0) ?
									static_cast<uint32_t>(streamNode->session_origin_pull_.clock_rate) : 90000u;
								for (auto it = streamNode->rtp_buffer_.rbegin();
									it != streamNode->rtp_buffer_.rend(); ++it) {
									if (packet.timestamp - (*it)->timestamp <=
										static_cast<uint64_t>(streamNode->rec_ctrl_.preSeconds) * _clock) {
										pre_packets.push_back(*it);
									}
									else { break; }
								}
							}
							for (auto it = pre_packets.rbegin(); it != pre_packets.rend(); ++it) {
								streamNode->recordRTPPacket(*it);
							}
							streamNode->rec_ctrl_.preRecordingDone = true;
						}
						streamNode->recordRTPPacket(pPkt);
					}
				}
			}
		}
		else if (received < 0) {
			// 超时或错误，检查是否长时间没有收到数据
			auto now = std::chrono::steady_clock::now();
			auto idleSec = std::chrono::duration_cast<std::chrono::seconds>(now - lastPacketTime).count();
			if (idleSec >= RTP_IDLE_TIMEOUT_SEC) {
				LOG("[RTSP-Recv] No RTP data for %lld seconds, closing RTSP connection for tag=%s",
					(long long)idleSec, session->tag.c_str());
				// 关闭 RTSP 控制连接，触发 handleRtspClient 信令线程退出
				if (session->tcp_sock != StreamNode::kInvalidSocket) {
#ifdef _WIN32
					shutdown(static_cast<SOCKET>(session->tcp_sock), SD_BOTH);
					// closesocket not used here — tcp_sock owned by handleRtspClient
#else
					shutdown(session->tcp_sock, SHUT_RDWR);
					// close not used here — tcp_sock owned by handleRtspClient
#endif
					session->tcp_sock = StreamNode::kInvalidSocket;
				}
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	// 清理
	LOG("[RTSP-Recv] Thread stopped for tag=%s, session=%s",
		session->tag.c_str(), session->session_id.c_str());
}

void StreamServer::cleanupPushSession(const std::string& sessionId) {
	std::shared_ptr<RtspRecvSession> session;
	{
		std::lock_guard<std::mutex> lock(m_pushSessionsMutex_);
		auto it = m_pushSessions_.find(sessionId);
		if (it != m_pushSessions_.end()) {
			session = it->second;
			m_pushSessions_.erase(it);
		}
	}

	if (!session) return;

	LOG("[RTSP-Recv] Cleaning up push session %s", sessionId.c_str());

	// 停止接收线程
	session->recv_running_ = false;

	// 关闭 socket
	if (session->is_tcp_interleaved) {
		// TCP 连接由 handleRtspClient 管理，不在此关闭
		session->tcp_sock = StreamNode::kInvalidSocket;
	}
	else {
		// 关闭 RTSP 控制连接，触发 handleRtspClient 信令线程退出
		if (session->tcp_sock != StreamNode::kInvalidSocket) {
#ifdef _WIN32
			shutdown(static_cast<SOCKET>(session->tcp_sock), SD_BOTH);
			// closesocket not used here — tcp_sock owned by handleRtspClient
#else
			shutdown(session->tcp_sock, SHUT_RDWR);
			// close not used here — tcp_sock owned by handleRtspClient
#endif
			session->tcp_sock = StreamNode::kInvalidSocket;
		}
		if (session->rtp_sock != StreamNode::kInvalidSocket) {
#ifdef _WIN32
			closesocket(static_cast<SOCKET>(session->rtp_sock));
#else
			close(session->rtp_sock);
#endif
			session->rtp_sock = StreamNode::kInvalidSocket;
		}
		if (session->rtcp_sock != StreamNode::kInvalidSocket) {
#ifdef _WIN32
			closesocket(static_cast<SOCKET>(session->rtcp_sock));
#else
			close(session->rtcp_sock);
#endif
			session->rtcp_sock = StreamNode::kInvalidSocket;
		}
	}
}
