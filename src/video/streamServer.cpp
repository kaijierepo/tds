#include "pch.h"
#include "streamServer.h"
#include "dtls_transport.h"
#include "logger.h"
#include <sstream>
#include <fstream>
#include <random>
#include <cstdio>
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
		si.session_type_ = StreamNode::SERVER_SEND;
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

		sdp << "a=rtcp-mux\r\n";                          // RTCP 复用 RTP 端口（匹配浏览器 rtcp-mux）
		sdp << "a=rtcp-rsize\r\n";                        // 精简 RTCP（匹配浏览器 rtcp-rsize）
		sdp << "a=sendonly\r\n";                         // 服务端仅发送视频（匹配浏览器 recvonly）
		sdp << "a=setup:passive\r\n";                     // 服务端作为 DTLS server，等待浏览器发起握手
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

		sleep(1000);
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
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "camera_ip");
	if (yyv)
		ip = yyjson_get_str(yyv);
	int preTime;
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
		rc->rec_ctrl_.fu_a_buffer_.clear();
		rc->rec_ctrl_.firstWrite = true;
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
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "camera_ip");
	if (yyv)
		ip = yyjson_get_str(yyv);
	StreamNode* rc = nullptr;
	if (!ip.empty()) {
		rc = getStreamNodeByIp(ip);
	} else if (!tag.empty()) {
		rc = getStreamNode(tag);
	}
	if (rc) {
		if (rc->rec_ctrl_.recording == true)
		{
			rc->rec_ctrl_.recording = false;

			// 计算 duration（秒）
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