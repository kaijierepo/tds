#include "pch.h"
#include "ioGW_rs485ToNet.h"
#include "logger.h"
#include "tdsSession.h"
#include "ioSrv.h"


namespace ns_ioGW_rs485 {
	ioDev* createDev()
	{
		return new ioGW_rs485ToNet();
	}
	class createReg {
	public:
		createReg() {
			mapDevCreateFunc["rs485-gateway"] = createDev;
			mapDevTypeLabel["rs485-gateway"] = "RS485转网络";
		};
	};
	createReg reg;
}


ioGW_rs485ToNet::ioGW_rs485ToNet(void)
{
	m_devType = "rs485-gateway";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_level = "gateway";
}


ioGW_rs485ToNet::~ioGW_rs485ToNet(void)
{	
	stop();
}

void ioGW_rs485ToNet::stop()
{
	m_bRunning = false;
	if (m_tcpClt)
		m_tcpClt->stop();


}

string ioGW_rs485ToNet::getConnInfo()
{
	return string();
}


bool ioGW_rs485ToNet::isCommBusy()
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_bIsWaitingResp)
		{
			return true;
		}
	}
	return false;
}


void ioGW_rs485ToNet::checkAcqReqTimeout()
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		p->checkAcqReqTimeout();
	}
}

void ioGW_rs485ToNet::DoCycleTask()
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* pChild = m_vecChildDev[i];
		pChild->DoCycleTask();
	}
}



bool ioGW_rs485ToNet::isConnected()
{
	//udp为无连接模式，默认认为已经连接
	if (m_addrType == DEV_ADDR_MODE::udpServer || m_addrType == DEV_ADDR_MODE::udpClient) {
		return true;
	}

	if (pIOSession)
		return true;
	return false;
}

bool ioGW_rs485ToNet::onRecvData(unsigned char* pData, size_t iLen )
{
	if (!m_bRunning)
		return false;

	for(int i = 0;i<m_vecChildDev.size();i++)
	{
		m_vecChildDev.at(i)->onRecvData(pData,iLen);
	}

	if (pSessionClientBridge)
	{
		pSessionClientBridge->send(pData, iLen);
	}

	if (m_pRecvCallback)
	{
		m_pRecvCallback(m_pCallbackUser, pData, iLen);
	}

	return true;
}

bool ioGW_rs485ToNet::onRecvPkt(unsigned char* pData, size_t iLen)
{
	int addr = pData[0];

	ioDev* p = getChild(str::fromInt(addr));
	if (p)
	{
		p->onRecvPkt(pData, iLen);
	}
	return true;
}


