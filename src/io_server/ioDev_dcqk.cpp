#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_dcqk.h"
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

namespace ns_ioDev_dcqk {
	ioDev* createDev()
	{
		return new ioDev_dcqk();
	}
	class createReg{
	public:
		createReg() {
			mapDevCreateFunc["dcqk-sys-device"] = createDev;
			mapDevTypeLabel["dcqk-sys-device"] = "自定义设备";
		};
	};
	createReg reg;
}




ioDev_dcqk::ioDev_dcqk()
{
	m_devType = "custom-device";
	m_devTypeLabel = "自定义设备";
	m_level = "devcie";
}

ioDev_dcqk::~ioDev_dcqk()
{
	stop();
}


void ioDev_dcqk::DoAcq()
{

}



void ioDev_dcqk::DoCycleTask()
{
	
}

void ioDev_dcqk::onEvent_online()
{
	
}

void ioDev_dcqk::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
}




