#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "jerry.h"
#include "jerryscript.h"
#include "scriptFunc.h"
#include "scriptEngine.h"

using json = nlohmann::json;
using namespace std;

class ScriptEngine_qjs;

class ScriptEngine_qjs {
public:
	ScriptEngine_qjs();
	bool runScript(string& script,string user);

	string m_sError;
	vector<string> m_vecOutput; //执行一次脚本的输出信息，包含错误信息，脚本中的log
	map<string,string> m_vecValRefTime; //本次脚本引用的所有val函数的当前值时间，基于val算出来的二次变量，用所有val的最新时间作为二次变量的时间

	json m_globalObj;

	//当前脚本执行的环境变量
	string m_tagContext;
	string m_user; //执行脚本的用户，根据该用户权限控制该脚本的权限
	jerry_value_t global_object;
	vector<GLOBAL_FUNC> m_vecGlobalFunc;
	string m_scriptName;
	string m_script;
	void* m_ioDevThis;

	//脚本执行结果
	string m_sEvalRet;
	json m_scriptRet; //脚本自定义的执行结果

	bool m_bValNullInCalc;  //val函数返回了null，当使用计算表达式时，例如 val(tag1) -val(tag2)，某一个val函数返回null，null会被作为0，但该次计算无效

	fp_initIODevFunc m_initIODevFunc;

};

extern thread_local ScriptEngine_qjs* pEngine_qjs;