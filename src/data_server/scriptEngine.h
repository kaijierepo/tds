#ifdef ENABLE_QJS
#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "quickjs.h"
#include <vector>

using json = nlohmann::json;
using namespace std;

class ScriptEngine;

typedef void (*fp_initTdsFunc)(JSContext* ctx);

class ScriptEngine {
public:
	ScriptEngine();
	bool runScript(string& script, string user);

	string m_sError;
	vector<string> m_vecOutput;         //执行一次脚本的输出信息，包含错误信息，脚本中的log
	map<string,string> m_vecValRefTime; //本次脚本引用的所有val函数的当前值时间，基于val算出来的二次变量，用所有val的最新时间作为二次变量的时间

	json m_globalObj;

	//当前脚本执行的环境变量
	string m_tagContext;
	string m_user;                      //执行脚本的用户，根据该用户权限控制该脚本的权限
	string m_scriptName;
	string m_script;

	void* m_ioDevThis;

	//脚本执行结果
	string m_sEvalRet;
	json m_scriptRet;                  //脚本自定义的执行结果

	bool m_bValNullInCalc;             //val函数返回了null，当使用计算表达式时，例如 val(tag1) -val(tag2)，某一个val函数返回null，null会被作为0，但该次计算无效

	fp_initTdsFunc m_initTdsFunc;
};

extern thread_local ScriptEngine* pEngine;

extern void jsValToJsonVal(JSContext* ctx, JSValueConst jsVal, json& jsonVal);
extern void jsonValToJsVal(json& jsonVal, JSContext* ctx, JSValue& jsVal);

extern json engineArrayToJson(JSContext* ctx, const JSValueConst array[], const int count);
extern json engineObjectToJson(JSContext* ctx, JSValueConst object);

extern bool jsItemToJsonItem(JSContext* ctx, JSValueConst propName, JSValueConst propValue, void* data);
extern bool jsItemToJsonItem(JSContext* ctx, JSAtom atom, JSValueConst propValue, void* data);

#endif