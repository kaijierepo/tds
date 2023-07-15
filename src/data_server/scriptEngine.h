#ifdef ENABLE_JERRY_SCRIPT
#include <string>
#include <map>
#include "json.hpp"
#include "jerryscript.h"
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
	string m_tagContext;
	string m_user; //执行脚本的用户，根据该用户权限控制该脚本的权限
	jerry_value_t global_object;
	vector<GLOBAL_FUNC> m_vecGlobalFunc;
	string m_script;

	RPC_SESSION currentSession;
	//脚本执行结果
	json m_jEvalRet;

	bool m_bValNullInCalc;  //val函数返回了null，当使用计算表达式时，例如 val(tag1) -val(tag2)，某一个val函数返回null，null会被作为0，但该次计算无效
};


void* context_alloc_fn(size_t size, void* cb_data);
extern thread_local jerry_context_t* tls_context;
extern thread_local ScriptEngine* pEngine;
jerry_context_t* jerry_port_get_current_context(void);
#endif