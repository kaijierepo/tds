#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "tdsSession.h"
#include "scriptEngine.h"

using json = nlohmann::json;
using namespace std;




class ScriptManager {
public:
	ScriptManager();
	bool init();
	bool loadScriptList();
	bool run();

	void setConfPath(const string& conf);
	string m_confPath;

	bool m_reloadFile;
	bool m_bRun;
	bool hasScripts();
	
	std::map<string, SCRIPT_INFO> m_mapScripts;  
	std::mutex m_csScripts;

	vector<SCRIPT_INFO> m_vecVarExpScripts;
	std::mutex m_csExpScripts;

	bool handleRpc(string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool runScript(string scriptName, string params, string& result, string& output);
	bool runScriptFileAsyn(string scriptName,string tagThis);
	bool getScript(string name, SCRIPT_INFO& si);
	bool setRunInfo(string name, SCRIPT_RUN_INFO& sri);
	bool rpc_runScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_loadScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptMngStatus(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScriptEnable(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);

	void scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl, yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot);
	void saveScriptList(string org, std::map<string, SCRIPT_INFO>& sl, bool saveScriptData = false);

	void updateVarExpScript(vector<SCRIPT_INFO>& varExpScripts);

	string getScriptPath(yyjson_val* params_obj, RPC_SESSION session);
	json getScriptList(string tag);

	ioDev* getEvnDev(SCRIPT_INFO& si);

	void exeAllGlobalScripts();
	void exeAllVarExpScripts();

	void loopExe();

	float m_lastExpScripTimeCost;
	TIME m_tLastExpScriptRunTime;
};

extern ScriptManager scriptManager;