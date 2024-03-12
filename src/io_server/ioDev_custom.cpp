#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_custom.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "rpcHandler.h"


using namespace httplib;

namespace ns_ioDev_custom {
	ioDev* createDev()
	{
		return new ioDev_custom();
	}
	class createReg{
	public:
		createReg() {
			mapDevCreateFunc["custom-device"] = createDev;
			mapDevTypeLabel["custom-device"] = "自定义设备";
		};
	};
	createReg reg;
}




ioDev_custom::ioDev_custom()
{
	m_devType = "custom-device";
	m_devTypeLabel = "自定义设备";
	m_level = "devcie";
}

ioDev_custom::~ioDev_custom()
{
	stop();
}


void ioDev_custom::DoAcq()
{

}

void thread_do_http_heartbeat(ioDev_custom* pDev) {
	pDev->doHttpHeartbeat();
}

void thread_do_ping_heartbeat(ioDev_custom* pDev) {
	pDev->doPingHeartbeat();
}

void ioDev_custom::doHttpHeartbeat()
{
	string ip = m_jDevAddr["ip"];
	int port = m_jDevAddr["port"].get<int>();
	string addr = "http://" + ip + ":" + str::fromInt(port);
	httplib::Client cli(addr);
	auto res = cli.Get(m_httpHeartbeatUrl);

	if (res != nullptr) {
		setOnline();
	}
	else {
		setOffline();
	}
}

void ioDev_custom::doPingHeartbeat()
{
	string strcmd = "ping -n 1 > null " + m_jDevAddr["ip"];
	int isatus = system(strcmd.c_str());

	if (isatus == 0)
	{
		setOnline();
	}
	else
	{
		setOffline();
	}
}

void ioDev_custom::DoCycleTask()
{
	if (timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > m_heartBeatInterval) {
		m_stLastHeartbeatTime = timeopt::now();
		if (m_bEnableHttpHeartbeat) {
			thread t(thread_do_http_heartbeat, this);
			t.detach();
		}
		
		if (m_bEnablePingOnlineCheck) {
			thread t(thread_do_ping_heartbeat, this);
			t.detach();
		}
	}
}

void ioDev_custom::onEvent_online()
{
	
}

void ioDev_custom::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
}




