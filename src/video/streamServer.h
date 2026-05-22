#pragma once
#include "streamNode.h"


class StreamServer {
public:
	StreamServer() {};
	~StreamServer() {};

	bool handleRpc(std::string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_startStreamNode(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_playWebRtc(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_startRecord(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_stopRecord(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getStreamInfo(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getStreamNodeList(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session);

	bool openStream(string tag, string pushTo = "");

	bool closeStream(string tag);

	StreamNode* getStreamNode(std::string tag);

	map<std::string, std::unique_ptr<StreamNode>> m_mapStreamNodes;
	map<std::string, std::unique_ptr<StreamNode>> m_mapStreamNodes_urlID;
	mutable std::mutex nodeLock_url_;
	mutable std::mutex nodeLock_; // 保护 m_mapStreamNodes 的互斥锁

	bool m_enableZLM;
};

extern StreamServer streamSrv;