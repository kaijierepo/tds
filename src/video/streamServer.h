#pragma once
#include "streamNode.h"
#include "yyjson.h"
#include "tdsRPC.h"

#include <thread>
#include <atomic>

class StreamServer {
public:
	StreamServer() {};
	~StreamServer() {};

	bool run();

	bool handleRpc(std::string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_openStream(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_playWebRtc(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_startRecord(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_stopRecord(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_removeRecordFile(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getRecordList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getStreamInfo(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getStreamNodeList(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setStream(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_serveLocalFile(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);

	std::shared_ptr<StreamNode> openStream(const STREAM_OPEN_PARAM& openParam);
	std::shared_ptr<StreamNode> createStream(const STREAM_OPEN_PARAM& openParam);

	bool closeStream(string tag);

	void setIdleTimeout(int secs);

	std::shared_ptr<StreamNode> getStreamNodeByStreamUrl(std::string tag);			   // 通过url查找 StreamNode，包含 /符号
	std::shared_ptr<StreamNode> getStreamNodeByTag(std::string tag);
	std::shared_ptr<StreamNode> getStreamNodeByIp(const std::string& ip);  // 通过IP地址查找 StreamNode
	std::shared_ptr<StreamNode> getStreamNodeBySrcUrl(const std::string& srcUrl);  // 通过IP地址查找 StreamNode

	map<std::string, std::shared_ptr<StreamNode>> m_mapStreamNodes;
	mutable std::mutex nodeLock_; // 保护 m_mapStreamNodes 的互斥锁

	bool m_enableZLM = false;
	bool m_alwaysOpenStream = false;

	// DTLS 证书（所有 WebRTC 会话共用）
	std::string m_dtlsCertPem;
	std::string m_dtlsKeyPem;
	std::string m_dtlsFingerprint;  // SHA-256, 用于 SDP

	void initDtlsCertificate();
	void cleanOldRecords();

	// 将本地 h264/mp4 文件作为 RTSP 流提供出去
	// filePath: 本地文件路径
	// url: RTSP 路径，如 "/camera1" 或 "/live/test"
	bool serveLocalStreamFile(const std::string& filePath, const std::string& url);

	// 默认递归遍历 tds.exe 同级目录下的 rtsp 文件夹，
	// 按照文件夹路径结构作为 RTSP url，对外提供所有 h264 文件流媒体服务
	// folderName: 要遍历的文件夹名，默认 "rtsp"
	void serveDefaultFolder(const std::string& folderName = "rtsp");

	// 按需加载本地文件流（有客户端拉流时才读文件启动喂流线程）
	// 返回创建的 StreamNode，失败返回 nullptr
	std::shared_ptr<StreamNode> loadLocalFileStream(const std::string& tag);

	// 检查并停止没有客户端的本地文件流
	void cleanupIdleLocalStream(const std::string& tag);

	// 本地文件映射：url路径 → 本地文件路径
	std::map<std::string, std::string> m_localFileMap;
	std::mutex m_localFileMapMutex_;

	// ---- RTSP 服务端 ----
	// 启动/停止 RTSP 服务端监听
	void startRtspServer(int port = 554);
	void stopRtspServer();
	bool isRtspServerRunning() const { return m_rtspRunning_; }

	// 通过 RTSP 推流创建的接收会话管理
	enum RtspRecvSessionState { RSS_IDLE, RSS_WAITING_RTP, RSS_RECEIVING };
	struct RtspRecvSession {
		SocketHandle rtp_sock = kInvalidSocket;
		SocketHandle rtcp_sock = kInvalidSocket;
		SocketHandle tcp_sock = kInvalidSocket;  // TCP interleaved 模式使用的 RTSP 连接
		int server_rtp_port = 0;
		int server_rtcp_port = 0;
		std::string tag;               // 关联的 stream tag
		std::string session_id;
		std::string client_ip;
		int client_rtp_port = 0;
		int client_rtcp_port = 0;
		int interleaved_rtp = -1;      // TCP interleaved RTP 通道号
		int interleaved_rtcp = -1;     // TCP interleaved RTCP 通道号
		bool is_tcp_interleaved = false;
		RtspRecvSessionState state = RSS_IDLE;
		// RTP 接收线程
		std::thread recv_thread_;
		std::atomic<bool> recv_running_{false};
	};
	std::map<std::string, std::shared_ptr<RtspRecvSession>> m_pushSessions_;  // key=session_id
	std::mutex m_pushSessionsMutex_;

private:
	// RTSP 服务端监听线程
	void rtspListenLoop(int port);
	void handleRtspClient(SocketHandle clientSock, const std::string& clientIp);
	std::string buildSdpForStream(const std::shared_ptr<StreamNode>& node);

	// RTSP 推流接收
	void threadRecv_rtspPublish(std::shared_ptr<RtspRecvSession> session);
	void rtpTcpRecvLoop(SocketHandle tcpSock,
		std::shared_ptr<RtspRecvSession> session, std::shared_ptr<StreamNode> streamNode);
	void cleanupPushSession(const std::string& sessionId);

	// 按需拉流 idle 监控
	int m_streamIdleTimeoutSec = 300;
	std::thread m_idleMonitorThread_;
	std::atomic<bool> m_idleMonitorRunning_{false};
	std::map<std::string, std::chrono::steady_clock::time_point> m_idleTrackMap_;
	void startIdleMonitor();
	void stopIdleMonitor();
	void idleMonitorLoop();

	std::thread m_rtspThread_;
	std::atomic<bool> m_rtspRunning_{false};
	SocketHandle m_rtspListenSock_ = kInvalidSocket;
};

extern StreamServer streamSrv;