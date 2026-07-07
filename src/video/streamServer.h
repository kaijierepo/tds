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

	bool handleRpc(std::string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_startStreamNode(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_playWebRtc(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_startRecord(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_stopRecord(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_removeRecordFile(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getStreamInfo(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getStreamNodeList(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session);

	bool openStream(string tag, string pushTo = "");

	bool closeStream(string tag);

	StreamNode* getStreamNode(std::string tag);			   // 通过标签查找 StreamNode
	StreamNode* getStreamNodeByIp(const std::string& ip);  // 通过IP地址查找 StreamNode

	map<std::string, std::unique_ptr<StreamNode>> m_mapStreamNodes;
	map<std::string, std::unique_ptr<StreamNode>> m_mapStreamNodes_urlID; //根据拉流源url查找StreamNode
	mutable std::mutex nodeLock_url_;
	mutable std::mutex nodeLock_; // 保护 m_mapStreamNodes 的互斥锁

	bool m_enableZLM;

	// DTLS 证书（所有 WebRTC 会话共用）
	std::string m_dtlsCertPem;
	std::string m_dtlsKeyPem;
	std::string m_dtlsFingerprint;  // SHA-256, 用于 SDP

	void initDtlsCertificate();
	void cleanOldRecords();

	// ---- RTSP 服务端 ----
	// 启动/停止 RTSP 服务端监听
	void startRtspServer(int port = 554);
	void stopRtspServer();
	bool isRtspServerRunning() const { return m_rtspRunning_; }

	// 通过 RTSP 推流创建的接收会话管理
	enum RtspRecvSessionState { RSS_IDLE, RSS_WAITING_RTP, RSS_RECEIVING };
	struct RtspRecvSession {
		StreamNode::SocketHandle rtp_sock = StreamNode::kInvalidSocket;
		StreamNode::SocketHandle rtcp_sock = StreamNode::kInvalidSocket;
		StreamNode::SocketHandle tcp_sock = StreamNode::kInvalidSocket;  // TCP interleaved 模式使用的 RTSP 连接
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
	void handleRtspClient(StreamNode::SocketHandle clientSock, const std::string& clientIp);
	std::string buildSdpForStream(StreamNode* node);
	StreamNode* findStreamByRtspPath(const std::string& path);

	// RTSP 推流接收
	void rtpRecvThread(std::shared_ptr<RtspRecvSession> session);
	void rtpTcpRecvLoop(StreamNode::SocketHandle tcpSock,
		std::shared_ptr<RtspRecvSession> session, StreamNode* streamNode);
	void cleanupPushSession(const std::string& sessionId);

	std::thread m_rtspThread_;
	std::atomic<bool> m_rtspRunning_{false};
	StreamNode::SocketHandle m_rtspListenSock_ = StreamNode::kInvalidSocket;
};

extern StreamServer streamSrv;