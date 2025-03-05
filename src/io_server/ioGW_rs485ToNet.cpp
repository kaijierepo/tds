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
		if (p->m_bIsWaitingResp || p->m_bCycleAcqThreadRunning)
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


void thread_485toNetCycleTask(ioGW_rs485ToNet* p) {
	for (int i = 0; i <p->m_vecChildDev.size(); i++)
	{
		ioDev* pChild = p->m_vecChildDev[i];
		pChild->DoCycleTaskSync();

		while (1) {
			if (!p->isBusBusy())
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		}
	}
	p->m_bCycleAcqThreadRunning = false;
}



void ioGW_rs485ToNet::DoCycleTask()
{
	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning) {
			m_bCycleAcqThreadRunning = true;
			thread t(thread_485toNetCycleTask, this);
			t.detach();
			m_stLastAcqTime = timeopt::now();
		}
	}
}


bool ioGW_rs485ToNet::isBusBusy() {
	if (ioSrv.m_serialSendDelayAfterRecv > 0) {
		if (timeopt::calcTimePassMilliSecond(m_lastBusRecvTime) < ioSrv.m_serialSendDelayAfterRecv) {
			return true;
		}
	}
	return false;
}

bool ioGW_rs485ToNet::sendData(unsigned char* pData, size_t iLen)
{
	m_lastBusSendTime = timeopt::now();
	return ioDev::sendData(pData, iLen);
}

void ioGW_rs485ToNet::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	stream2pkt* pab = &m_pab;
	pab->PushStream(recvData, recvDataLen);


	while (pab->PopPkt(IsValidPkt_ModbusRTU))
	{
		if (pab->abandonData != "")
		{
			string remoteAddr = udpSession.getRemoteIOAddr();
			LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
		}
		onRecvData(pab->pkt, pab->iPktLen);
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
	setOnline();

	m_lastBusRecvTime = timeopt::now();

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
	setOnline();
	int addr = pData[0];

	ioDev* p = getChild(str::fromInt(addr));
	if (p)
	{
		p->onRecvPkt(pData, iLen);
	}
	return true;
}


