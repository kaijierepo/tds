#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;

/*
When executing a script, you do not need to write the complete system bit number, just write a bit number relative to the "environment bit number" (contextTag)
Complete environment bit number envTag = user.org + callerObjTag + rootTag;
user.org is the organizational structure to which the user who edited the script belongs
hostObjTag means calling and executing the script with a certain object as the main body
rootTag is a custom prefix specified by the user
*/

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
	string calcMpTag      = "";   // The monitoring point uses the monitoring point of the same level to calculate its own value. When it is an expression script, the variable is the monitoring point number to which the expression belongs. Call the script with a certain monitoring point as the main body, and use the number of the parent object of mp as callerObjTag.
	string callerObjTag   = "";   // Use an object as the main body to call and execute the script. For example, the duct machine calls the startup script
	string rootTag        = "";   // The root tag specified when the script is configured
	string devAddr        = "";   // Environmental device address
	string mode           = "";

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
	bool m_bEnableAutoCyclic;

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