#include "pch.h"
#include "ioGW_tuyaProject.h"
#include "httplib.h"
#include <thread>
#define PBKDF2_SHA256_IMPLEMENTATION
#include "pbkdf2_sha256.h"
#include "logger.h"
#include "json.hpp"
#include "ioDev_tuya.h"


void tuyaProjectInitThread(ioGW_tuyaProject* pGw)
{
	while (1)
	{
		while (pGw->m_accessToken == "")
		{
			//get access token
			string client_id = pGw->m_addr;
			string secret = pGw->m_secret;
			string time = str::format("%lld", timeopt::getTick());

			uint8_t out[SHA256_DIGESTLEN] = { 0 };
			string data = client_id + time;
			hmac_sha256_calc(out, (uint8_t*)data.data(), data.length(), (uint8_t*)secret.data(), secret.length());

			string sign = str::fromBytes((char*)out, SHA256_DIGESTLEN);
			pGw->m_sign = sign;

			string host = "https://openapi.tuyacn.com";
			string path = "/v1.0/token?grant_type=1";
			string url = host + path;

			httplib::Client cli(host.c_str());
			httplib::Headers headers = {
				{"client_id", client_id },
				{"sign",sign },
				{"t",time},
				{"sign_method","HMAC-SHA256"}
			};


			if (auto res = cli.Get(path.c_str(), headers)) {
				if (res->status == 200) {
					string tokenInfo = res->body;
					json j = json::parse(tokenInfo);
					if (!j.empty())
					{
						if (j["result"] != nullptr)
						{
							json jResult = j["result"];
							pGw->m_accessToken = jResult["access_token"];
							pGw->m_refreshToken = jResult["refresh_token"];
						}
					}
					LOG("request " + url + "return " + tokenInfo);
				}
				else
				{
					LOG("request " + url + "return status " + str::fromInt(res->status));
				}
			}
			else {
				auto err = res.error();
				LOG("accessing https://openapi.tuyacn.com failed,retry after 5 seconds");
				Sleep(5000);
			}
		}

		//get current status of devices
		for (auto& it : pGw->m_vecChild)
		{
			if (it->m_devType.find("tuya") != string::npos)
			{
				ioDev_tuya* pty = (ioDev_tuya*)it;
				pty->getCurrentVal();
			}
		}

		Sleep(1000 * 60 * 5);
	}
}

bool ioGW_tuyaProject::run()
{	
	std::thread t(tuyaProjectInitThread,this);
	t.detach();

	return true;
}

bool ioGW_tuyaProject::outputVal(json jVal, string chanAddr)
{
	return false;
}

bool ioGW_tuyaProject::inputVal(json jVal, string chanAddr)
{
	return false;
}
