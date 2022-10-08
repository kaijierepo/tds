#include <string>
#include <map>
#include "json.hpp"
#ifdef ENABLE_JERRY_SCRIPT
#include "jerryscript.h"
#endif
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;

class scriptHost {
public:
	bool init();
	bool run();

	std::map<string, string> m_mapScripts;
#ifdef ENABLE_JERRY_SCRIPT
	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool runScript(string& script);
	bool rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	string getScriptPath(json& params, RPC_SESSION session);
	bool rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	//获得属于某一个位号的脚本列表
	json getScriptList(string tag);

	void loopExe();
	json engineValToJson(const jerry_value_t value);
	static json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count);
	static bool setScriptEngineObj(json& jObj, jerry_value_t engineObj);
	static bool getScriptEngineObj(json& jObj, jerry_value_t engineObj);
#endif
};

extern scriptHost sHost;