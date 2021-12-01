#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioGW_tuyaProject.h"
#include "ioDev_tuya.h"
#include "pbkdf2_sha256.h"
#include "ioChan.h"
#include "logger.h"
using namespace httplib;


bool ioDev_tuya::getCurrentVal()
{
	//get gateway
	ioGW_tuyaProject* pGw = nullptr;
	if (m_pParent &&  m_pParent->m_devType == "tuya-iot-project")
	{
		pGw = (ioGW_tuyaProject*)m_pParent;
	}
	else
		return false;

	string client_id = pGw->m_devAddr;
	string secret = pGw->m_secret;
	string accessToken = pGw->m_accessToken;
	string time = str::format("%lld", timeopt::getTick());

	uint8_t out[SHA256_DIGESTLEN] = { 0 };
	string data = client_id + accessToken + time;
	hmac_sha256_calc(out, (uint8_t*)data.data(), data.length(), (uint8_t*)secret.data(), secret.length());

	string sign = str::fromBytes((char*)out, SHA256_DIGESTLEN);

	httplib::Client cli("https://openapi.tuyacn.com");
	httplib::Headers headers = {
		{"client_id", client_id },
		{"access_token", accessToken},
		{"sign",sign },
		{"t",time},
		{"sign_method","HMAC-SHA256"}
	};

	string url = "/v1.0/devices/" + m_devAddr;
	auto res = cli.Get(url.c_str(), headers);

	if (res) {
		if (res->status == 200) {

		}
		LOG("accessing" + url +  "success,response : " + res->body);

		json j = json::parse(res->body);
		if (!j.empty())
		{
			if (j["result"]!=nullptr)
			{
				json result = j["result"];
				if (result["status"] != nullptr)
				{
					json statusList = result["status"];
					for (auto& it : statusList)
					{
						string code = it["code"].get<string>();
						if (code.find("switch_") == 0)
						{
							string strChan = code.substr(7, code.length() - 7);
							ioDev* pChild = getChild(strChan);
							if (pChild->m_level == "channel")
							{
								ioChannel* pC = (ioChannel*)pChild;
								pC->input(it["value"]);
								return true;
							}
						}
					}
				}
			}
		}
	}
	else {
		auto err = res.error();
		LOG("accessing https://openapi.tuyacn.com/v1.0/devices/{{device_id}}/commands failed");
		return false;
	}
	return false;
}

bool ioDev_tuya::output(string chanAddr, json jVal, json& chanResp,bool sync)
{
	//get gateway
	ioGW_tuyaProject* pGw = nullptr;
	if (m_pParent && m_pParent->m_devType == "tuya-iot-project")
	{
		pGw = (ioGW_tuyaProject*)m_pParent;
	}
	else
		return false;

	//get device
	ioDev_tuya* pDev = this;
	
	string client_id = pGw->m_devAddr;
	string secret = pGw->m_secret;
	string accessToken = pGw->m_accessToken;
	string time = str::format("%lld", timeopt::getTick());

	uint8_t out[SHA256_DIGESTLEN] = { 0 };
	string data = client_id + accessToken + time;
	hmac_sha256_calc(out, (uint8_t*)data.data(), data.length(), (uint8_t*)secret.data(), secret.length());

	string sign = str::fromBytes((char*)out, SHA256_DIGESTLEN);

	httplib::Client cli("https://openapi.tuyacn.com");
	httplib::Headers headers = {
		{"client_id", client_id },
		{"access_token", accessToken},
		{"sign",sign },
		{"t",time},
		{"sign_method","HMAC-SHA256"}
	};

	json j;
	std::string body = R"delimiter(
	{
		"commands": [
			{
				"code": "switch_{{1}}",
				"value" : {{2}}
			}
		]
	}
	)delimiter";

	string val = jVal.dump();
	str::replace(body, "{{1}}", m_devAddr);
	str::replace(body, "{{2}}", val);

	string url = "/v1.0/devices/" + pDev->m_devAddr + "/commands";
	auto res = cli.Post(url.c_str(), headers, body, "text/plain");

	if (res) {
		if (res->status == 200) {

		}
		LOG("accessing https://openapi.tuyacn.com/v1.0/devices/{{device_id}}/commands success,response : " + res->body);

		json j = json::parse(res->body);
		if (!j.empty())
		{
			if (j["success"] == true)
			{
				//input(jVal);
				return true;
			}
		}
	}
	else {
		auto err = res.error();
		LOG("accessing https://openapi.tuyacn.com/v1.0/devices/{{device_id}}/commands failed");
		return false;
	}

	return false;
}