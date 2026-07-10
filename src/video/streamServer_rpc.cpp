#include "pch.h"
#include "streamServer.h"
#include "logger.h"

// ============================================================================
// RPC 处理
// ============================================================================

json getStreamInfo(shared_ptr<StreamNode> rc) {
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
	json clientSession;
	std::lock_guard<std::mutex> lock(rc->session_list_client_pull_mutex_);
	for (const auto& pair : rc->session_list_client_pull_)
	{

	}

	return jSi;
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
	else if (method == "serveLocalFile") {
		rpc_serveLocalFile(params, rpcResp, session);
	}
	else {
		bHandled = false;
	}

	return bHandled;
}

bool StreamServer::rpc_startStreamNode(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string srcUrl, destUrl,streamUrl;
	yyjson_val* yyv = yyjson_obj_get(params, "srcUrl");
	if (yyv)
		srcUrl = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params,"destUrl");
	if(yyv)
		 destUrl = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "streamUrl");
	if (yyv)
		streamUrl = yyjson_get_str(yyv);

	{
		std::shared_ptr<StreamNode> p = getStreamNodeBySrcUrl(srcUrl);
		if (p) {
			LOG("[流媒体] StreamNode 已在运行 for src url: %s", srcUrl.c_str());
			return true;
		}
	}

	// 2. 创建新的 StreamNode
	auto sn = std::make_unique<StreamNode>();

	// 设置回调
	sn->setFrameCallback([](const uint8_t* data, size_t size, uint32_t timestamp) {
		// 可以在这里处理帧，例如存档或分析
		});
	sn->setStatusCallback([](StreamNode::State state, const std::string& msg) {
		LOG("[StreamNode] Status: %d - %s", static_cast<int>(state), msg.c_str());
		});
	sn->setErrorCallback([](const std::string& error, int code) {
		LOG("[StreamNode] Error (%d): %s", code, error.c_str());
		});
	// 3. 配置 sn

	// 3. 配置 sn
	StreamNode::Config config;
	config.source_url = srcUrl; // 源地址
	// 提取用户名和密码
	bool isSuccess = sn->extractRtspAuthInfo(config);

	config.target_url = destUrl;
	config.retry_interval = 3000;
	config.max_retries = 0; // 无限重试
	config.rtp_timeout = 10000;

	// 4. 启动 sn
	LOG("[流媒体] 启动 StreamNode (内置模式)，源: %s, 目标: %s",
		config.source_url.c_str(), config.target_url.c_str());

	if (sn->start(config)) {
		std::lock_guard<std::mutex> lock(nodeLock_);
		m_mapStreamNodes[streamUrl] = std::move(sn);
	}
	else {
		LOG("[流媒体] 启动 StreamNode 失败 for srcUrl: %s", srcUrl.c_str());
	}

	rpcResp.result = RPC_OK;
	return true;
}

bool StreamServer::rpc_serveLocalFile(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string filePath, url;
	yyjson_val* yyv = yyjson_obj_get(params_obj, "filePath");
	if (yyv)
		filePath = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params_obj, "url");
	if (yyv)
		url = yyjson_get_str(yyv);

	if (filePath.empty() || url.empty()) {
		LOG("[RPC] serveLocalFile: missing filePath or url");
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing filePath or url");
		return false;
	}

	LOG("[RPC] serveLocalFile: filePath=%s, url=%s", filePath.c_str(), url.c_str());

	bool ok = serveLocalStreamFile(filePath, url);
	if (ok) {
		rpcResp.result = RPC_OK;
	}
	else {
		LOG("[RPC] serveLocalFile: failed for filePath=%s", filePath.c_str());
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::OS_fileNotExist,
			"failed to serve local file, check file path and format");
	}

	return true;
}

bool StreamServer::rpc_playWebRtc(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag;
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);

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
	auto rc = getStreamNodeByTag(tag);
	if (rc) {
		StreamNode::STREAM_SESSION si = rc->session_origin_pull_;
		si.session_type_ = StreamNode::CLIENT_PULL;
		si.client_rtp_port = clientRtpPort;
		si.remote_host = session.remoteIP;
		rc->createUDPServerSocket(si);

		std::string serverIp = session.localIP;
		if (serverIp.empty()) serverIp = "0.0.0.0";
		// SDP 规范要求 c= 行和 candidate 行使用数字 IP 地址，
		// 浏览器严格解析无法接受主机名（如 "localhost"），
		// 因此如果 localIP 不是数字 IP，则尝试解析为主机名
		else {
			bool isNumericIp = true;
			for (char c : serverIp) {
				if (!((c >= '0' && c <= '9') || c == '.')) {
					isNumericIp = false;
					break;
				}
			}
			if (!isNumericIp) {
				struct addrinfo hints = {}, *res = nullptr;
				hints.ai_family = AF_INET;
				hints.ai_socktype = SOCK_DGRAM;
				std::string hostname = serverIp;
				if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) == 0 && res) {
					char ipstr[INET_ADDRSTRLEN] = {};
					inet_ntop(AF_INET,
						&((struct sockaddr_in*)res->ai_addr)->sin_addr,
						ipstr, sizeof(ipstr));
					serverIp = ipstr;
					freeaddrinfo(res);
					LOG("[WebRTC] resolved hostname '%s' to IP '%s'",
						hostname.c_str(), serverIp.c_str());
				}
			}
		}

		std::string fingerprint = m_dtlsFingerprint.empty()
			? "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
			  "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00"
			: m_dtlsFingerprint;

		rc->buildWebRTCSdpAnswer(si, serverIp, fingerprint);
		LOG("[WebRTC] SDP Answer:\n%s", si.sdp.c_str());

		auto sessionPtr = std::make_shared<StreamNode::STREAM_SESSION>(si);
		rc->session_list_client_pull_mutex_.lock();
		rc->session_list_client_pull_.push_back(sessionPtr);
		rc->session_list_client_pull_mutex_.unlock();

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
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "stream node of specified tag not found");
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
	std::shared_ptr<StreamNode> rc = nullptr;
	if (!ip.empty()) {
		rc = getStreamNodeByIp(ip);
	} else if (!tag.empty()) {
		rc = getStreamNodeByTag(tag);
	}
	if (rc) {
		std::lock_guard<std::recursive_mutex> lock(rc->rec_mutex_);
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
	std::shared_ptr<StreamNode> rc = nullptr;
	if (!ip.empty()) {
		rc = getStreamNodeByIp(ip);
	} else if (!tag.empty()) {
		rc = getStreamNodeByTag(tag);
	}
	if (rc) {
		std::lock_guard<std::recursive_mutex> lock(rc->rec_mutex_);
		if (rc->rec_ctrl_.recording == true)
		{
			rc->rec_ctrl_.recording = false;
			rc->flushRecordBuffer();

			auto now = std::chrono::steady_clock::now();
			int duration = static_cast<int>(
				std::chrono::duration_cast<std::chrono::seconds>(
					now - rc->rec_ctrl_.startTime).count());
			std::string filePath = rc->rec_ctrl_.path;
			size_t pos = filePath.find_last_of("/\\");
			std::string fileName = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
			std::string fileUrl = "/db/record/" + fileName;

			json j;
			j["fileUrl"] = fileUrl;
			j["duration"] = duration;
			j["preSeconds"] = rc->rec_ctrl_.preSeconds;
			rpcResp.result = j.dump();

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
	yyjson_val* yyv = yyjson_obj_get(params, "url");
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

	if (streamId != "*") {
		shared_ptr<StreamNode> rc = getStreamNodeByTag(streamId);
		if (rc == nullptr)
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_NO_STREAM_SRC, "no stream src of this tag");
			return true;
		}
		json jsn = getStreamInfo(rc);
		rpcResp.result = jsn.dump();
		return true;
	}
	else {
		json jsnList = json::array();
		std::lock_guard<std::mutex> lock(nodeLock_);
		for (const auto& pair : m_mapStreamNodes)
		{
			json j = getStreamInfo(pair.second);
			jsnList.push_back(j);
		}
		rpcResp.result = jsnList.dump();
		return true;
	}
}

bool StreamServer::rpc_getStreamNodeList(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	const auto& mapStreamNodes = m_mapStreamNodes;

	yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* root_arr = yyjson_mut_arr(doc);
	yyjson_mut_doc_set_root(doc, root_arr);

	for (const auto& pair : mapStreamNodes) {
		const std::string& relay_key = pair.first;
		const std::shared_ptr<StreamNode>& relay_ptr = pair.second;

		if (!relay_ptr) {
			continue;
		}

		const StreamNode::Statistics& stats = relay_ptr->getStatistics();

		yyjson_mut_val* relay_obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, relay_obj, "relay_key", relay_key.c_str());

		yyjson_mut_obj_add_uint(doc, relay_obj, "frames_received", stats.frames_received);
		yyjson_mut_obj_add_uint(doc, relay_obj, "frames_forwarded", stats.frames_forwarded);
		yyjson_mut_obj_add_uint(doc, relay_obj, "bytes_received", stats.bytes_received);
		yyjson_mut_obj_add_uint(doc, relay_obj, "bytes_forwarded", stats.bytes_forwarded);
		yyjson_mut_obj_add_uint(doc, relay_obj, "reconnect_count", stats.reconnect_count);
		yyjson_mut_obj_add_uint(doc, relay_obj, "errors", stats.errors);

		using namespace std::chrono;
		uint64_t start_time_ms = duration_cast<seconds>(stats.start_time.time_since_epoch()).count();
		uint64_t last_frame_time_ms = duration_cast<seconds>(stats.last_frame_time.time_since_epoch()).count();
		yyjson_mut_obj_add_uint(doc, relay_obj, "start_time_s", start_time_ms);
		yyjson_mut_obj_add_uint(doc, relay_obj, "last_frame_time_s", last_frame_time_ms);

		yyjson_mut_obj_add_real(doc, relay_obj, "fps", stats.fps);
		yyjson_mut_obj_add_real(doc, relay_obj, "bitrate_kbps", stats.bitrate);

		yyjson_mut_arr_add_val(root_arr, relay_obj);
	}

	size_t len;
	char* json_str = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);

	std::string result;
	if (json_str) {
		rpcResp.result = json_str;
		free(json_str);
	}

	yyjson_mut_doc_free(doc);
	return true;
}
