#include "pch.h"
#include "ioGW_rs485.h"
#include "commSrv.h"
#include "logger.h"
#include "tdsSession.h"


void ioGW_rs485_acqThread(ioGW_rs485* gw)
{
	std::unique_lock<mutex> lock(gw->m_csThis);
	while (1)
	{
		if (!gw->m_bRunning)
			break;
		gw->DoCycleTask();
		Sleep(100);
	}
}


ioGW_rs485::ioGW_rs485(void)
{
	m_devType = IO_DEV_TYPE::GW::rs485_gateway;
	m_devTypeLabel = "RS485网关";
	m_parentDevType = IO_DEV_TYPE::SERVER::tds;
	m_level = "gateway";
	thread t(ioGW_rs485_acqThread, this);
	t.detach();
}


ioGW_rs485::~ioGW_rs485(void)
{	
	m_bRunning = false;
	m_csThis.lock();
	m_csThis.unlock();
}

bool ioGW_rs485::run()
{

	return true;
}


bool ioGW_rs485::sendData(char* pData, int iLen)
{
	unique_lock<mutex> lock(m_csIOSession);
	if (pIOSession)
	{
		pIOSession->send(pData, iLen);
		if (m_bEnableIoLog)
			statisOnSend((char*)pData, iLen, getIOAddrStr());
	}
	else
		return false;
	return true;
}

void ioGW_rs485::DoCycleTask()
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* pChild = m_vecChild[i];
		pChild->DoCycleTask();
	}
}



bool ioGW_rs485::OnRecvData(char* pData, int iLen )
{
	if (m_bEnableIoLog)
		statisOnRecv(pData, iLen, getIOAddrStr());

	for(int i = 0;i<m_vecChild.size();i++)
	{
		m_vecChild.at(i)->OnRecvData(pData,iLen);
	}

	if (pTdsSession)
	{
		pTdsSession->send(pData, iLen);
	}

	if (m_pRecvCallback)
	{
		m_pRecvCallback(m_pCallbackUser, pData, iLen);
	}

	return true;
}


