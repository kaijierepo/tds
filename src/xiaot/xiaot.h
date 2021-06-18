#include <string>
#include <map>
#include "json.hpp"
using json = nlohmann::json;
using namespace std;

class CXiaoT {
public:
	bool init();
	std::string getReply(json msg);
	std::map<string, string> brain;
};

extern CXiaoT xiaot;