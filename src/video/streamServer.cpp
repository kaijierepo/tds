#include "pch.h"
#include "streamServer.h"
#include "dtls_transport.h"
#include "logger.h"
#include <sstream>
#include <fstream>
#include <random>
#include <cstdio>
#include <algorithm>
#include "prj.h"
#include <filesystem>

StreamServer streamSrv;

// ============================================================================
// DTLS 证书初始化
// ============================================================================
void StreamServer::initDtlsCertificate() {
    // 幂等：已初始化则跳过
    if (!m_dtlsCertPem.empty()) return;

    std::string certPem, keyPem;

    // 1. 优先从配置目录 webRtcKey 下加载持久化的证书和私钥
    std::string keyDir = tds->conf->confPath + "/webRtcKey";
    std::string certPath = keyDir + "/cert.pem";
    std::string keyPath = keyDir + "/key.pem";

    std::ifstream certFile(certPath);
    std::ifstream keyFile(keyPath);
    if (certFile.is_open() && keyFile.is_open()) {
        std::stringstream certBuf, keyBuf;
        certBuf << certFile.rdbuf();
        keyBuf << keyFile.rdbuf();
        certPem = certBuf.str();
        keyPem = keyBuf.str();
        if (!certPem.empty() && !keyPem.empty()) {
            LOG("[DTLS] Certificate loaded from disk: %s", certPath.c_str());
        }
    }

    // 2. 兜底：如果文件不存在，运行时生成自签证书（仅内存，不写盘）
    if (certPem.empty() || keyPem.empty()) {
		string genCert, genKey;
		DtlsTransport::generateSelfSignedCert("TDS WebRTC Server",genCert,genKey);
        certPem = genCert;
        keyPem = genKey;
        LOG("[DTLS] Certificate generated in memory (no files on disk)");
    }

    m_dtlsCertPem = certPem;
    m_dtlsKeyPem = keyPem;

    // 计算 SHA-256 指纹用于 SDP
    if (!certPem.empty()) {
        mbedtls_x509_crt cert;
        mbedtls_x509_crt_init(&cert);
        int ret = mbedtls_x509_crt_parse(&cert,
            (const unsigned char*)certPem.c_str(), certPem.size() + 1);
        if (ret == 0) {
            m_dtlsFingerprint = DtlsTransport::getFingerprint(cert);
        }
        mbedtls_x509_crt_free(&cert);
    }

    LOG("[DTLS] Certificate initialized, fingerprint: %s", m_dtlsFingerprint.c_str());
}

StreamNode* StreamServer::getStreamNode(std::string tag)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	auto it = m_mapStreamNodes.find(tag);
	if (it != m_mapStreamNodes.end()) {
		return it->second.get();
	}
	return nullptr;
}


StreamNode* StreamServer::getStreamNodeByIp(const std::string& ip)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	for (const auto& pair : m_mapStreamNodes) 
	{
		if (pair.second &&
			pair.second->config_.source_url.find(ip) != std::string::npos) 
		{
			return pair.second.get();
		}
	}
	return nullptr;
}



bool StreamServer::handleRpc(std::string method, yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session) {
	bool bHandled = true;

	if (method == "startStreamNode") {
		rpc_startStreamNode(params, rpcResp, session);
	}
	else if (method == "stopStreamNode") {

	}
	else if (method == "playWebRtc") {
		rpc_playWebRtc(params, rpcResp, session);
	}
	else if (method == "getStreamInfo") {
		rpc_getStreamInfo(params, rpcResp, session);
	}
	else if (method == "startRecord") {
		rpc_startRecord(params, rpcResp, session);
	}
	else if (method == "stopRecord") {
		rpc_stopRecord(params, rpcResp, session);
	}
	else if (method == "removeRecordFile") {
		rpc_removeRecordFile(params, rpcResp, session);
	}
	else {
		bHandled = false;
	}

	return bHandled;
}

bool StreamServer::rpc_startStreamNode(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string srcUrl, destUrl;
	yyjson_val* yyv = yyjson_obj_get(params, "srcUrl");
	if (yyv)
		srcUrl = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params,"destUrl");
	if(yyv)
		 destUrl = yyjson_get_str(yyv);
	{
		std::lock_guard<std::mutex> lock(nodeLock_url_);
		if (m_mapStreamNodes_urlID.find(srcUrl) != m_mapStreamNodes_urlID.end()) {
			LOG("[流媒体] StreamNode 已在运行 for src url: %s", srcUrl.c_str());
			return true;
		}
	}

	// 2. 创建新的 StreamNode
	auto relay = std::make_unique<StreamNode>();

	// 设置回调
	relay->setFrameCallback([](const uint8_t* data, size_t size, uint32_t timestamp) {
		// 可以在这里处理帧，例如存档或分析
		});
	relay->setStatusCallback([](StreamNode::State state, const std::string& msg) {
		LOG("[StreamNode] Status: %d - %s", static_cast<int>(state), msg.c_str());
		});
	relay->setErrorCallback([](const std::string& error, int code) {
		LOG("[StreamNode] Error (%d): %s", code, error.c_str());
		});


	// 3. 配置 relay
	StreamNode::Config config;
	config.source_url = srcUrl; // 源地址
	// 提取用户名和密码
	bool isSuccess = relay->extractRtspAuthInfo(config);

	config.target_url = destUrl;
	config.retry_interval = 3000;
	config.max_retries = 0; // 无限重试
	config.rtp_timeout = 10000;

	// 4. 启动 relay
	LOG("[流媒体] 启动 StreamNode (内置模式)，源: %s, 目标: %s",
		config.source_url.c_str(), config.target_url.c_str());

	if (relay->start(config)) {
		std::lock_guard<std::mutex> lock(nodeLock_url_);
		m_mapStreamNodes_urlID[srcUrl] = std::move(relay);
	}
	else {
		LOG("[流媒体] 启动 StreamNode 失败 for srcUrl: %s", srcUrl.c_str());
	}

	rpcResp.result = RPC_OK;
	return true;
}

bool StreamServer::rpc_playWebRtc(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag;
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);

	// 提取浏览器生成的 SDP Offer（后续可用于协商编解码参数等）
	string sdpOffer;
	yyv = yyjson_obj_get(params, "sdpOffer");
	if (yyv)
		sdpOffer = yyjson_get_str(yyv);
	LOG("[WebRTC] playWebRtc tag=%s, sdpOffer=%zu bytes", tag.c_str(), sdpOffer.size());

	int clientRtpPort = 0;
	yyv = yyjson_obj_get(params, "clientRtpPort");
	if (yyjson_is_int(yyv)) {
		clientRtpPort = yyjson_get_int(yyv);
	}
	StreamNode* rc = getStreamNode(tag);
	if (rc) {
		StreamNode::STREAM_SESSION si = rc->pull_session_;
		si.session_type_ = StreamNode::SERVER_PULL;
		si.client_rtp_port = clientRtpPort;
		si.remote_host = session.remoteIP;
		rc->createUDPServerSocket(si);

		// —— 构造 WebRTC SDP Answer ——
		std::string serverIp = session.localIP;
		if (serverIp.empty()) serverIp = "0.0.0.0";

		// 生成 ICE 凭据（每个会话随机，长度符合 RFC 5245 要求）
		std::string iceUfrag;
		std::string icePwd;
		{
			static const char alphanum[] =
				"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/";
			std::mt19937 rng(std::random_device{}());
			std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);
			for (int i = 0; i < 8; i++) iceUfrag += alphanum[dist(rng)];
			for (int i = 0; i < 22; i++) icePwd += alphanum[dist(rng)];
		}

		// 标记为 WebRTC 会话并写入 ICE 凭据
		si.is_webrtc = true;
		si.ice_ufrag = iceUfrag;
		si.ice_pwd = icePwd;

		// 使用自签证书的真实 SHA-256 指纹
		std::string fingerprint = m_dtlsFingerprint.empty()
			? "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
			  "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00"
			: m_dtlsFingerprint;

		// 将 SPS/PPS 编码为 Base64（用于 SDP sprop-parameter-sets）
		// 格式: <sps_base64>,<pps_base64>
		std::string spropParamSets;
		if (!si.sps.empty() && !si.pps.empty()) {
			// 拼接 SPS 和 PPS 为 sprop-parameter-sets 格式：
			// Base64(SPS),Base64(PPS)
			std::string sps_raw(reinterpret_cast<const char*>(si.sps.data()), si.sps.size());
			std::string pps_raw(reinterpret_cast<const char*>(si.pps.data()), si.pps.size());
			std::string sps_b64 = StreamNode::base64Encode(sps_raw);
			std::string pps_b64 = StreamNode::base64Encode(pps_raw);
			spropParamSets = sps_b64 + "," + pps_b64;
		}

		std::ostringstream sdp;
		sdp << "v=0\r\n";
		sdp << "o=- 0 0 IN IP4 " << serverIp << "\r\n";
		sdp << "s=TDS\r\n";
		sdp << "t=0 0\r\n";
		sdp << "m=video " << si.server_rtp_port
			<< " UDP/TLS/RTP/SAVPF " << si.payload_type << "\r\n";
		sdp << "c=IN IP4 " << serverIp << "\r\n";
		sdp << "a=mid:0\r\n";                             // 媒体流标识（匹配浏览器 Offer）
		sdp << "a=rtpmap:" << si.payload_type
			<< " " << si.codec << "/" << si.clock_rate << "\r\n";

		// 构造 fmtp 行：如果已有 fmtp 则在其后追加 sprop-parameter-sets，
		// 否则从 sps/pps 构造完整 fmtp
		std::string fmtpLine;
		if (!si.fmtp.empty()) {
			fmtpLine = si.fmtp;
		}
		if (!spropParamSets.empty()) {
			// 如果原有 fmtp 已有 sprop-parameter-sets，则不重复添加
			if (fmtpLine.find("sprop-parameter-sets") == std::string::npos) {
				if (!fmtpLine.empty()) fmtpLine += ";";
				fmtpLine += "sprop-parameter-sets=" + spropParamSets;
			}
		}
		// 确保 packetization-mode 存在（RFC 6184 必需，默认 mode=1 支持 FU-A/STAP-A）
		if (!fmtpLine.empty() && fmtpLine.find("packetization-mode") == std::string::npos) {
			fmtpLine = "packetization-mode=1;" + fmtpLine;
		}
		// 确保 profile-level-id 存在（如果原始 fmtp 没有，填一个默认值）
		if (!fmtpLine.empty() && fmtpLine.find("profile-level-id") == std::string::npos) {
			// 从 SPS 前 3 字节推导 profile-level-id
			if (si.sps.size() >= 3) {
				char buf[16];
				snprintf(buf, sizeof(buf), "profile-level-id=%02X%02X%02X",
					si.sps[0], si.sps[1], si.sps[2]);
				fmtpLine = std::string(buf) + ";" + fmtpLine;
			} else {
				fmtpLine = "profile-level-id=42C01F;" + fmtpLine;
			}
		}
		// 确保 level-asymmetry-allowed 存在
		if (!fmtpLine.empty() && fmtpLine.find("level-asymmetry-allowed") == std::string::npos) {
			fmtpLine += ";level-asymmetry-allowed=1";
		}
		if (!fmtpLine.empty()) {
			sdp << "a=fmtp:" << si.payload_type << " " << fmtpLine << "\r\n";
		}

		sdp << "a=rtcp-mux\r\n";                          // RTCP 复用 RTP 端口
		sdp << "a=rtcp-rsize\r\n";                        // 精简 RTCP
		sdp << "a=sendonly\r\n";                         // 服务端仅发送视频
		sdp << "a=setup:passive\r\n";                     // DTLS server
		sdp << "a=ice-lite\r\n";                           // ICE-Lite 模式
		sdp << "a=ice-ufrag:" << iceUfrag << "\r\n";
		sdp << "a=ice-pwd:" << icePwd << "\r\n";
		sdp << "a=fingerprint:sha-256 " << fingerprint << "\r\n";
		// SSRC 声明：使用实际流中的 SSRC（如果尚未捕获则用 1 作为占位符）
		uint32_t declaredSsrc = si.video_ssrc ? si.video_ssrc : 1;
		sdp << "a=ssrc:" << declaredSsrc << " cname:TDS\r\n";
		sdp << "a=candidate:1 1 UDP 2130706431 "
			<< serverIp << " " << si.server_rtp_port << " typ host\r\n";

		si.sdp = sdp.str();

		// 打印完整 SDP Answer 用于调试
		LOG("[WebRTC] SDP Answer:\n%s", si.sdp.c_str());

		// 先推入列表，使用 shared_ptr 确保 vector 扩容时 ICE 线程持有的指针不会失效
		auto sessionPtr = std::make_shared<StreamNode::STREAM_SESSION>(si);
		rc->client_sessions_mutex_.lock();
		rc->client_sessions_.push_back(sessionPtr);
		rc->client_sessions_mutex_.unlock();

		// 启动 ICE-Lite 线程，监听该会话的 UDP 端口并响应 STUN Binding Request
		rc->startIceHandleThread(sessionPtr);

#ifdef _WIN32
		Sleep(1000);
#else
		usleep(1000 * 1000);
#endif
		json j;
		j["sdpAnswer"] = si.sdp;
		j["serverRtpPort"] = si.server_rtp_port;
		j["serverRtspPort"] = si.server_rtcp_port;
		j["clientRtpPort"] = si.client_rtp_port;
		rpcResp.result = j.dump();
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "rtsp client of specified tag not found");
	}

	return true;
}

bool StreamServer::rpc_startRecord(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag;
	string ip;
	yyjson_val* yyv = yyjson_obj_get(params, "camera_ip");
	if (yyv)
		ip = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);
	int preTime = 0;
	yyv = yyjson_obj_get(params, "preSeconds");
	if (yyv)
		preTime = yyjson_get_int(yyv);
	StreamNode* rc = nullptr;
	if (!ip.empty()) {
		rc = getStreamNodeByIp(ip);
	} else if (!tag.empty()) {
		rc = getStreamNode(tag);
	}
	if (rc) {
		std::lock_guard<std::recursive_mutex> lock(rc->rec_mutex_);  // 与 doRtpRecv 录制线程互斥
		if (rc->rec_ctrl_.recording == false)
		{
			rc->rec_ctrl_.fu_a_buffer_.clear();
			rc->rec_ctrl_.firstWrite = true;
			rc->rec_ctrl_.preRecordingDone = false;
			rc->rec_ctrl_.preSeconds = preTime;
			DB_TIME now; now.setNow();
			std::string ts = str::format("%04d%02d%02d_%02d%02d%02d",
				now.wYear, now.wMonth, now.wDay,
				now.wHour, now.wMinute, now.wSecond);
			rc->rec_ctrl_.path = tds->conf->dbPath + "/record/"
				+ rc->config_.tag + "_" + ts + ".h264";
			rc->rec_ctrl_.startTime = std::chrono::steady_clock::now();
			rc->rec_ctrl_.recording = true;
			rpcResp.result = RPC_OK;
		}
		else
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "record is already started");
			return false;
		}
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "rtsp client of specified tag or ip not found");
	}
	LOG("[HTTP API]startRecord, tag: %s, camera_ip: %s, preSeconds: %d", tag.c_str(), ip.c_str(), preTime);
	return true;
}

bool StreamServer::rpc_stopRecord(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag;
	string ip;
	yyjson_val* yyv = yyjson_obj_get(params, "camera_ip");
	if (yyv)
		ip = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);
	StreamNode* rc = nullptr;
	if (!ip.empty()) {
		rc = getStreamNodeByIp(ip);
	} else if (!tag.empty()) {
		rc = getStreamNode(tag);
	}
	if (rc) {
		std::lock_guard<std::recursive_mutex> lock(rc->rec_mutex_);  // 与 doRtpRecv 录制线程互斥
		if (rc->rec_ctrl_.recording == true)
		{
			// 先停止录制，确保 RTP 线程不再写入新数据，再刷缓冲区
			rc->rec_ctrl_.recording = false;
			rc->flushRecordBuffer();

			// 计算 duration（秒）：录制会话挂钟时间
			auto now = std::chrono::steady_clock::now();
			int duration = static_cast<int>(
				std::chrono::duration_cast<std::chrono::seconds>(
					now - rc->rec_ctrl_.startTime).count());		
			// fileUrl 用相对路径
			std::string filePath = rc->rec_ctrl_.path;
			size_t pos = filePath.find_last_of("/\\");
			std::string fileName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
			std::string fileUrl = "/db/record/" + fileName;

			json j;
			j["fileUrl"] = fileUrl;
			j["duration"] = duration;
			j["preSeconds"] = rc->rec_ctrl_.preSeconds;
			rpcResp.result = j.dump();

			// 清理超出数量限制的旧录像文件
			cleanOldRecords();
		}
		else
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "record is not start");
			return false;
		}
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "rtsp client of specified tag or ip not found");
	}
		LOG("[HTTP API]stopRecord, tag: %s, camera_ip: %s", tag.c_str(), ip.c_str());
	return true;
}

bool StreamServer::rpc_removeRecordFile(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string fileUrl;
	yyjson_val* yyv = yyjson_obj_get(params, "fileUrl");
	if (yyv)
		fileUrl = yyjson_get_str(yyv);
	if (fileUrl.empty())
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param missing, fileUrl is not specified");
		return true;
	}
	std::string filePath = tds->conf->dbPath + fileUrl.substr(std::string("/db").length());
	if (std::remove(filePath.c_str()) == 0) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::OS_fileNotExist, "file not exist or failed to delete");
	}
	LOG("[HTTP API]removeRecordFile, fileUrl: %s", fileUrl.c_str());
	return true;
}

bool StreamServer::rpc_getStreamInfo(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string streamId;
	yyjson_val* yyv = yyjson_obj_get(params, "streamId");
	if (yyv)
		streamId = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "tag");
	if (yyv)
	{
		streamId = yyjson_get_str(yyv);
		streamId = TAG::addRoot(streamId, session.org);
	}
	if (streamId == "")
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param missing,streamid or tag is not specified");
		return true;
	}

	StreamNode* rc = getStreamNode(streamId);
	if (rc == nullptr)
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_NO_STREAM_SRC, "no stream src of this tag");
		return true;
	}

	json jSi;
	jSi["srcUrl"] = rc->config_.source_url;
	jSi["destUrl"] = rc->config_.target_url;
	json jRtpBuffer;
	jRtpBuffer["size"] = rc->rtp_buffer_.size();
	jRtpBuffer["maxSeconds"] = rc->rtp_buffer_max_seconds_;
	jRtpBuffer["bufferedSeconds"] = rc->getBufferedSeconds();
	jSi["rtpBuffer"] = jRtpBuffer;
	json jRecCtrl;
	jRecCtrl["recording"] = rc->rec_ctrl_.recording;
	jRecCtrl["preSeconds"] = rc->rec_ctrl_.preSeconds;
	jSi["recordCtrl"] = jRecCtrl;
	json jStatis;
	StreamNode::Statistics statis = rc->getStatistics();
	jStatis["bitrate"] = statis.bitrate;
	jStatis["fps"] = statis.fps;
	jStatis["frameReceived"] = statis.frames_received;
	jStatis["frameForwarded"] = statis.frames_forwarded;
	jStatis["bytesReceived"] = statis.bytes_received;
	jStatis["bytesForwarded"] = statis.bytes_forwarded;
	jStatis["reconnectCount"] = statis.reconnect_count;
	jSi["statis"] = jStatis;
	jSi["startTime"] = statis.start_time.time_since_epoch().count();
	rpcResp.result = jSi.dump();
	return true;
}

bool StreamServer::rpc_getStreamNodeList(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	const auto& mapStreamNodes = m_mapStreamNodes;

	// 1. 创建yyjson文档和根对象（JSON数组）
	yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* root_arr = yyjson_mut_arr(doc);
	yyjson_mut_doc_set_root(doc, root_arr);

	// 2. 遍历map中的每个StreamNode实例
	for (const auto& pair : mapStreamNodes) {
		const std::string& relay_key = pair.first;          // map的key（比如RTSP流标识）
		const std::unique_ptr<StreamNode>& relay_ptr = pair.second;

		// 安全检查：跳过空指针
		if (!relay_ptr) {
			continue;
		}

		// 3. 获取当前实例的Statistics统计信息
		const StreamNode::Statistics& stats = relay_ptr->getStatistics();

		// 4. 创建当前relay的JSON对象
		yyjson_mut_val* relay_obj = yyjson_mut_obj(doc);

		// 4.1 添加map的key（便于识别每个relay）
		yyjson_mut_obj_add_str(doc, relay_obj, "relay_key", relay_key.c_str());

		// 4.2 逐个添加Statistics的字段到JSON对象
		// 无符号整数类型字段
		yyjson_mut_obj_add_uint(doc, relay_obj, "frames_received", stats.frames_received);
		yyjson_mut_obj_add_uint(doc, relay_obj, "frames_forwarded", stats.frames_forwarded);
		yyjson_mut_obj_add_uint(doc, relay_obj, "bytes_received", stats.bytes_received);
		yyjson_mut_obj_add_uint(doc, relay_obj, "bytes_forwarded", stats.bytes_forwarded);
		yyjson_mut_obj_add_uint(doc, relay_obj, "reconnect_count", stats.reconnect_count);
		yyjson_mut_obj_add_uint(doc, relay_obj, "errors", stats.errors);

		// 时间戳字段：转换为秒级整数
		using namespace std::chrono;
		uint64_t start_time_ms = duration_cast<seconds>(stats.start_time.time_since_epoch()).count();
		uint64_t last_frame_time_ms = duration_cast<seconds>(stats.last_frame_time.time_since_epoch()).count();
		yyjson_mut_obj_add_uint(doc, relay_obj, "start_time_s", start_time_ms);
		yyjson_mut_obj_add_uint(doc, relay_obj, "last_frame_time_s", last_frame_time_ms);

		// 浮点数类型字段
		yyjson_mut_obj_add_real(doc, relay_obj, "fps", stats.fps);
		yyjson_mut_obj_add_real(doc, relay_obj, "bitrate_kbps", stats.bitrate);

		// 4.3 将当前relay的JSON对象添加到根数组
		yyjson_mut_arr_add_val(root_arr, relay_obj);
	}

	// 5. 将JSON文档转换为字符串（带格式化，便于阅读）
	// 如需紧凑格式，将YYJSON_WRITE_PRETTY改为0
	size_t len;
	char* json_str = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);

	std::string result;
	if (json_str) {
		rpcResp.result = json_str;
		free(json_str);  // 释放yyjson分配的字符串内存
	}

	// 6. 释放yyjson文档内存
	yyjson_mut_doc_free(doc);
	return true;
}


bool StreamServer::openStream(string tag, string pushTo)
{
	m_enableZLM = tds->conf->getInt("enableZLM", 0) != 0;
	MP* pmp = prj.GetMPByTag(tag, "zh");
	if (!pmp) {
		LOG("[流媒体] 请求的位号不存在, tag=" + tag);
		return false;
	}

	if (pmp->m_isOpenningStream) {
		LOG("[流媒体] 当前正在打开媒体源，收到重复打开请求，忽略, 位号:%s, 当前配置地址:%s",
			tag.c_str(), pmp->m_mediaUrl.c_str());
		return false;
	}

	pmp->m_isOpenningStream = true;

	// 如果拉流地址变更，先关闭旧流
	if (pmp->m_mpStatus.m_pullingSrcUrl != pmp->m_mediaUrl && pmp->m_mpStatus.m_pullingSrcUrl != "") {
		LOG("[流媒体] 监测到媒体源配置变更，先关闭拉流，当前拉流地址:%s, 配置地址:%s",
			pmp->m_mpStatus.m_pullingSrcUrl.c_str(), pmp->m_mediaUrl.c_str());

		closeStream(tag);
	}

	bool ret = false;
	if (m_enableZLM) {
		bool retPull = pmp->startStreamPull();   //zlm  addStreamProxy
		bool pushRet = false;
		if (pushTo != "") {
			if (retPull) {
				timeopt::sleepMilli(500);
				pushRet = pmp->startStreamPush(pushTo);  //zlm  addStreamPusherProxy
				LOG("[流媒体] 向上级服务推流（ZLM模式），url=%s", pushTo.c_str());
				ret = pushRet;
			}
		}
		else {
			ret = retPull;
		}
	}
	else {
		StreamNode* rc = getStreamNode(tag);
		if (rc) {
			LOG("[流媒体] StreamNode 已在运行 for tag: %s", tag.c_str());
			if (rc->config_.target_url == "" && pushTo != "") {
				rc->config_.target_url = pushTo;
				LOG("[流媒体] StreamNode is pulling for tag: %s, start push to %s", tag.c_str(), pushTo.c_str());
			}
			pmp->m_isOpenningStream = false;
			return true;
		}

		// 创建新的 StreamNode
		auto rtspClt = std::make_unique<StreamNode>();

		// 3. 配置 relay
		StreamNode::Config config;
		config.source_url = pmp->m_mediaUrl; // 源地址
		// 提取用户名和密码
		bool isSuccess = rtspClt->extractRtspAuthInfo(config);

		config.target_url = pushTo; // 目标地址
		config.retry_interval = 3000;
		config.max_retries = 0; // 无限重试
		config.rtp_timeout = 10000;
		config.tag = tag;

		if (rtspClt->start(config)) {
			std::lock_guard<std::mutex> lock(nodeLock_);
			m_mapStreamNodes[tag] = std::move(rtspClt);
			ret = true;
		}
		else {
			ret = false;
		}
	}

	pmp->m_isOpenningStream = false;
	return ret;
}

bool StreamServer::closeStream(string tag)
{
	if (m_enableZLM) {
		MP* pmp = prj.GetMPByTag(tag, "zh");
		if (pmp) {
			pmp->stopStreamPush();
			pmp->stopStreamPull(tag);
		}
	}
	else {
		// 再尝试关闭 StreamNode
		std::unique_ptr<StreamNode> relayToStop;
		{
			std::lock_guard<std::mutex> lock(nodeLock_);
			auto it = m_mapStreamNodes.find(tag);
			if (it != m_mapStreamNodes.end()) {
				relayToStop = std::move(it->second);
				m_mapStreamNodes.erase(it);
				LOG("[流媒体] 正在停止 StreamNode for tag: %s", tag.c_str());
			}
		}

		// 在锁外停止 relay，避免潜在死锁
		if (relayToStop) {
			relayToStop->stop();
			LOG("[流媒体] StreamNode 已停止 for tag: %s", tag.c_str());
		}
	}

	return true;
}

void StreamServer::cleanOldRecords() {
	std::string recordDir = tds->conf->dbPath + "/record/";
	std::vector<std::pair<std::string, std::filesystem::file_time_type>> files;

	namespace fs = std::filesystem;

	try {
		if (!fs::exists(recordDir) || !fs::is_directory(recordDir))
			return;

		for (const auto& entry : fs::directory_iterator(recordDir)) {
			if (entry.is_regular_file() && entry.path().extension() == ".h264") {
				files.push_back({ entry.path().string(), entry.last_write_time() });
			}
		}
	}
	catch (const std::exception& e) {
		LOG("[录像清理] 扫描目录失败: %s, 错误: %s", recordDir.c_str(), e.what());
		return;
	}

	int maxFiles = tds->conf->recordMaxFiles;
	if (maxFiles <= 0) maxFiles = 100;

	if ((int)files.size() <= maxFiles)
		return;

	std::sort(files.begin(), files.end(),
		[](const auto& a, const auto& b) { return a.second < b.second; });

	int toDelete = (int)files.size() - maxFiles;
	int deleted = 0;
	for (int i = 0; i < toDelete; i++) {
		const std::string& filePath = files[i].first;
		if (std::remove(filePath.c_str()) == 0) {
			LOG("[录像清理] 删除旧录像: %s", filePath.c_str());
			deleted++;
		}
		else {
			LOG("[录像清理] 删除失败: %s", filePath.c_str());
		}
	}

	if (deleted > 0) {
		LOG("[录像清理] 完成, 保留上限: %d, 当前: %zu, 删除: %d",
			maxFiles, files.size(), deleted);
	}
}

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
		if (len <= 0) return false;
		buf[len] = '\0';
		out = std::string(buf, len);
		return true;
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
	StreamNode* streamNode = nullptr;
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
			LOG("[RTSP-Server] Client %s disconnected or timeout", clientIp.c_str());
			break;
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
				cfg.source_url = "";  // 没有源，纯接收端
				cfg.target_url = "";
				cfg.retry_interval = 3000;
				cfg.max_retries = 0;
				cfg.rtp_timeout = 10000;

				auto node = std::make_unique<StreamNode>();
				// 直接设置 pull_session_ 信息（跳过 doStreamPull）
				node->config_ = cfg;
				node->pull_session_ = videoInfo;
				node->pull_session_.session_type_ = StreamNode::SERVER_PULL;
				node->isPulling_ = true;  // 标记为"有流数据"，使 DESCRIBE 不会等待
				node->running_ = true;
				node->state_ = StreamNode::State::PLAYING;

				std::lock_guard<std::mutex> lock(nodeLock_);
				m_mapStreamNodes[tag] = std::move(node);
				streamNode = m_mapStreamNodes[tag].get();
				LOG("[RTSP-Server] Created new StreamNode for push tag=%s, codec=%s, pt=%d",
					tag.c_str(), videoInfo.codec.c_str(), videoInfo.payload_type);
			}
			else {
				// 已存在的节点，更新编码信息
				streamNode->pull_session_ = videoInfo;
				streamNode->pull_session_.session_type_ = StreamNode::SERVER_PULL;
				streamNode->isPulling_ = true;
			}

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
			std::string path = extractPathFromUrl(url);

			// 尝试通过路径查找 stream（路径格式: /tag）
			streamNode = findStreamByRtspPath(path);

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
				// ---- 推流模式 SETUP ----
				rtspSession.client_rtp_port = clientRtpPort;
				rtspSession.client_rtcp_port = clientRtcpPort;

				if (isTcpTransport && interleavedRtp >= 0) {
					// ---- TCP interleaved 模式：RTP 数据通过 RTSP TCP 连接传输 ----
					LOG("[RTSP-Server] Push SETUP TCP interleaved: tag=%s, channels=%d-%d",
						streamTag.c_str(), interleavedRtp, interleavedRtcp);

					// 创建推流接收会话（使用 TCP 连接接收 RTP interleaved 数据）
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

				// 创建推流接收会话
				pushSession = std::make_shared<RtspRecvSession>();
				pushSession->rtp_sock = rtpSock;
				pushSession->rtcp_sock = rtcpSock;
				pushSession->tcp_sock = clientSock;  // 保存 RTSP 控制连接，RTP 超时时可关闭
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
				LOG("[RTSP-Server] Push SETUP: tag=%s, client=%s:%d-%d, server=%d-%d",
					streamTag.c_str(), clientIp.c_str(), clientRtpPort, clientRtcpPort,
					serverRtpPort, serverRtcpPort);
				}
			}
			else {
				// ---- 拉流模式 SETUP ----
				// 复制流信息，设置会话参数
				rtspSession = streamNode->pull_session_;
				rtspSession.session_type_ = StreamNode::SERVER_PULL;
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
			streamNode->client_sessions_mutex_.lock();
			streamNode->client_sessions_.push_back(sessionPtr);
			streamNode->client_sessions_mutex_.unlock();

			LOG("[RTSP-Server] Stream %s started playing to %s:%d (RTP port %d)",
				streamTag.c_str(), clientIp.c_str(), rtspSession.client_rtp_port, rtspSession.server_rtp_port);

			// TCP interleaved 拉流：去掉 recv 超时，保持连接等待 TEARDOWN
			if (rtspSession.transport_mode == StreamNode::TransportMode::TCP) {
#ifdef _WIN32
				int timeout_infinite = 0;  // 0 = 无限等待
				setsockopt(static_cast<SOCKET>(clientSock), SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&timeout_infinite, sizeof(timeout_infinite));
#else
				struct timeval tv_inf = {0, 0};
				setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&tv_inf, sizeof(tv_inf));
#endif
				LOG("[RTSP-Server] TCP interleaved pull: removed recv timeout for %s", clientIp.c_str());
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
				struct timeval tv_inf = {0, 0};
				setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&tv_inf, sizeof(tv_inf));
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
				struct timeval tv_inf = {0, 0};
				setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO,
					(const char*)&tv_inf, sizeof(tv_inf));
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
				streamNode->client_sessions_mutex_.lock();
				auto& sessions = streamNode->client_sessions_;
				sessions.erase(
					std::remove_if(sessions.begin(), sessions.end(),
						[&](const std::shared_ptr<StreamNode::STREAM_SESSION>& s) {
							return s->tcp_socket == clientSock;
						}),
					sessions.end());
				streamNode->client_sessions_mutex_.unlock();
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
		streamNode->client_sessions_mutex_.lock();
		auto& sessions = streamNode->client_sessions_;
		sessions.erase(
			std::remove_if(sessions.begin(), sessions.end(),
				[&](const std::shared_ptr<StreamNode::STREAM_SESSION>& s) {
					return s->tcp_socket == clientSock;
				}),
			sessions.end());
		streamNode->client_sessions_mutex_.unlock();
		LOG("[RTSP-Server] TCP interleaved pull session cleaned for %s", clientIp.c_str());
	}

	// 清理：关闭客户端 socket
#ifdef _WIN32
	closesocket(static_cast<SOCKET>(clientSock));
#else
	close(clientSock);
#endif
	LOG("[RTSP-Server] Client handler finished for %s", clientIp.c_str());
}

std::string StreamServer::buildSdpForStream(StreamNode* node) {
	const auto& si = node->pull_session_;
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

StreamNode* StreamServer::findStreamByRtspPath(const std::string& path) {
	// 路径格式: /tag → 去除前导 / 得到 tag
	std::string tag = path;
	if (!tag.empty() && tag[0] == '/') {
		tag = tag.substr(1);
	}

	// 先精确匹配 tag
	StreamNode* node = getStreamNode(tag);
	if (node) return node;

	// 再尝试在 url_id map 中查找
	std::lock_guard<std::mutex> lock(nodeLock_url_);
	for (const auto& pair : m_mapStreamNodes_urlID) {
		if (pair.second && pair.second->config_.tag == tag) {
			return pair.second.get();
		}
	}

	// 遍历 m_mapStreamNodes 找匹配
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		for (const auto& pair : m_mapStreamNodes) {
			if (pair.second && pair.second->config_.tag == tag) {
				return pair.second.get();
			}
		}
	}

	return nullptr;
}

// ============================================================================
// RTSP 推流接收：TCP interleaved 模式 — 从 RTSP TCP 连接读取 $channel length RTP_data
// ============================================================================

void StreamServer::rtpTcpRecvLoop(StreamNode::SocketHandle tcpSock,
	std::shared_ptr<RtspRecvSession> session, StreamNode* streamNode) {
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
				if (streamNode->pull_session_.video_ssrc == 0 && packet.ssrc != 0) {
					streamNode->pull_session_.video_ssrc = packet.ssrc;
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

	StreamNode* streamNode = nullptr;
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		auto it = m_mapStreamNodes.find(session->tag);
		if (it != m_mapStreamNodes.end()) {
			streamNode = it->second.get();
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
			// 解析 RTP 包
			auto pPkt = std::make_shared<StreamNode::RTPPacket>();
			StreamNode::RTPPacket& packet = *pPkt;

			if (packet.parse(buffer.data(), received)) {
				// 更新 SSRC
				if (streamNode->pull_session_.video_ssrc == 0 && packet.ssrc != 0) {
					streamNode->pull_session_.video_ssrc = packet.ssrc;
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
								uint32_t _clock = (streamNode->pull_session_.clock_rate > 0) ?
									static_cast<uint32_t>(streamNode->pull_session_.clock_rate) : 90000u;
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
					closesocket(static_cast<SOCKET>(session->tcp_sock));
#else
					shutdown(session->tcp_sock, SHUT_RDWR);
					close(session->tcp_sock);
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
			closesocket(static_cast<SOCKET>(session->tcp_sock));
#else
			shutdown(session->tcp_sock, SHUT_RDWR);
			close(session->tcp_sock);
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