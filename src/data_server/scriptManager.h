#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;

/*
脚本在执行时，不需要写完整的系统位号，只要写一个相对于“环境位号”(contextTag)的位号即可
完整的环境位号 envTag = user.org + callerObjTag + rootTag;
user.org 编辑该脚本的用户所属的组织结构
hostObjTag 表示以某个对象为主体来调用执行该脚本
rootTag 为用户指定的一个自定义前缀
*/

struct SCRIPT_RUN_INFO {
	bool runSuccess     = false;
	bool valNullInCalc  = false;

	string lastError    = "";
	int runTimeCost     = 0; 
	json retVal         = nullptr;
	map<string, string> tagRefDataTime;
};

struct SCRIPT_INFO {
	string script         = "";
	string envVarScript   = "";
	string calcMpTag      = "";   //监控点利用同级监控点计算自身数值的情况，当为表达式脚本时，该变量就是表达式所属的监控点位号。以某个监控点为主体调用脚本，将mp的父对象的位号作为callerObjTag。 
	string callerObjTag   = "";   //以某个对象为主体来调用执行该脚本. 例如风管机 调用 开机脚本
	string rootTag        = "";   //脚本配置时指定的根位号
	string devAddr        = "";   //环境设备地址
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

	string getContextTag();
	string getExpContextTag();

	void toJson(json& j, bool getStatus = false);
	void fromJson(json& j);
};

class ScriptManager {
public:
	ScriptManager();
	bool init();
	bool run();

	void setConfPath(const string& conf);
	string m_confPath; //从原有调用tds->conf->confPath 改为外部设置这个变量，然后启用被注释的代码

	bool m_bRun;
	bool m_bEnable;
	bool m_bEnableAutoCyclic;

	bool hasScripts();
	
	std::map<string,SCRIPT_INFO> m_mapScripts;  //第一个key是组织结构，第二个key是脚本文件的name
	std::mutex m_csScripts;

	vector<SCRIPT_INFO> m_vecVarExpScripts;
	std::mutex m_csExpScripts;

	bool handleRpc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	bool runScriptFileAsyn(string scriptName,string tagThis);
	bool getScript(string name, SCRIPT_INFO& si);
	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptMngStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	void scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl,json& j);
	void saveScriptList(string org, std::map<string, SCRIPT_INFO>& sl, bool saveScriptData = false);

	void updateVarExpScript(vector<SCRIPT_INFO>& varExpScripts);

	string getScriptPath(json& params, RPC_SESSION session);
	json getScriptList(string tag);

	void exeAllGlobalScripts();
	void exeAllVarExpScripts();

	void loopExe();

	float m_lastExpScripTimeCost;
	TIME m_tLastExpScriptRunTime;
};

extern ScriptManager scriptManager;