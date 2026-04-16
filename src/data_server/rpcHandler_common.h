#pragma once
#include <string>
#include "yyjson.h"
#include "json.hpp"
#include "tdsRPC.h"


using json = nlohmann::json;


class RpcHandler_common {
public:
	bool handleRpc(const std::string& method,json& params, RPC_RESP& rpcResp, RPC_SESSION& session);

	void rpc_getconffile(json params, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_setconffile(json params, RPC_RESP& resp, RPC_SESSION& session);

	std::string m_confPath;
	std::string m_dbPath;
	std::string m_fmsPath;
	std::string m_appPath;
};

extern RpcHandler_common rpcHandler_common;