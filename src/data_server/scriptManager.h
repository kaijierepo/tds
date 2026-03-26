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

	void setConfPath(const std::string& conf);
	std::string m_confPath;

	bool m_reloadFile;
	bool m_bRun;
	bool hasScripts();
	
	std::map<std::string, SCRIPT_INFO> m_mapScripts;  
	std::mutex m_csScripts;

	std::vector<SCRIPT_INFO> m_vecVarExpScripts;
	std::mutex m_csExpScripts;

	bool handleRpc(std::string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool runScript(std::string scriptName, std::string params, std::string& result, std::string& output);
	bool runScriptFileAsyn(std::string scriptName,std::string tagThis);
	bool getScript(std::string name, SCRIPT_INFO& si);
	bool setRunInfo(std::string name, SCRIPT_RUN_INFO& sri);
	bool rpc_runScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_loadScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptMngStatus(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScriptEnable(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_runAnalyseScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);

	void scriptList2Json(std::string org, std::map<std::string, SCRIPT_INFO>& sl, yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot);
	void saveScriptList(std::string org, std::map<std::string, SCRIPT_INFO>& sl, bool saveScriptData = false);

	void updateVarExpScript(std::vector<SCRIPT_INFO>& varExpScripts);

	std::string getScriptPath(yyjson_val* params_obj, RPC_SESSION session);
	json getScriptList(std::string tag);

	ioDev* getEvnDev(SCRIPT_INFO& si);

	void exeAllGlobalScripts();
	void exeAllVarExpScripts();

	void loopExe();

	float m_lastExpScripTimeCost;
	TIME m_tLastExpScriptRunTime;

	std::vector<fp_engineInitFunc> m_engineInitFuncList;
};

extern ScriptManager scriptManager;