#include "pch.h"
#include "ioGW_rs485.h"
#include "commSrv.h"
#include "logger.h"
#include "tdsSession.h"


void ioGW_rs485_acqThread(ioGW_rs485* gw)
{
	gw->m_bWorkingThreadRunning = true;
	while (1)
	{
		if (!gw->m_bRunning)
			break;
		gw->DoCycleTask();
		Sleep(100);
	}
	gw->m_bWorkingThreadRunning = false;
	gw->m_signalWorkThreadExit.notify();
}


ioGW_rs485::ioGW_rs485(void)
{
	m_devType = IO_DEV_TYPE::GW::rs485_gateway;
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_parentDevType = IO_DEV_TYPE::SERVER::tds;
	m_level = "gateway";
	thread t(ioGW_rs485_acqThread, this);
	t.detach();
}


ioGW_rs485::~ioGW_rs485(void)
{	
	stop();
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
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* pChild = m_vecChildDev[i];
		pChild->DoCycleTask();
	}
}



bool ioGW_rs485::OnRecvData(char* pData, int iLen )
{
	if (m_bEnableIoLog)
		statisOnRecv(pData, iLen, getIOAddrStr());

	for(int i = 0;i<m_vecChildDev.size();i++)
	{
		m_vecChildDev.at(i)->OnRecvData(pData,iLen);
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


