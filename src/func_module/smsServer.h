#include <string>
#include <map>
#include "json.hpp"
#include "tds.h"
using json = nlohmann::json;
using namespace std;

class SmsServer  : public i_smsServer {
public:
	SmsServer();
	bool init() override;
	bool run() override;
	bool send(string& msg, string& phoneNum)  override;
	bool send(json& params, string& phoneNum) override;
	bool sendVerificationCode(string phoneNum) override;
	bool checkVerificationCode(string phoneNum,string code) override;
	string m_url;
	string m_user;
	string m_key;

	map<string, string> m_mapVeriCode;
};

extern SmsServer smsServer;