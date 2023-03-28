#include <string>
#include <map>
#include "json.hpp"
#ifdef ENABLE_JERRY_SCRIPT
#include "jerryscript.h"
#endif
#include "scriptFunc.h"

using json = nlohmann::json;
using namespace std;




struct GLOBAL_FUNC {
	jerry_value_t property_name;
	jerry_value_t property_func;
};

class ScriptEngine {
public:
	bool runScript(string& script,string user);

	vector<string> m_vecOutput;
	bool initGlobalFunc();
	void releaseGlobalFunc();
	string getErrorDesc(jerry_error_t error);

	//当前脚本执行的环境变量
	string m_tagThis;
	string m_user; //执行脚本的用户，根据该用户权限控制该脚本的权限
	jerry_value_t global_object;
	vector<GLOBAL_FUNC> m_vecGlobalFunc;

	RPC_SESSION currentSession;
	//脚本执行结果
	json m_jEvalRet;
};

extern thread_local ScriptEngine* pEngine;