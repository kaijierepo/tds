#include <string>
#include <map>
#include "json.hpp"
#ifdef ENABLE_JERRY_SCRIPT
#include "jerryscript.h"
#endif
#include "tdsSession.h"
#include "scriptFunc.h"

using json = nlohmann::json;
using namespace std;


struct VAR_EXP_SCRIPT_INFO {
	string script;
	string tagThis;
};


class ScriptManager {
public:
	bool init();
	bool run();
	
	std::map<string, string> m_mapScripts;
	std::map<string, VAR_EXP_SCRIPT_INFO> m_mapVarExpScripts;

	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	void updateVarExpScript();
	string getScriptPath(json& params, RPC_SESSION session);
	json getScriptList(string tag);
	void exeAllGlobalScripts();
	void exeAllVarExpScripts();
	void loopExe();
};

extern ScriptManager scriptManager;