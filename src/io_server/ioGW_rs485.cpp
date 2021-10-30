#include "pch.h"
#include "ioGW_rs485.h"
#include "commSrv.h"
#include "logger.h"
#include "tdsSession.h"



ioGW_rs485::ioGW_rs485(void)
{
	m_devType = IO_DEV_TYPE::GW::rs485_gateway;
	m_devTypeLabel = "RS485网关";
	m_parentDevType = IO_DEV_TYPE::SERVER::tds;
	m_level = "gateway";
}


ioGW_rs485::~ioGW_rs485(void)
{	m_csThis.lock();
	m_csThis.unlock();
}

bool ioGW_rs485::run()
{

	return true;
}


bool ioGW_rs485::sendData(char* pData, int iLen)
{

	return 1;
}



bool ioGW_rs485::OnRecvData(char* pData, int iLen )
{
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


