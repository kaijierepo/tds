#pragma once
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


struct SCRIPT_INFO {
	string script;
	string tagThis;
	string mode;
	int interval;
	TIME lastExe;
	string org;
	string lastModifyTime;
	string lastModifyUser;
	string name;
	string desc;

	void toJson(json& j);
	void fromJson(json& j);
};


class ScriptManager {
public:
	ScriptManager();
	bool init();
	bool run();

	bool hasScripts();
	//第一个key是组织结构，第二个key是脚本文件的name
	std::map<string, std::map<string,SCRIPT_INFO>> m_mapScripts;
	std::mutex m_csScripts;

	std::map<string, SCRIPT_INFO> m_mapVarExpScripts;
	std::mutex m_csExpScripts;

	bool runScriptFileAsyn(string scriptName,string tagThis);
	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	void scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl,json& j);
	void saveScriptList(string org, std::map<string, SCRIPT_INFO>& sl, bool saveScriptData = false);

	void updateVarExpScript(std::map<string, SCRIPT_INFO>& varExpScripts);
	string getScriptPath(json& params, RPC_SESSION session);
	json getScriptList(string tag);
	void exeAllGlobalScripts();
	void exeAllVarExpScripts();
	void loopExe();
	bool loopRunning;
};

extern ScriptManager scriptManager;