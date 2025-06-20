#include "pch.h"
#include "ioGW_tuyaProject.h"
#include "httplib.h"
#include <thread>
#define PBKDF2_SHA256_IMPLEMENTATION
#include "pbkdf2_sha256.h"
#include "logger.h"
#include "json.hpp"
#include "ioDev_tuya.h"
#include "ioSrv.h"


ioDev* createDev_tuyaProject()
{
	return new ioGW_tuyaProject();
}

class createReg_tuyaProject {
public:
	createReg_tuyaProject() {
		mapDevCreateFunc[IO_DEV_TYPE::GW::tuya_iot_project] = createDev_tuyaProject;
	};
};
createReg_tuyaProject regTuyaProject;


void tuyaProjectInitThread(ioGW_tuyaProject* pGw)
{
	while (1)
	{
		while (pGw->m_accessToken == "")
		{
			if (!pGw->m_jDevAddr.contains("id"))
			{
				return;
			}
			if (!pGw->m_jDevAddr.contains("secret"))
			{
				return;
			}


			//get access token
			string client_id = pGw->m_jDevAddr["id"].get<string>();
			string secret = pGw->m_jDevAddr["secret"].get<string>();
			string time = str::format("%lld", timeopt::getTick());

			uint8_t out[SHA256_DIGESTLEN] = { 0 };
			string data = client_id + time;
			hmac_sha256_calc(out, (uint8_t*)data.data(), data.length(), (uint8_t*)secret.data(), secret.length());

			string sign = str::bytesToHexStr((char*)out, SHA256_DIGESTLEN,"");
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
						else
						{
							LOG("[error][涂鸦平台]query access token of tuya project failed.please check project_id and key config for tuya project. sign in tuya iot platform to get these config");
							return;
						}
					}
					LOG("[涂鸦平台]request " + url + "return " + tokenInfo);
				}
				else
				{
					LOG("[涂鸦平台]request " + url + "return status " + str::fromInt(res->status));
				}
			}
			else {
				auto err = res.error();
				LOG("[涂鸦平台]accessing https://openapi.tuyacn.com failed,retry after 5 seconds");
				timeopt::sleepMilli(5000);
			}
		}

		//get current status of devices
		for (auto& it : pGw->m_vecChildDev)
		{
			if (it->m_devType.find("tuya") != string::npos)
			{
				ioDev_tuya* pty = (ioDev_tuya*)it;
				pty->getCurrentVal();
			}
		}

		timeopt::sleepMilli(1000 * 60 * 5);
	}
}

ioGW_tuyaProject::ioGW_tuyaProject()
{
	m_devType = IO_DEV_TYPE::GW::tuya_iot_project;
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_parentDevType = "tds";
	m_level = IO_DEV_LEVEL::gateway;
	m_addrMode = DEV_ADDR_MODE::deviceID;
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
