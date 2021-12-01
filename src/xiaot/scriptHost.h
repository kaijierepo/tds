#include <string>
#include <map>
#include "json.hpp"
#include "jerryscript.h"
using json = nlohmann::json;
using namespace std;

class scriptHost {
public:
	bool init();
	bool run();

	void loopExe();
	std::map<string, string> m_mapScripts;
	static json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count);
	static bool setScriptEngineObj(json& jObj, jerry_value_t engineObj);
};

extern scriptHost sHost;