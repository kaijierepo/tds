#ifndef TDS_FUNC_MODULE_XIAOT_H
#define TDS_FUNC_MODULE_XIAOT_H

#include <string>
#include <map>
#include "json.hpp"
#include "tds.h"
using json = nlohmann::json;
using namespace std;


struct DEV_MNG_INFO {
	string devList;
	size_t devCount;
};


namespace MSG_CMD {
	const string nextPage = "nextPage";
	const string previousPage = "previousPage";
	const string play = "play";
	const string stop = "stop";
}



class CXiaoT  : public i_xiaoT{
public:
	CXiaoT();
	bool init() override;
	bool run() override;
	std::string getReply(string jMsg) override;
	bool isDevMngCmd(string msg);
	bool isDevCtrlCmd(string ctrlCmd);
	string handleDevCtrl(string ctrlCmd, json& user);
	DEV_MNG_INFO getDevList();
	string handleDevMng(string msg, json& user);
	std::string getReply_czyMode(string msg, json& user);
	std::string getReply_chatgpt_aliCloud_proxy(string msg, json& user);
	std::string getReply_chatgpt(string msg, json& user);
	std::string getReply(string msg, json& user);
	std::map<string, string> brain;

	string parseMsg(string msg);

	string m_ctrlAuthCode;
	string generateCtrlAuthCode();
	map<string, string> m_WaitingUserNameInput;

	string m_openAiSecret;
	string m_xiaotMode;

	//20个字一下问题，可能雷同，缓存下
	void setReplyBuff(string question, string reply);
	string getReplyFromBuff(string question, string reply);
	map<string, string> m_mapReplyBuff;
	mutex m_csReply;
};

extern CXiaoT xiaot;
#endif /* TDS_FUNC_MODULE_XIAOT_H */
