#include "pch.h"
#include "streamServer.h"
#include "streamSession_webrtc.h"
#include "mp4Writer.h"
#include "logger.h"
#include <thread>

string toTimeStr(std::chrono::system_clock::time_point tp) {
	std::time_t tt = std::chrono::system_clock::to_time_t(tp);
	// 转换为 tm 结构（本地时间）
	std::tm local_tm;
#ifdef _WIN32
	localtime_s(&local_tm, &tt);
#else
	localtime_r(&tt, &local_tm);
#endif
	char buffer[24] = { 0 }; // YYYY-MM-DD HH:MM:SS.xxx 共23字符 + '\0'
	std::snprintf(buffer, sizeof(buffer),
		"%04d-%02d-%02d %02d:%02d:%02d.%03lld",
		local_tm.tm_year + 1900,
		local_tm.tm_mon + 1,
		local_tm.tm_mday,
		local_tm.tm_hour,
		local_tm.tm_min,
		local_tm.tm_sec,
		0);
	string sTime = buffer;
	return sTime;
}

// ============================================================================
// RPC 处理
// ============================================================================

// 将 SESSION_STATE 转为字符串
static const char* sessionStateStr(SESSION_STATE st) {
	switch (st) {
	case SESSION_IDLE:         return "idle";
	case SESSION_CONNECTING:   return "connecting";
	case SESSION_HANDSHAKING:  return "handshaking";
	case SESSION_STREAMING:    return "streaming";
	case SESSION_ERROR:        return "error";
	case SESSION_RECONNECTING: return "reconnecting";
	default:                   return "unknown";
	}
}

json getStreamInfo(shared_ptr<StreamNode> sn) {
	json jSi;
	jSi["streamUrl"] = sn->config_.streamUrl;
	jSi["tag"] = sn->config_.tag;

	// 源拉流信息
	json jOrigin;
	jOrigin["url"] = sn->session_origin_pull_.server_url_;
	jOrigin["state"] = sessionStateStr(sn->session_origin_pull_.state_);
	jOrigin["transport"] = (sn->session_origin_pull_.transport_mode == TransportMode::UDP) ? "udp" : "tcp";
	jOrigin["codec"] = sn->session_origin_pull_.codec;
	jOrigin["lastError"] = sn->session_origin_pull_.last_error_;
	jSi["originPull"] = jOrigin;

	// 转推流信息
	json jRelay;
	jRelay["url"] = sn->session_relay_push_.server_url_;
	jRelay["state"] = sessionStateStr(sn->session_relay_push_.state_);
	jRelay["transport"] = (sn->session_relay_push_.transport_mode == TransportMode::UDP) ? "udp" : "tcp";
	jRelay["openTime"] = toTimeStr(sn->session_relay_push_.open_time_);
	jRelay["bytesSended"] = sn->session_relay_push_.rtpBytesSended;
	jRelay["lastError"] = sn->session_relay_push_.last_error_;
	jSi["relayPush"] = jRelay;

	json jRtpBuffer;
	jRtpBuffer["size"] = sn->rtp_buffer_.size();
	jRtpBuffer["maxSeconds"] = sn->rtp_buffer_max_seconds_;
	jRtpBuffer["bufferedSeconds"] = sn->getBufferedSeconds();
	jSi["rtpBuffer"] = jRtpBuffer;
	json jRecCtrl;
	jRecCtrl["recording"] = sn->rec_ctrl_.recording;
	jRecCtrl["preSeconds"] = sn->rec_ctrl_.preSeconds;
	jSi["recordCtrl"] = jRecCtrl;
	json jStatis;
	StreamNode::Statistics statis = sn->getStatistics();
	jStatis["bitrate"] = statis.bitrate;
	jStatis["fps"] = statis.fps;
	jStatis["frameReceived"] = statis.frames_received;
	jStatis["frameForwarded"] = statis.frames_forwarded;
	jStatis["bytesReceived"] = statis.bytes_received;
	jStatis["bytesForwarded"] = statis.bytes_forwarded;
	jStatis["totalBytesReceived"] = statis.bytes_received;
	jStatis["reconnectCount"] = statis.reconnect_count;
	jSi["statis"] = jStatis;
	jSi["openTime"] = toTimeStr(sn->open_time_);
	
	// 客户端会话列表（每 session 包含自身状态）
	json clientSession = json::array();
	std::lock_guard<std::mutex> lock(sn->session_list_client_pull_mutex_);
	for (const auto& session : sn->session_list_client_pull_)
	{
		json j;
		j["sessionType"] = session->getTypeDesc();
		j["state"] = sessionStateStr(session->state_);
		j["stateDesc"] = session->getSessionStateDesc();
		j["clientRtpPort"] = session->client_rtp_port;
		j["clientRtcpPort"] = session->client_rtcp_port;
		j["serverRtpPort"] = session->server_rtp_port;
		j["serverRtcpPort"] = session->server_rtcp_port;
		
		j["lastStunBindReqTime"] = toTimeStr(session->last_stun_bind_req_time);
		j["openTime"] = toTimeStr(session->open_time_);
		j["bytesSended"] = session->rtpBytesSended;
		if (session->session_type_ == STREAM_SESSION_TYPE::CLIENT_WEBRTC_PULL) {
			if (session->dtls_transport_) {
				SessionDtlsState* dtls = session->dtls_transport_.get();
				if (dtls->dtls.isHandshakeDone()) {
					const char* dtlsCipher = dtls->dtls.getDtlsCipherName();
					const char* srtpProfile = dtls->dtls.getSrtpProfileName();
					if (dtlsCipher) j["dtlsCipher"] = dtlsCipher;
					if (srtpProfile) j["srtpCipher"] = srtpProfile;
				}
			}
		}

		clientSession.push_back(j);
	}
	jSi["clientSessions"] = clientSession;

	return jSi;
}

bool StreamServer::handleRpc(std::string method, yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session) {
	bool bHandled = true;

	if (method == "openStream") {
		rpc_openStream(params, rpcResp, session);
	}
	else if (method == "closeStream") {
		//if (yyv)
		//	op.streamUrl = yyjson_get_str(yyv);
		//yyv = yyjson_obj_get(params, "tag");
		//bool opend = streamSrv.closeStream(tag);

		//if (opend)
		//	rpcResp.result = RPC_OK;
		//else
		//	rpcResp.error = RPC_FAIL;
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
	else if (method == "getRecordList") {
		rpc_getRecordList(params, rpcResp, session);
	}
	else if (method == "serveLocalFile") {
		rpc_serveLocalFile(params, rpcResp, session);
	}
	else if (method == "setStream") {
		rpc_setStream(params, rpcResp, session);
	}
	else if (method == "remux") {
		rpc_remux(params, rpcResp, session);
	}
	else {
		bHandled = false;
	}

	return bHandled;
}

bool StreamServer::rpc_openStream(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	STREAM_OPEN_PARAM op;
	yyjson_val* yyv = yyjson_obj_get(params, "originUrl");
	if (yyv)
		op.originPullUrl = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params,"relayUrl");
	if(yyv)
		op.relayPushUrl = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "streamUrl");
	if (yyv)
		op.streamUrl = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "tag");
	if(yyv)
		op.tag = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "pushTo"); //兼容 pushTo 和 pushToTag
	if (yyv)
		op.pushToTag = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "pushToTag");
	if (yyv)
		op.pushToTag = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "pushToIP");
	if (yyv)
		op.pushToIP = yyjson_get_str(yyv);

	if (op.pushToIP == "") {
		op.pushToIP = session.remoteIP;
	}

	if (op.pushToIP != "") {
		op.relayPushUrl = "rtsp://" + op.pushToIP + "/stream/" + op.pushToTag;
	}

	if (openStream(op) != nullptr) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = RPC_FAIL;
	}

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

bool StreamServer::rpc_remux(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string src, target;
	yyjson_val* yyv = yyjson_obj_get(params, "srcFile");
	if (yyv)
		src = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "targetFile");
	if (yyv)
		target = yyjson_get_str(yyv);

	src = m_recordPath + src;
	target = m_recordPath + target;

	bool ok = mp4::convertH264toMP4(src, target);

	if (ok) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = RPC_FAIL;
	}
	return true;
}

bool StreamServer::rpc_playWebRtc(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag,streamUrl;
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);
	yyv = yyjson_obj_get(params, "streamUrl");
	if (yyv)
		streamUrl = yyjson_get_str(yyv);

	string sdpOffer;
	yyv = yyjson_obj_get(params, "sdpOffer");
	if (yyv)
		sdpOffer = yyjson_get_str(yyv);
	LOG("[WebRTC] playWebRtc tag=%s,streamUrl=%s, sdpOffer=%zu bytes", tag.c_str(),streamUrl.c_str(), sdpOffer.size());

	int clientRtpPort = 0;
	yyv = yyjson_obj_get(params, "clientRtpPort");
	if (yyjson_is_int(yyv)) {
		clientRtpPort = yyjson_get_int(yyv);
	}
	std::shared_ptr<StreamNode> sn;
	if (streamUrl != "")
		sn = getStreamNodeByStreamUrl(streamUrl);
	else if(tag != "")
		sn = getStreamNodeByTag(tag);

	if (sn) {
		// 按需启动拉流：如果 origin pull session 处于 idle 或 error 状态，启动拉流
		if (sn->session_origin_pull_.state_ == SESSION_STATE::SESSION_IDLE || sn->session_origin_pull_.state_ == SESSION_STATE::SESSION_ERROR) {
			LOG("[WebRTC] 按需启动拉流 tag=%s, state=%d", sn->config_.tag.c_str(), (int)sn->session_origin_pull_.state_);
			sn->run(sn->config_);

			// 等待拉流准备好（15秒超时，200ms轮询）
			int waitCount = 0;
			while (sn->isPulling_ == false && waitCount < 75) {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				waitCount++;
			}
			if (!sn->isPulling_) {
				LOG("[WebRTC] 按需拉流超时 tag=%s", sn->config_.tag.c_str());
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "stream pull timeout");
				return true;
			}
		}

		STREAM_SESSION si;
		si.copyStreamInfoFrom(sn->session_origin_pull_);
		si.session_type_ = CLIENT_WEBRTC_PULL;
		si.client_rtp_port = clientRtpPort;
		si.remote_host = session.remoteIP;
		// 从浏览器 Offer 中解析 H264 payload type（避免 PT 冲突）
		int h264PT = parseH264PTFromOffer(sdpOffer);
		if (h264PT > 0) {
			si.payload_type = h264PT;
			LOG("[WebRTC] using H264 PT=%d from Offer", h264PT);
		}
		si.createUDPConsecutiveSockets(true);

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

		buildWebRTCSdpAnswer(si, serverIp, fingerprint);
		LOG("[WebRTC] SDP Answer:\n%s", si.sdp.c_str());

		auto sessionPtr = std::make_shared<STREAM_SESSION>();
		sessionPtr->copyClientSessionFrom(si);
		sessionPtr->open_time_ = std::chrono::system_clock::now();
		sn->session_list_client_pull_mutex_.lock();
		sn->session_list_client_pull_.push_back(sessionPtr);
		sn->session_list_client_pull_mutex_.unlock();

		sn->startRtcSessionHandleThread(sessionPtr);

#ifdef _WIN32
		Sleep(1000);
#else
		usleep(1000 * 1000);
#endif
		json j;
		j["tag"] = sn->config_.tag;
		j["streamUrl"] = sn->config_.streamUrl;
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
		if (!rc->running_) {
			bool ok = rc->run(rc->config_);
			LOG("[StreamSrv]streamNode not running while startRecord,run streamNode %s, tag: %s, streamUrl: %s",ok?"success":"fail", tag.c_str(), rc->config_.streamUrl.c_str());
			if (!ok) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "run streamNode fail");
				return true;
			}
		}

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
			// 启动独立 I/O 线程，将磁盘写入与实时收包线程解耦
			rc->record_io_running_ = true;
			rc->record_io_thread_ = std::thread(&StreamNode::threadRec_h264File, rc.get());
			LOG("[录像] 开始录像 tag=%s, path=%s", rc->config_.tag.c_str(), rc->rec_ctrl_.path.c_str());
			rpcResp.result = RPC_OK;
		}
		else
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "record is already started");
			return true;
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
		bool wasRecording = false;
		{
			std::lock_guard<std::recursive_mutex> lock(rc->rec_mutex_);
			if (rc->rec_ctrl_.recording == true)
			{
				rc->rec_ctrl_.recording = false;
				wasRecording = true;
			}
		}
		if (wasRecording)
		{
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

			// 异步将 .h264 转为 .mp4（不阻塞 RPC 响应）
			std::string mp4Path = filePath.substr(0, filePath.size() - 5) + ".mp4";
			std::thread([h264Path = filePath, mp4Path]() {
				mp4::convertH264toMP4(h264Path, mp4Path);
			}).detach();

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
#ifdef _WIN32
	// Windows 下 std::remove 走系统代码页，UTF-8 中文路径会失败，需转宽字符调用 _wremove
	std::wstring wPath;
	int wlen = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
	if (wlen > 0) {
		wPath.resize(wlen - 1);
		MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wPath[0], wlen);
	}
	if (!wPath.empty() && _wremove(wPath.c_str()) == 0) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::OS_fileNotExist, "file not exist or failed to delete");
	}
#else
	if (std::remove(filePath.c_str()) == 0) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::OS_fileNotExist, "file not exist or failed to delete");
	}
#endif
	LOG("[HTTP API]removeRecordFile, fileUrl: %s", fileUrl.c_str());
	return true;
}

bool StreamServer::rpc_getRecordList(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag;
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);
	if (tag.empty()) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param missing, tag is required");
		return true;
	}

	std::string recordDir = tds->conf->dbPath + "/record/";
	json records = json::array();

	namespace fs = std::filesystem;
#ifdef _WIN32
	// 中文 Windows 上 filesystem 用系统编码（GBK）解释窄字符串，
	// dbPath 是 UTF-8，需要转成 UTF-16 宽字符串路径
	fs::path recordDirPath(str::utf8_to_utf16(recordDir));
#else
	fs::path recordDirPath(recordDir);
#endif
	try {
		if (!fs::exists(recordDirPath) || !fs::is_directory(recordDirPath)) {
			rpcResp.result = records.dump();
			return true;
		}

		std::string prefix = tag + "_";
		for (const auto& entry : fs::directory_iterator(recordDirPath)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".h264")
				continue;
			// Windows 上 filesystem::path::string() 返回系统编码（中文 Windows 为 GBK），
			// 而 tag 来自 JSON 是 UTF-8，二者需统一为 UTF-8 才能正确比对前缀
			std::string fname = str::gb_to_utf8(entry.path().filename().string());
			if (fname.find(prefix) != 0)
				continue;

			// parse time from filename: tag_YYYYMMDD_HHMMSS.h264
			std::string tsStr;
			size_t pos = fname.find("_", prefix.length());
			if (pos != std::string::npos) {
				tsStr = fname.substr(prefix.length(), pos - prefix.length());
			} else {
				tsStr = fname.substr(prefix.length(), fname.length() - 5); // strip .h264
			}
			// format: YYYYMMDD_HHMMSS -> "YYYY-MM-DD HH:MM:SS"
			std::string formattedTime;
			if (tsStr.length() >= 15 && tsStr[8] == '_') {
				formattedTime = tsStr.substr(0, 4) + "-" + tsStr.substr(4, 2) + "-" + tsStr.substr(6, 2)
					+ " " + tsStr.substr(9, 2) + ":" + tsStr.substr(11, 2) + ":" + tsStr.substr(13, 2);
			} else {
				formattedTime = tsStr;
			}

			std::string fileUrl = "/db/record/" + fname;
			int fileSize = static_cast<int>(entry.file_size());

			json jRec;
			jRec["tag"] = tag;
			jRec["time"] = formattedTime;
			jRec["duration"] = 0;  // unknown from filename alone
			jRec["preSeconds"] = 0;
			jRec["fileUrl"] = fileUrl;
			jRec["fileSize"] = fileSize;
			records.push_back(jRec);
		}
	} catch (const std::exception& e) {
		LOG("[录像列表] 扫描目录失败: %s, 错误: %s", recordDir.c_str(), e.what());
	}

	// sort by time descending (newest first)
	std::sort(records.begin(), records.end(), [](const json& a, const json& b) {
		return a["time"].get<std::string>() > b["time"].get<std::string>();
	});

	rpcResp.result = records.dump();
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
		// 将 steady_clock 的时间点转换为 system_clock 的 Unix 时间戳（秒）
		auto now_steady = steady_clock::now();
		auto now_sys = system_clock::now();
		auto elapsed_start = now_steady - stats.start_time;
		auto start_time_sys = now_sys - duration_cast<system_clock::duration>(elapsed_start);
		uint64_t start_time_sec = duration_cast<seconds>(start_time_sys.time_since_epoch()).count();
		yyjson_mut_obj_add_uint(doc, relay_obj, "start_time_s", start_time_sec);

		auto elapsed_last = now_steady - stats.last_frame_time;
		auto last_frame_time_sys = now_sys - duration_cast<system_clock::duration>(elapsed_last);
		uint64_t last_frame_time_sec = duration_cast<seconds>(last_frame_time_sys.time_since_epoch()).count();
		yyjson_mut_obj_add_uint(doc, relay_obj, "last_frame_time_s", last_frame_time_sec);

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

bool StreamServer::rpc_setStream(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string tag;
	yyjson_val* yyv = yyjson_obj_get(params, "tag");
	if (yyv)
		tag = yyjson_get_str(yyv);

	string streamUrl;
	yyv = yyjson_obj_get(params, "streamUrl");
	if (yyv)
		streamUrl = yyjson_get_str(yyv);

	if (tag.empty() && streamUrl.empty()) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param missing, tag or streamUrl is required");
		return true;
	}

	shared_ptr<StreamNode> sn = nullptr;
	if (!tag.empty())
		sn = getStreamNodeByTag(tag);
	if (!sn && !streamUrl.empty())
		sn = getStreamNodeByStreamUrl(streamUrl);

	if (!sn) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "stream not found");
		return true;
	}

	string relayUrl;
	yyv = yyjson_obj_get(params, "relayUrl");
	if (yyv)
		relayUrl = yyjson_get_str(yyv);

	string relayTransport;
	yyv = yyjson_obj_get(params, "relayTransport");
	if (yyv) {
		string transport = yyjson_get_str(yyv);
		if (transport == "tcp" || transport == "udp") {
			relayTransport = transport;
		}
	}

	string originTransport;
	yyv = yyjson_obj_get(params, "originTransport");
	if (yyv) {
		string transport = yyjson_get_str(yyv);
		if (transport == "tcp" || transport == "udp") {
			originTransport = transport;
		}
	}

	bool changed = false;
	if (relayUrl != sn->session_relay_push_.server_url_) {
		LOG("[流媒体] setStream tag=%s relayUrl: %s -> %s",
			sn->config_.tag.c_str(), sn->session_relay_push_.server_url_.c_str(), relayUrl.c_str());
		sn->session_relay_push_.server_url_ = relayUrl;
		changed = true;
	}

	if (!relayTransport.empty()) {
		TransportMode newMode = (relayTransport == "udp") ? TransportMode::UDP : TransportMode::TCP;
		if (sn->session_relay_push_.transport_mode != newMode) {
			LOG("[流媒体] setStream tag=%s relayTransport: %d -> %d",
				sn->config_.tag.c_str(), (int)sn->session_relay_push_.transport_mode, (int)newMode);
			sn->session_relay_push_.transport_mode = newMode;
			changed = true;
		}
	}

	if (!originTransport.empty()) {
		TransportMode newMode = (originTransport == "udp") ? TransportMode::UDP : TransportMode::TCP;
		if (sn->session_origin_pull_.transport_mode != newMode) {
			LOG("[流媒体] setStream tag=%s originTransport: %d -> %d",
				sn->config_.tag.c_str(), (int)sn->session_origin_pull_.transport_mode, (int)newMode);
			sn->session_origin_pull_.transport_mode = newMode;
			changed = true;
		}
	}

	if (changed) {
		sn->run(sn->config_);
	}

	rpcResp.result = RPC_OK;
	return true;
}
