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
#include "as.h"

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



void ioDev_custom::DoCycleTask()
{
	
}

void ioDev_custom::onEvent_online()
{
	
}

void ioDev_custom::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
}




