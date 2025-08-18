#pragma once
#include <string>
#include "yyjson.h"
#include "json.hpp"
#include "tdsRPC.h"

using namespace std;
using json = nlohmann::json;


class RpcHandler_common {
public:
	bool handleRpc(const string& method,json& params, RPC_RESP& rpcResp, RPC_SESSION& session);

	string m_confPath;
	string m_dbPath;
	string m_fmsPath;
};

extern RpcHandler_common rpcHandler_common;