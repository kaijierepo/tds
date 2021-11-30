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
};

extern scriptHost sHost;