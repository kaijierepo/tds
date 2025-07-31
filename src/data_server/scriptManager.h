#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;

struct SCRIPT_RUN_INFO {
	bool runSuccess     = false;
	bool valNullInCalc  = false;

	string lastError    = "";
	string retVal       = "";

	int runTimeCost     = 0; 
	map<string, string> tagRefDataTime;
};

struct SCRIPT_INFO {
	string script         = "";
	string envVarScript   = "";
	string calcMpTag      = "";   
	string callerObjTag   = "";   
	string rootTag        = "";   
	string devAddr        = "";   
	string mode           = "";
	string devId          = ""; 

	int interval          = 0;
	TIME lastExe          = TIME();

	string org            = "";
	string lastModifyTime = "";
	string lastModifyUser = "";
	string name           = "";
	string desc           = "";

	SCRIPT_RUN_INFO lastRunInfo = SCRIPT_RUN_INFO();
	bool enableLog        = false;

	bool scriptActived    = false;
	bool scriptLooping    = false;

	string getContextTag();
	string getExpContextTag();

	void toJson(json& j, bool getStatus = false);
	void fromJson(json& j);
	
	void toJson(yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot, bool getStatus = false);
	void fromJson(yyjson_val* mutRoot);
};

class ScriptManager {
public:
	ScriptManager();
	bool init();
	bool run();

	void setConfPath(const string& conf);
	string m_confPath;

	bool m_bRun;
	bool m_bEnable;

	bool hasScripts();
	
	std::map<string, SCRIPT_INFO> m_mapScripts;  
	std::mutex m_csScripts;

	vector<SCRIPT_INFO> m_vecVarExpScripts;
	std::mutex m_csExpScripts;

	bool handleRpc(string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);

	bool runScriptFileAsyn(string scriptName,string tagThis);
	bool getScript(string name, SCRIPT_INFO& si);
	bool rpc_runScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptMngStatus(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScriptActived(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScriptLooping(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session);

	void scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl, yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot);
	void saveScriptList(string org, std::map<string, SCRIPT_INFO>& sl, bool saveScriptData = false);

	void updateVarExpScript(vector<SCRIPT_INFO>& varExpScripts);

	string getScriptPath(yyjson_val* params_obj, RPC_SESSION session);
	json getScriptList(string tag);

	void exeAllGlobalScripts();
	void exeAllVarExpScripts();

	void loopExe();

	float m_lastExpScripTimeCost;
	TIME m_tLastExpScriptRunTime;

	void updateAutoCyclicScripLoopings();
};

extern ScriptManager scriptManager;