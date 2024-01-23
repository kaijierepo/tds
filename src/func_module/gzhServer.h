#include <string>
#include <map>
#include "json.hpp"
#include "tds.h"
using json = nlohmann::json;
using namespace std;

class GzhServer  : public i_gzhServer{
public:
	GzhServer();
	bool init() override;
	bool run() override;
	std::string getReply(string msg) override;
	std::map<string, string> brain;

	string packGzhResp(string msg, string openID);

	string getOpenId(string msg);
	string getRecognition(string msg);
	string getMsgType(string msg);
	string getContent(string msg);
	string getEvent(string msg);
	string getToUserName(string msg);

	string m_GzhID;
};

extern GzhServer gzhServer;