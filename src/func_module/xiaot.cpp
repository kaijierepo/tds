#include "pch.h"
#include "xiaot.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "tds_imp.h"
#include "ioSrv.h"
#include "userMng.h"
#include "httplib.h"

CXiaoT xiaot;

CXiaoT::CXiaoT()
{
	tds->xiaoT = this;
}

bool CXiaoT::init()
{
	m_xiaotMode = tds->conf->getStr("xiaotMode", "czy");
	m_openAiSecret = tds->conf->getStr("openAiSecret", "");

	string conf;
	if (!fs::readFile(tds->conf->confPath + "/xiaot/xiaot.json", conf))
	{
		//LOG("project conf xiaot.json not find,use empty config!");
		return true;
	}

	try {
		json jXiaot = json::parse(conf.c_str());
		for (int i = 0; i < jXiaot.size(); i++)
		{
			json qaTemp = jXiaot.at(i);
			json qList = qaTemp[0];
			json aList = qaTemp[1];
			json a = aList[0];
			for (int j = 0; j < qList.size(); j++)
			{
				json q = qList[j];
				brain[q.get<string>()] = a.get<string>();
			}
		}
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
		return false;
	}
	return  true;
}

bool CXiaoT::run()
{
	return false;
}


std::string CXiaoT::getReply(string jMsg)
{
	json msg = json::parse(jMsg);
	
	return "";
}


bool CXiaoT::isDevMngCmd(string msg)
{
	if (msg.find("当前设备") != string::npos)
		return true;
	else if (msg.find("设备列表") != string::npos)
		return true;
	return  false;
}


bool CXiaoT::isDevCtrlCmd(string ctrlCmd)
{
	if (ctrlCmd == MSG_CMD::nextPage)
		return true;
	else if (ctrlCmd == MSG_CMD::previousPage)
		return true;
	else if (ctrlCmd == MSG_CMD::play)
		return true;
	else if (ctrlCmd == MSG_CMD::stop)
		return true;
	return  false;
}

string CXiaoT::handleDevCtrl(string ctrlCmd, json& user)
{
	string reply;
	//用户没有选中当前设备
	if (user["dlgCtrlIOAddr"] == nullptr)
	{
		vector<ioDev*> ary;
		ioSrv.getAllTDSPDev(ary);
		if (ary.size() == 0)
		{
			reply = "没有在线设备";
			return reply;
		}
		else if (ary.size() == 1)
		{
			user["dlgCtrlIOAddr"] = ary[0]->getIOAddrStr();
		}
		else
		{
			reply = "请先选中一台设备,设备列表:\n";
			DEV_MNG_INFO dmi = getDevList();
			reply += dmi.devList;
			return reply;
		}
	}
	
	//用户已经选中或者只有一台自动给用户选中
	if(user["dlgCtrlIOAddr"] != nullptr)
	{
		if (user["role"] == "观察员")
		{
			reply = "您没有操控权限，请向系统管理员获取操控授权";
			return reply;
		}


		string ioAddr = user["dlgCtrlIOAddr"].get<string>();
		ioDev* pd = ioSrv.getIODev(ioAddr);
		if (pd && pd->m_devType == DEV_TYPE::DEV::tdsp_device)
		{
			if (!pd->m_bOnline)
			{
				reply = "设备" + ioAddr + "目前不在线";
				return reply;
			}

			json jrlt, jerr;

			if(ctrlCmd == MSG_CMD::nextPage)
				pd->call("demoScreen.nextPage", nullptr, nullptr, jrlt, jerr, false);
			else if(ctrlCmd == MSG_CMD::previousPage)
				pd->call("demoScreen.previousPage", nullptr, nullptr, jrlt, jerr, false);
			else if (ctrlCmd == MSG_CMD::play)
				pd->call("demoScreen.play", nullptr, nullptr, jrlt, jerr, false);
			else if (ctrlCmd == MSG_CMD::stop)
				pd->call("demoScreen.stop", nullptr, nullptr, jrlt, jerr, false);

			reply = "好的，已发送操控指令";
		}
		else
		{
			reply = "没有找到设备" + ioAddr;
		}
	}
	return reply;
}

DEV_MNG_INFO CXiaoT::getDevList()
{
	DEV_MNG_INFO dmi;
	vector<ioDev*> ary;
	ioSrv.getAllTDSPDev(ary);
	for (int i = 0; i < ary.size(); i++)
	{
		ioDev* p = ary[i];
		string sOnline = p->m_bOnline ? "在线" : "离线";
		dmi.devList += p->getIOAddrStr() + "(" + sOnline + ")" + "\n";
	}
	dmi.devCount = ary.size();
	return dmi;
}

string CXiaoT::handleDevMng(string msg, json& user)
{
	string text = msg;
	string reply;
	DEV_MNG_INFO dmi = getDevList();

	if (dmi.devCount == 0)
	{
		reply = "系统中没有设备，请联系系统管理员连接设备";
		return reply;
	}

	if (text.find("当前设备") != string::npos)
	{
		if (user["dlgCtrlIOAddr"] == nullptr)
		{
			reply = "您还没有选择当前控制设备，请在设备列表中选择:\n";
			reply += dmi.devList;
		}
		else
		{
			string ioAddr = user["dlgCtrlIOAddr"].get<string>();
			reply = ioAddr;
		}
	}
	else if (text.find("设备列表") != string::npos) {
		reply = "设备列表:\n";
		reply += dmi.devList;
	}
	return reply;
}

std::string CXiaoT::getReply_czyMode(string msg, json& user) {
	string reply = "";
	string userName = user["name"].get<string>();
	bool isUserNameInput = false;
	msg = str::trim(msg, "。");//删除末尾的句号
	string msgCmd = parseMsg(msg);

	LOG("[xiaoT]收到请求," + msg);

	//问答类处理
	for (auto& it : brain)
	{
		if (msg.find(it.first) != string::npos)
		{
			reply = it.second;
		}
	}

	//判断是否是用户名输入
	if (m_WaitingUserNameInput.find(userName) != m_WaitingUserNameInput.end())
	{
		isUserNameInput = true;
	}

	//控制类处理
	json val, resp, err;
	if (reply == "")
	{
		string tag = "";
		ioDev* pSelDev = ioSrv.getIODev(msgCmd, true);
		if (isUserNameInput)
		{
			userMng.m_mapUsers.erase(user["name"].get<string>());
			user["name"] = msg;
			userMng.m_mapUsers[msg] = user;
			userMng.saveConf();
			reply = "您的用户名为:" + msg;
		}
		else if (pSelDev) //直接选中了某台设备
		{
			string ioAddr = pSelDev->getIOAddrStr();
			user["dlgCtrlIOAddr"] = ioAddr;
			json jErr, jRlt;
			userMng.setUser(user, jRlt, jErr);
			reply = "当前控制设备:" + ioAddr;
		}
		else if (msg.find("用户名") != string::npos)
		{
			m_WaitingUserNameInput[userName] = userName;
			reply = "请以文字方式输入您的用户名";
		}
		else if (msg == m_ctrlAuthCode && m_ctrlAuthCode != "")
		{
			m_ctrlAuthCode = "";
			if (user["role"].get<string>() == "观察员") //防止管理员自己输入授权码变成操作员
				user["role"] = "操作员";
			json jErr, jRlt;
			userMng.setUser(user, jRlt, jErr);
			reply = "授权成功，您可以操控设备了";
		}
		else if (msg.find("授权码") != string::npos)
		{
			if (user["role"] == "管理员")
			{
				reply = generateCtrlAuthCode();
				LOG("当前授权码:" + m_ctrlAuthCode);
			}
		}
		else if (msg.find("打开") != string::npos)
		{
			val = true;
			tag = str::trim(msg, "打开");
		}
		else if (msg.find("关闭") != string::npos)
		{
			val = false;
			tag = str::trim(msg, "关闭");
		}
		else if (isDevMngCmd(msg))
		{
			reply = handleDevMng(msg, user);
		}
		if (isDevCtrlCmd(msgCmd))
		{
			reply = handleDevCtrl(msgCmd, user);
		}

		if (!val.empty())
		{
			MP* pmp = prj.GetDescendantMPByName(tag);
			if (!pmp)
			{
				reply = "没找到需要控制的设备";
			}
			else
			{
				pmp->output(val, resp, err, false);
				reply = "好的";
			}
		}
	}



	if (reply == "")
	{
		reply = "对不起，我还不能处理该请求。";
	}

	return reply;
}


//用阿里云部署的FC函数 代理请求。
std::string CXiaoT::getReply_chatgpt_aliCloud_proxy(string msg, json& user) {
	LOG("[xiaoT]收到请求," + msg);

	string addr = "https://chatgpt-common-bbxpqumamm.us-west-1.fcapp.run";

	json jReq;
	jReq["model"] = "gpt-3.5-turbo";
	json jMessages = json::array();
	json jMsg;
	jMsg["role"] = "user";
	jMsg["content"] = msg;
	jMessages.push_back(jMsg);
	jReq["messages"] = jMessages;
	json jRpc;
	jRpc["method"] = "getCompletions";
	jRpc["params"] = jReq;
	string sReq = jRpc.dump();


	httplib::Client cli(addr);
	cli.set_connection_timeout(0, 2000000); // 300 milliseconds
	cli.set_read_timeout(15, 0); // 5 seconds
	cli.set_write_timeout(15, 0); // 5 seconds
	auto res = cli.Post("/", sReq, "application/json; charset=utf-8");

	if (res != nullptr && res->body != "") {
		json j = json::parse(res->body);
		try
		{
			string sReply = j["result"]["choices"][0]["message"]["content"];
			LOG("[xiaoT]回复," + sReply);
			return sReply;
		}
		catch (const std::exception&)
		{

		}
	}
	return "系统故障，暂时无法回答您的问题";
}

std::string CXiaoT::getReply_chatgpt(string msg, json& user) {
	LOG("[xiaoT]收到请求," + msg);

	string addr = "https://api.openai.com";

	json jReq;
	jReq["model"] = "gpt-3.5-turbo";
	json jMessages = json::array();
	json jMsg;
	jMsg["role"] = "user";
	jMsg["content"] = msg;
	jMessages.push_back(jMsg);
	jReq["messages"] = jMessages;
	string sReq = jReq.dump();


	httplib::Client cli(addr);
	httplib::Headers header = {
		{"Authorization","Bearer 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"}
	};
	auto res = cli.Post("/v1/chat/completions", header, sReq, "application/json; charset=utf-8");

	if (res != nullptr && res->body != "") {
		json j = json::parse(res->body);
		try
		{
			string sReply = j["choices"][0]["message"]["content"];
			LOG("[xiaoT]回复," + sReply);
			return sReply;
		}
		catch (const std::exception&)
		{

		}
	}
	return "系统故障，暂时无法回答您的问题";
}

std::string CXiaoT::getReply(string msg, json& user)
{
	if (m_xiaotMode == "czy") {
		return getReply_czyMode(msg, user);
	}
	else if (m_xiaotMode == "chatgpt") {
		return getReply_chatgpt(msg, user);
	} 
	else if (m_xiaotMode == "aliyunChatgpt") {
		return getReply_chatgpt_aliCloud_proxy(msg, user);
	}

	return "xiaot关机了,联系管理员设置xiaot工作模式";
}

string CXiaoT::parseMsg(string msg)
{
	//将一，二，三替换成1,2,3。语音转文字会把1-10转成中文大写。 10以上则直接转成阿拉伯字母
	msg = str::replace(msg, "一", "1");
	msg = str::replace(msg, "二", "2");
	msg = str::replace(msg, "三", "3");
	msg = str::replace(msg, "四", "4");
	msg = str::replace(msg, "五", "5");
	msg = str::replace(msg, "六", "6");
	msg = str::replace(msg, "七", "7");
	msg = str::replace(msg, "八", "8");
	msg = str::replace(msg, "九", "9");
	msg = str::replace(msg, "十", "10");

	string pinyin;
	if (!str::hanZi2Pinyin(msg, pinyin))
	{
		LOG("[error]无效的gb2312字符串，无法转换拼音首字母，utf8原始数据:" + msg);
	}
	else
	{
		LOG("[拼音首字母转换]" + pinyin);
	}

	if (pinyin.find("s1y") != string::npos || pinyin.find("syy") != string::npos)
		return MSG_CMD::previousPage;
	else if (pinyin.find("x1y") != string::npos || pinyin.find("xyy") != string::npos)
		return MSG_CMD::nextPage;
	else if (msg.find("停止") != string::npos || msg.find("暂停") != string::npos)
		return MSG_CMD::stop;
	else if (msg.find("播放") != string::npos)
		return MSG_CMD::play;

	return pinyin;
}

string CXiaoT::generateCtrlAuthCode()
{
	TIME st;
	timeopt::now(&st);
	string s = str::format("%03d", st.wMilliseconds);
	if (s.length() > 3)
	{
		s = s.substr(s.length() - 3, 3);
	}
	m_ctrlAuthCode = s;
	return s;
}

void CXiaoT::setReplyBuff(string question, string reply)
{
	m_csReply.lock();
	m_mapReplyBuff[question] = reply;
	m_csReply.unlock();
}

string CXiaoT::getReplyFromBuff(string question, string reply)
{
	string s;
	m_csReply.lock();
	map<string, string>::iterator iter = m_mapReplyBuff.find(question);
	if (iter != m_mapReplyBuff.end()) {
		s = iter->second;
	}
	m_csReply.unlock();
	return s;
}
