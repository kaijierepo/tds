#ifdef ENABLE_QJS
#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "quickjs.h"
#include <vector>
#include "yyjson.h"

using json = nlohmann::json;


struct SCRIPT_RUN_INFO {
	bool runSuccess = false;
	bool valNullInCalc = false;
	std::string lastError = "";
	std::string lastExe;
	std::string retVal = "";
	int runTimeCost = 0;
	std::map<std::string, std::string> tagRefDataTime;

	void toJson(yyjson_mut_doc* doc, yyjson_mut_val* yyVal);
};

struct SCRIPT_INFO {
	std::string script = "";
	std::string filePath = "";
	std::string folderPath = "";
	std::string envVarScript = "";
	std::string calcMpTag = "";
	std::string callerObjTag = "";
	std::string rootTag = "";
	std::string devAddr = "";
	std::string mode = "";
	std::string devId = "";
	std::string user = "";
	int interval = 0;
	bool isExp = 0; //是否表达式脚本

	std::string org = "";
	std::string lastModifyTime = "";
	std::string lastModifyUser = "";
	std::string name = "";
	std::string desc = "";

	SCRIPT_RUN_INFO lastRunInfo = SCRIPT_RUN_INFO();
	bool enableLog = false;

	bool enable = false;

	std::string getContextTag();
	std::string getExpContextTag();

	void toJson(yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot, bool getStatus = false);
	void fromJson(yyjson_val* mutRoot);
};

class ScriptEngine;

typedef void (*fp_engineInitFunc)(JSContext* ctx, void* pCustom);
typedef void (*fp_callMethod)(std::string method, std::string param, std::string& rlt,std::string& err);
typedef void (*fp_callMethodRR)(const std::string& req,std::string& resp);

class ScriptEngine {
public:
	ScriptEngine();
	bool runScript(SCRIPT_INFO& si, SCRIPT_RUN_INFO& sri);

	std::string m_sError;
	std::vector<std::string> m_vecOutput;         //执行一次脚本的输出信息，包含错误信息，脚本中的log
	std::map<std::string,std::string> m_vecValRefTime; //本次脚本引用的所有val函数的当前值时间，基于val算出来的二次变量，用所有val的最新时间作为二次变量的时间

	json m_globalObj;

	//当前脚本执行的环境变量
	std::string m_tagContext;
	std::string m_user;                      //执行脚本的用户，根据该用户权限控制该脚本的权限
	std::string m_scriptName;
	std::string m_script;

	void* m_ioDevThis;

	std::string m_folderPath;              //脚本所在的文件夹路径

	bool m_reloadFile;

	//脚本执行结果
	json m_sEvalRet;                   //脚本表达式的执行结果
	json m_scriptRet;                  //脚本内部返回的自定义的执行结果

	bool m_bValNullInCalc;             //val函数返回了null，当使用计算表达式时，例如 val(tag1) -val(tag2)，某一个val函数返回null，null会被作为0，但该次计算无效

	// 环境变量脚本行数
	int m_envVarScriptLine;

	std::vector<fp_engineInitFunc> m_engineInitFuncList;
	static fp_callMethod  callMethodImp;
	static fp_callMethodRR callMethodRRImp;
	static std::string ScriptFolder;
	static std::string ConfFolder;

	static void init();

};

extern void jsValToJsonVal(JSContext* ctx, JSValueConst jsVal, json& jsonVal);
extern void jsonValToJsVal(json& jsonVal, JSContext* ctx, JSValue& jsVal);

extern json engineArrayToJson(JSContext* ctx, const JSValueConst array[], const int count);
extern json engineObjectToJson(JSContext* ctx, JSValueConst object);

extern bool jsItemToJsonItem(JSContext* ctx, JSValueConst propName, JSValueConst propValue, void* data);
extern bool jsItemToJsonItem(JSContext* ctx, JSAtom atom, JSValueConst propValue, void* data);

JSValue yyVal_to_qjsVal(JSContext* ctx, yyjson_val* val);
JSValue yyVal_to_qjsVal(JSContext* ctx, yyjson_mut_val* val);
yyjson_val* qjsVal_to_yyVal(JSContext* ctx, JSValueConst js_val, yyjson_doc*& doc);

extern std::map<std::string, std::string> g_mapConfFile;

#endif