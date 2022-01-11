#include <string>
#include <map>
#include "json.hpp"
#include "jerryscript.h"
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;

class scriptHost {
public:
	bool init();
	bool run();

	bool rpc_runScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool runScript(string& script);

	void loopExe();
	std::map<string, string> m_mapScripts;
	json engineValToJson(const jerry_value_t value);
	static json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count);
	static bool setScriptEngineObj(json& jObj, jerry_value_t engineObj);
	static bool getScriptEngineObj(json& jObj, jerry_value_t engineObj);
};

extern scriptHost sHost;