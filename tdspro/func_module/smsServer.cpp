#include "pch.h"
#include "smsServer.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "userMng.h"
#include "common.h"
#include "httplib.h"
#include "logger.h"

SmsServer smsServer;


SmsServer::SmsServer()
{
	tds->smsServer = this;
}

bool SmsServer::init()
{
	m_url = tds->conf->smsApiUrl;
	m_user = tds->conf->smsApiUser;
	m_key = tds->conf->smsApiKey;

	if (m_user != "" && m_key != "")
	{
		LOG("[短信服务  ] 已启用");
	}
	else
	{
		LOG("[短信服务  ] 未启用，请配置短信平台账号与key");
	}
	return true;
}

bool SmsServer::run()
{
	return false;
}

bool SmsServer::send(string& msg, string& phoneNum)
{
	if (m_user == "" || m_key == "")
		return false;

	httplib::Client cli("https://api.4321.sh");

	httplib::Params params{
		  { "apikey", m_user },
		  { "secret", m_key },
		  {"mobile",phoneNum},
		  {"sign_id","149855"},
		  {"content",msg}
	};

	auto res = cli.Post("/sms/send", params);
	return true;
}

bool SmsServer::send(json& params, string& phoneNum)
{
	if (m_user == "" || m_key == "")
		return false;

	string tplId = params["templateId"];
	string content = params["content"];

	httplib::Client cli("https://api.4321.sh");
	httplib::Params httpparams{
		  { "apikey", m_user },
		  { "secret", m_key },
		  {"mobile",phoneNum},
		  {"template_id",tplId},  //137880  官网验证码 模版
		  {"sign_id","176344"},   //176344  短信签名  良途软件
		  {"content",content}
	};

	auto res = cli.Post("/sms/template", httpparams);
	if (res->body != "") {
		json jResp = json::parse(res->body);
		if (jResp.contains("code")) {
			int code = jResp["code"].get<int>();

			if (code == 0) {
				return true;
			}
		}
	}
	return false;
}


//该函数与飞鸽传书平台绑定
bool SmsServer::sendVerificationCode(string phoneNum)
{
	if (m_user == "" || m_key == "")
		return false;

	int code = common::randomInt(1000, 9999);
	string sCode = str::format("%04d", code);

	httplib::Client cli("https://api.4321.sh");
	httplib::Params params{
		  { "apikey", m_user },
		  { "secret", m_key },
		  {"mobile",phoneNum},
		  {"template_id","137880"},  //137880  官网验证码 模版
		  {"sign_id","176344"},   //176344  短信签名  良途软件
		  {"content",sCode}
	};

	auto res = cli.Post("/sms/template", params);
	if (res->body != "") {
		json jResp = json::parse(res->body);
		if (jResp.contains("code")) {
			int code = jResp["code"].get<int>();

			if (code == 0) {
				return true;
			}
		}
	}
	
	return false;
}

bool SmsServer::checkVerificationCode(string phoneNum,string code)
{
	if (m_mapVeriCode.find(phoneNum) == m_mapVeriCode.end()) {
		return false;
	}
	string realCode = m_mapVeriCode[phoneNum];

	if (realCode != code)
		return false;
	return true;
}

