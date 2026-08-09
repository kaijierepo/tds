#include "pch.h"
#include "streamServer.h"
#include "dtls_transport.h"
#include "logger.h"
#include <sstream>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <filesystem>

StreamServer streamSrv;

void openAllStream() {
	std::lock_guard<std::mutex> lock(streamSrv.nodeLock_);
	for (auto& pair : streamSrv.m_mapStreamNodes) {
		std::shared_ptr<StreamNode> sn = pair.second;
		if (sn && sn->session_origin_.server_url_ != "") {
			sn->run(sn->config_);
		}
		LOG("[StreamSrv]持续拉流模式: streamUrl=%s, origin_pull_url=%s", pair.first.c_str(), sn->session_origin_.server_url_.c_str());
	}
}

bool StreamServer::run() {
	initDtlsCertificate();

	// 启动 RTSP 服务端（配置项: rtspServerPort，默认 0=不启动）
	int rtspPort = tds->conf->getInt("rtspPort", 554);
	if (rtspPort > 0 && rtspPort <= 65535) {
		startRtspServer(rtspPort);
	}

	// 默认加载 tds 同级目录下 rtsp 文件夹的 h264 文件作为流媒体源
	serveDefaultFolder("rtsp");

	if (m_alwaysOpenStream) {
		thread t_os(openAllStream);
		t_os.detach();
	}

	startIdleMonitor();

	return true;
}

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

std::shared_ptr<StreamNode> StreamServer::getStreamNodeByStreamUrl(std::string streamUrl)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	auto it = m_mapStreamNodes.find(streamUrl);
	if (it != m_mapStreamNodes.end()) {
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<StreamNode> StreamServer::getStreamNodeByTag(std::string tag)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	for (const auto& pair : m_mapStreamNodes)
	{
		if (pair.second &&
			pair.second->config_.tag == tag)
		{
			return pair.second;
		}
	}
	return nullptr;
}


std::shared_ptr<StreamNode> StreamServer::getStreamNodeByIp(const std::string& ip)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	for (const auto& pair : m_mapStreamNodes) 
	{
		if (pair.second &&
			pair.second->session_origin_.server_url_.find(ip) != std::string::npos) 
		{
			return pair.second;
		}
	}
	return nullptr;
}

std::shared_ptr<StreamNode> StreamServer::getStreamNodeBySrcUrl(const std::string& srcUrl)
{
	std::lock_guard<std::mutex> lock(nodeLock_);
	for (const auto& pair : m_mapStreamNodes)
	{
		if (pair.second &&
			pair.second->session_origin_.server_url_ == srcUrl)
		{
			return pair.second;
		}
	}
	return nullptr;
}



std::shared_ptr<StreamNode> StreamServer::createStream(const STREAM_OPEN_PARAM& op)
{
	std::string streamUrl = op.streamUrl;
	if (streamUrl == "" && op.tag != "")
		streamUrl = "/" + op.tag;

	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		if (m_mapStreamNodes.find(streamUrl) != m_mapStreamNodes.end()) {
			LOG("[StreamServer] createStream: streamUrl=%s 已存在，忽略", streamUrl.c_str());
			return nullptr;
		}
	}

	std::shared_ptr<StreamNode> sn = std::make_shared<StreamNode>();
	sn->setFrameCallback([](const uint8_t* data, size_t size, uint32_t timestamp) {

		});

	sn->setErrorCallback([](const std::string& error, int code) {
		LOG("[StreamNode] Error (%d): %s", code, error.c_str());
		});

	sn->session_origin_.server_url_ = op.originPullUrl;
	sn->extractRtspAuthInfo(sn->session_origin_);
	sn->session_relay_push_.server_url_ = op.relayPushUrl;
	sn->session_origin_.retry_interval_ = 3000;
	sn->session_origin_.max_retries_ = 0;
	sn->session_origin_.rtp_timeout_ = 10000;
	sn->config_.tag = op.tag;
	sn->config_.streamUrl = streamUrl;
	sn->session_origin_.tag_ = op.tag;
	sn->session_origin_.stream_url_ = streamUrl;
	sn->session_relay_push_.tag_ = op.tag;
	sn->session_relay_push_.stream_url_ = streamUrl;
	sn->config_.srcStreamFetch = op.srcStreamFetch != "" ? op.srcStreamFetch : "always";

	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		m_mapStreamNodes[streamUrl] = sn;
	}

	LOG("[StreamServer] createStream: tag=%s, streamUrl=%s, originUrl=%s",
		op.tag.c_str(), streamUrl.c_str(), op.originPullUrl.c_str());
	return sn;
}

std::shared_ptr<StreamNode> StreamServer::openStream(const STREAM_OPEN_PARAM& op)
{
	std::string streamUrl = op.streamUrl;
	if (streamUrl == "" && op.tag != "")
		streamUrl = "/" + op.tag;

	std::shared_ptr<StreamNode> sn;
	if (op.tag != "")
		sn = getStreamNodeByTag(op.tag);
	else if (op.originPullUrl != "")
		sn = getStreamNodeBySrcUrl(op.originPullUrl);

	if (sn) {
		bool confChanged = false;
		if (op.originPullUrl != "" && sn->session_origin_.server_url_ != op.originPullUrl) {
			LOG("[流媒体] 媒体源变更，原地址:%s, 新地址:%s",
				sn->session_origin_.server_url_.c_str(), op.originPullUrl.c_str());
			sn->session_origin_.server_url_ = op.originPullUrl;
			sn->extractRtspAuthInfo(sn->session_origin_);
			confChanged = true;
		}
		if (op.relayPushUrl != "" && sn->session_relay_push_.server_url_ != op.relayPushUrl) {
			LOG("[流媒体] 推流地址变更，原地址:%s, 新地址:%s",
				sn->session_relay_push_.server_url_.c_str(), op.relayPushUrl.c_str());
			sn->session_relay_push_.server_url_ = op.relayPushUrl;
			confChanged = true;
		}

		if (sn->running_ &&!confChanged) {
			LOG("[流媒体] 媒体源已打开，重复打开请求，参数未改变,忽略。 位号:%s, 源地址:%s，转发地址:%s",
				op.tag.c_str(),
				sn->session_origin_.server_url_.c_str(),
				sn->session_relay_push_.server_url_.c_str());
			return sn;
		}


		if (op.srcStreamFetch != "") {
			sn->config_.srcStreamFetch = op.srcStreamFetch;
		}
	}
	else {
		sn = createStream(op);
		if (!sn) {
			LOG("[StreamServer] openStream create fail, tag:%s", op.tag.c_str());
			return nullptr;
		}
	}

	if (!sn->run(sn->config_)) {
		LOG("[StreamServer] openStream start fail, tag:%s", op.tag.c_str());
		return nullptr;
	}

	LOG("[StreamServer] openStream success (new), tag:%s, streamUrl:%s, originUrl:%s",
		op.tag.c_str(), streamUrl.c_str(), op.originPullUrl.c_str());
	return sn;
}

bool StreamServer::closeStream(string tag)
{
	// map key 是 streamUrl（如 "/tag"），而参数是纯 tag，
	// 需要通过遍历匹配 config_.tag 来查找
	std::shared_ptr<StreamNode> sn = getStreamNodeByTag(tag);
	if (sn) {
		{
			std::lock_guard<std::mutex> lock(nodeLock_);
			auto it = m_mapStreamNodes.find(sn->config_.streamUrl);
			if (it != m_mapStreamNodes.end()) {
				m_mapStreamNodes.erase(it);
				LOG("[流媒体] 正在停止 StreamNode for tag: %s", tag.c_str());
			}
		}

		// 在锁外停止 relay，避免潜在死锁
		sn->stop();
		LOG("[流媒体] StreamNode 已停止 for tag: %s", tag.c_str());
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
// 按需拉流 idle 监控
// ============================================================================
void StreamServer::setIdleTimeout(int secs) {
	m_streamIdleTimeoutSec = secs > 0 ? secs : 300;
	LOG("[StreamServer] 按需拉流 idle 超时: %d 秒", m_streamIdleTimeoutSec);
}

void StreamServer::startIdleMonitor() {
	if (m_idleMonitorRunning_) return;
	m_idleMonitorRunning_ = true;
	m_idleMonitorThread_ = std::thread(&StreamServer::idleMonitorLoop, this);
	m_idleMonitorThread_.detach();
	LOG("[StreamServer] 按需拉流 idle 监控已启动，超时=%d秒，检测间隔=5秒", m_streamIdleTimeoutSec);
}

void StreamServer::stopIdleMonitor() {
	m_idleMonitorRunning_ = false;
}

void StreamServer::idleMonitorLoop() {
	while (m_idleMonitorRunning_) {
		std::this_thread::sleep_for(std::chrono::seconds(5));

		std::lock_guard<std::mutex> lock(nodeLock_);
		auto now = std::chrono::steady_clock::now();

		for (auto& pair : m_mapStreamNodes) {
			const std::string& streamUrl = pair.first;
			std::shared_ptr<StreamNode> sn = pair.second;
			if (!sn) continue;

			// 只监控 ondemand 模式且状态为 streaming 的流
			if (sn->config_.srcStreamFetch != "ondemand") continue;
			if (sn->session_origin_.state_ != SESSION_STATE::SESSION_STREAMING) {
				m_idleTrackMap_.erase(streamUrl);
				continue;
			}

			if (sn->hasWorkToDo()) {
				m_idleTrackMap_.erase(streamUrl);
			}
			else {
				auto it = m_idleTrackMap_.find(streamUrl);
				if (it == m_idleTrackMap_.end()) {
					m_idleTrackMap_[streamUrl] = now;
				}
				else {
					int64_t elapsed = std::chrono::duration_cast<std::chrono::seconds>(
						now - it->second).count();
					if (elapsed >= m_streamIdleTimeoutSec) {
						LOG("[StreamServer] ondemand流 %s 已空闲 %lld 秒（超时=%d秒），自动关闭源连接",
							streamUrl.c_str(), elapsed, m_streamIdleTimeoutSec);
						sn->stop();
						m_idleTrackMap_.erase(streamUrl);
					}
				}
			}
		}
	}
}

void StreamServer::keepStreamAlive(const std::string& tag) {
	std::lock_guard<std::mutex> lock(nodeLock_);
	std::shared_ptr<StreamNode> sn = getStreamNodeByTag(tag);
	if (!sn) return;
	if (sn->config_.srcStreamFetch != "ondemand") return;
	m_idleTrackMap_.erase(sn->config_.streamUrl);
}

// ============================================================================
// 以下模块已拆分到独立文件：
//   - streamServer_file.cpp: 本地文件流服务
//   - streamServer_rtsp.cpp:  RTSP 服务端
//   - streamServer_rpc.cpp:   RPC 处理
// ============================================================================
