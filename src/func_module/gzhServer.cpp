#include "pch.h"
#include "gzhServer.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "userMng.h"
#include "xiaot.h"
#include "common.h"

GzhServer gzhServer;


GzhServer::GzhServer()
{
	tds->gzhServer = this;
}

bool GzhServer::init()
{
	return true;
}

bool GzhServer::run()
{
	return false;
}


std::string GzhServer::getReply(string msg)
{
	json reqXiaoT;
	string openID = getOpenId(msg);
	string type = getMsgType(msg);
	string gzhID = getToUserName(msg);
	m_GzhID = gzhID;
	string xiaotMsg;
	if (type == "voice")
	{
		xiaotMsg = getRecognition(msg);
	}
	else if (type == "event")
	{
		string eventType = getEvent(msg);
		if (eventType == "subscribe")
		{
			string welcomeMsg = "欢迎使用智慧物联系统";
			string gzhResp = packGzhResp(welcomeMsg, openID);
			return gzhResp;
		}
	}
	else
	{
		xiaotMsg = getContent(msg);
	}

	json jUser = userMng.getUserByOpenID(openID);
	if (jUser == nullptr)
	{
		jUser = json::object();
		jUser["name"] = openID;
		jUser["gzhOpenID"] = openID;
		jUser["enable"] = true;
		jUser["pwd"] = "123";
		jUser["role"] = "观察员";
		jUser["org"] = "";

		RPC_RESP resp;
		RPC_SESSION session;
		session.org = "";
		userMng.rpc_addUser(jUser,resp,session);
	}

	string xiaotResp = xiaot.getReply(xiaotMsg, jUser);	
	string gzhResp = packGzhResp(xiaotResp, openID);

	return gzhResp;
}

string GzhServer::packGzhResp(string msg,string openID)
{
	string gzhResp = R"(
	<xml>
		<ToUserName><![CDATA[$toUser$]]></ToUserName>
		<FromUserName><![CDATA[$gzhID$]]></FromUserName>
		<CreateTime>$time$</CreateTime>
		<MsgType><![CDATA[text]]></MsgType>
		<Content><![CDATA[$content$]]></Content>
	</xml>
	)";

	gzhResp = str::replace(gzhResp, "$gzhID$", m_GzhID);
	gzhResp = str::replace(gzhResp, "$toUser$", openID);
	time_t t = time(NULL);
	string strTime = str::fromInt((size_t)t);
	gzhResp = str::replace(gzhResp, "$time$", strTime);
	gzhResp = str::replace(gzhResp, "$content$", msg);
	return gzhResp;
}

string GzhServer::getOpenId(string msg)
{
	std::smatch m;
	std::regex e(R"(<FromUserName><!\[CDATA\[(.*)\]\]></FromUserName>)");
	while (std::regex_search(msg, m, e))
	{
		if (m.size() > 1)
		{
			string s = m[1];
			return s;
		}
	}

	return "";
}

string GzhServer::getRecognition(string msg)
{
	std::smatch m;
	std::regex e(R"(<Recognition><!\[CDATA\[(.*)\]\]></Recognition>)");
	while (std::regex_search(msg, m, e))
	{
		if (m.size() > 1)
		{
			string s = m[1];
			return s;
		}
	}

	return "";
}

string GzhServer::getMsgType(string msg)
{
	std::smatch m;
	std::regex e(R"(<MsgType><!\[CDATA\[(.*)\]\]></MsgType>)");
	while (std::regex_search(msg, m, e))
	{
		if (m.size() > 1)
		{
			string s = m[1];
			return s;
		}
	}

	return "";
}

string GzhServer::getContent(string msg)
{
	std::smatch m;
	std::regex e(R"(<Content><!\[CDATA\[(.*)\]\]></Content>)");
	while (std::regex_search(msg, m, e))
	{
		if (m.size() > 1)
		{
			string s = m[1];
			return s;
		}
	}

	return "";
}

string GzhServer::getEvent(string msg)
{
	std::smatch m;
	std::regex e(R"(<Event><!\[CDATA\[(.*)\]\]></Event>)");
	while (std::regex_search(msg, m, e))
	{
		if (m.size() > 1)
		{
			string s = m[1];
			return s;
		}
	}

	return "";
}

string GzhServer::getToUserName(string msg)
{
	std::smatch m;
	std::regex e(R"(<ToUserName><!\[CDATA\[(.*)\]\]></ToUserName>)");
	while (std::regex_search(msg, m, e))
	{
		if (m.size() > 1)
		{
			string s = m[1];
			return s;
		}
	}

	return "";
}
