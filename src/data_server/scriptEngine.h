#ifdef ENABLE_JERRY_SCRIPT
#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "jerry.h"
#include "jerryscript.h"
#include  "tdsSession.h"
#include "ioDev.h"

using json = nlohmann::json;
using namespace std;



struct GLOBAL_FUNC {
	jerry_value_t property_name;
	jerry_value_t property_func;
};

typedef bool (*fp_initGlobalFunc)(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc);
typedef bool (*fp_initIODevFunc)(jerry_value_t ioDev_object,ioDev* pDev);

class ScriptEngine {
public:
	ScriptEngine();
	bool runScript(string& script,string user);

	string m_sError;
	vector<string> m_vecOutput; //执行一次脚本的输出信息，包含错误信息，脚本中的log
	void releaseGlobalFunc();
	string getErrorDesc(jerry_error_t error);

	fp_initGlobalFunc m_initGlobalFunc;
	fp_initIODevFunc m_initIODevFunc;

	json m_globalObj;

	//当前脚本执行的环境变量
	string m_tagContext;
	string m_user; //执行脚本的用户，根据该用户权限控制该脚本的权限
	jerry_value_t global_object;
	vector<GLOBAL_FUNC> m_vecGlobalFunc;
	string m_script;
	ioDev* m_ioDevThis;

	RPC_SESSION currentSession;
	//脚本执行结果
	string m_sEvalRet;
	json m_scriptRet; //脚本自定义的执行结果

	bool m_bValNullInCalc;  //val函数返回了null，当使用计算表达式时，例如 val(tag1) -val(tag2)，某一个val函数返回null，null会被作为0，但该次计算无效
};

extern thread_local ScriptEngine* pEngine;
#endif