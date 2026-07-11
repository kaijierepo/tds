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

bool StreamServer::run() {
	initDtlsCertificate();

	// 启动 RTSP 服务端（配置项: rtspServerPort，默认 0=不启动）
	int rtspPort = tds->conf->getInt("rtspServerPort", 554);
	if (rtspPort > 0 && rtspPort <= 65535) {
		startRtspServer(rtspPort);
	}

	// 默认加载 tds.exe 同级目录下 rtsp 文件夹的 h264 文件作为流媒体源
	serveDefaultFolder("rtsp");

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
			pair.second->config_.origin_pull_url.find(ip) != std::string::npos) 
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
			pair.second->config_.origin_pull_url == srcUrl)
		{
			return pair.second;
		}
	}
	return nullptr;
}



bool StreamServer::openStream(string tag,string srcUrl, string pushTo)
{
	std::shared_ptr<StreamNode> sn = getStreamNodeByTag(tag);

	if (sn) {
		if (sn->config_.origin_pull_url == srcUrl && sn->config_.relay_push_url == pushTo) {
			LOG("[流媒体] 媒体源已打开，收到重复打开请求，忽略, 位号:%s, 当前配置地址:%s",
				tag.c_str(), sn->config_.origin_pull_url.c_str());
			return false;
		}
		if (sn->config_.origin_pull_url != srcUrl) {
			LOG("[流媒体] 媒体源变更，重启streamNode，当前拉流地址:%s, 新地址:%s",
				sn->config_.origin_pull_url.c_str(), srcUrl.c_str());
			closeStream(tag);
		}
		if (sn->config_.relay_push_url != pushTo) {
			LOG("[流媒体] 推流地址变更，重启streamNode, 当前推流地址:%s, 新地址:%s",
				sn->config_.relay_push_url.c_str(), pushTo.c_str());
			closeStream(tag);
		}
	}
	else {
		sn = std::make_shared<StreamNode>();
	}


	StreamNode::Config config;
	config.origin_pull_url = srcUrl; 
	// 提取用户名和密码
	bool isSuccess = sn->extractRtspAuthInfo(config);
	config.relay_push_url = pushTo; // 目标地址
	config.retry_interval = 3000;
	config.max_retries = 0; // 无限重试
	config.rtp_timeout = 10000;
	config.tag = tag;

	bool ret;
	if (sn->start(config)) {
		std::lock_guard<std::mutex> lock(nodeLock_);
		m_mapStreamNodes[tag] = sn;
		ret = true;
	}
	else {
		ret = false;
	}

	return ret;
}

bool StreamServer::closeStream(string tag)
{
	// 再尝试关闭 StreamNode
	std::shared_ptr<StreamNode> sn;
	{
		std::lock_guard<std::mutex> lock(nodeLock_);
		auto it = m_mapStreamNodes.find(tag);
		if (it != m_mapStreamNodes.end()) {
			sn = it->second;
			m_mapStreamNodes.erase(it);
			LOG("[流媒体] 正在停止 StreamNode for tag: %s", tag.c_str());
		}
	}

	// 在锁外停止 relay，避免潜在死锁
	if (sn) {
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
// 以下模块已拆分到独立文件：
//   - streamServer_file.cpp: 本地文件流服务
//   - streamServer_rtsp.cpp:  RTSP 服务端
//   - streamServer_rpc.cpp:   RPC 处理
// ============================================================================
