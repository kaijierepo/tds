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


/*
脚本在执行时，不需要写完整的系统位号，只要写一个相对于“环境位号”(contextTag)的位号即可
完整的环境位号 envTag = user.org + callerObjTag + rootTag;
user.org 编辑该脚本的用户所属的组织结构
hostObjTag 表示以某个对象为主体来调用执行该脚本
rootTag 为用户指定的一个自定义前缀
*/

struct SCRIPT_INFO {
	string script;
	string calcMpTag;    //监控点利用同级监控点计算自身数值的情况，以某个监控点为主体调用脚本，将mp的父对象的位号作为callerObjTag。 
	string callerObjTag;   //以某个对象为主体来调用执行该脚本. 例如风管机 调用 开机脚本
	string rootTag;  //脚本配置时指定的根位号
	string mode;
	int interval;
	TIME lastExe;
	string org;
	string lastModifyTime;
	string lastModifyUser;
	string name;
	string desc;

	string getContextTag();
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
	std::map<string,SCRIPT_INFO> m_mapScripts;
	std::mutex m_csScripts;

	vector<SCRIPT_INFO> m_vecVarExpScripts;
	std::mutex m_csExpScripts;

	bool runScriptFileAsyn(string scriptName,string tagThis);
	bool getScript(string name, SCRIPT_INFO& si);
	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	void scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl,json& j);
	void saveScriptList(string org, std::map<string, SCRIPT_INFO>& sl, bool saveScriptData = false);

	void updateVarExpScript(vector<SCRIPT_INFO>& varExpScripts);
	string getScriptPath(json& params, RPC_SESSION session);
	json getScriptList(string tag);
	void exeAllGlobalScripts();
	void exeAllVarExpScripts();
	void loopExe();
	bool loopRunning;
};

extern ScriptManager scriptManager;