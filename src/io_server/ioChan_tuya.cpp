#include "pch.h"
#include "ioChan_tuya.h"
#include "httplib.h"
#include "json.hpp"
#include "ioGW_tuyaProject.h"
#include "ioDev_tuya.h"
#include "pbkdf2_sha256.h"
#include "logger.h"
using namespace httplib;

bool ioChan_tuya::outputVal(json jVal)
{
	//get gateway
	ioGW_tuyaProject* pGw = nullptr;
	if (m_pParent && m_pParent->m_pParent && m_pParent->m_pParent->m_devType == "tuya-iot-project")
	{
		pGw = (ioGW_tuyaProject*)m_pParent->m_pParent;
	}
	else
		return false;

	//get device
	ioDev_tuya* pDev = nullptr;
	if (m_pParent && m_pParent->m_devType.find("tuya") != string::npos)
	{
		pDev = (ioDev_tuya*)m_pParent;
	}
	else
		return false;


	string client_id = pGw->m_addr;
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
	str::replace(body, "{{1}}", m_addr);
	str::replace(body, "{{2}}", val);

	string url = "/v1.0/devices/" + pDev->m_addr + "/commands";
	auto res = cli.Post(url.c_str(), headers,body,"text/plain");

	if (res) {
		if (res->status == 200) {
			
		}
		LOG("accessing https://openapi.tuyacn.com/v1.0/devices/{{device_id}}/commands success,response : " + res->body);
		
		json j = json::parse(res->body);
		if (!j.empty())
		{
			if (j["success"] == true)
			{
				inputVal(jVal);
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


