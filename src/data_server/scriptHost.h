#ifdef ENABLE_JERRY_SCRIPT
#include <string>
#include <map>
#include "json.hpp"
#include "jerryscript.h"
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;


struct VAR_EXP_SCRIPT_INFO {
	string script;
	string tagThis;
};

class scriptHost {
public:
	bool init();
	bool run();
	void updateVarExpScript();
	vector<string> m_vecOutput;


	std::map<string, string> m_mapScripts;
	std::map<string, VAR_EXP_SCRIPT_INFO> m_mapVarExpScripts;
	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool initGlobalFunc();
	void releaseGlobalFunc();
	bool runScript(string& script);
	bool rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	string getScriptPath(json& params, RPC_SESSION session);
	bool rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	json getScriptList(string tag);

	void exeAllGlobalScripts();
	void exeAllVarExpScripts();
	void loopExe();


	static bool jsonVal2jerryVal(json& jVal, jerry_value_t& jerryVal);
	static json engineValToJson(const jerry_value_t value);
	static json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count);

	static bool getScriptEngineObj(json& jObj, jerry_value_t engineObj);

	string m_tagThis;


	jerry_value_t global_object;
	jerry_value_t property_name_getMp;
	jerry_value_t property_func_getMp;
	jerry_value_t property_name_log;
	jerry_value_t property_func_log;
	jerry_value_t property_name_output;
	jerry_value_t property_func_output;
	jerry_value_t property_name_call;
	jerry_value_t property_func_call;
	jerry_value_t property_name_sum;
	jerry_value_t property_func_sum;
	jerry_value_t property_name_val;
	jerry_value_t property_func_val;
};

extern scriptHost sHost;
#endif