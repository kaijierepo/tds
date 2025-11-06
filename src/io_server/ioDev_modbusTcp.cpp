#include "pch.h"
#include "ioDev_modbusTcp.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"
#include "webSrv.h"


namespace ns_ioDev_ModbusTcp {
	ioDev* createDev()
	{
		return new ioDev_ModbusTcp();
	}
	class createReg {
	public:
		createReg() {
			mapDevCreateFunc["modbus-tcp-slave"] = createDev;
			mapDevTypeLabel["modbus-tcp-slave"] = "ModbusTCP";
		};
	};
	createReg reg;
}


ioDev_ModbusTcp::ioDev_ModbusTcp(void)
{
	m_devType = "modbus-tcp-slave";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_level = IO_DEV_LEVEL::device;
	m_addrType = DEV_ADDR_MODE::tcpServer;
	m_bCycleAcqThreadRunning = false;
	m_transId = 0;
}


ioDev_ModbusTcp::~ioDev_ModbusTcp(void)
{
}

bool ioDev_ModbusTcp::isOnline()
{
	if (pIOSession == nullptr)
		return false;
	return true;
}


bool ioDev_ModbusTcp::sendADU(MB_PDU& req)
{
	MB_TCP_PKT tcpReq;
	tcpReq.setTransId(m_transId++);
	tcpReq.setPayloadLen(1 + req.len);

	if (m_jDevAddr["unitID"].is_number_integer()) {
		tcpReq.unitId = (unsigned char)m_jDevAddr["unitID"].get<int>();
	}
	else if (m_jDevAddr["unitID"].is_string()){
		string s = m_jDevAddr["unitID"].get<string>();
		tcpReq.unitId = (unsigned char)atoi(s.c_str());
	}
	else {
		tcpReq.unitId = 0;
		LOG("[error][ModbusTCP] unitID没有设置，请设置为modbusSlave的地址，设备地址:%s", getDevAddrStr().c_str());
	}
	tcpReq.pdu = req;
	tcpReq.pack();
	return sendData(tcpReq.data, tcpReq.len);
}


bool ioDev_ModbusTcp::sendData(unsigned char* pData, size_t iLen)
{
	unique_lock<mutex> lock(m_csIOSession);
	if (pIOSession)
	{
		pIOSession->send(pData, iLen);
		if (m_bEnableIoLog)
			statisOnSend(pData, iLen, getIOAddrStr());
	}
	else
		return false;
	return true;
}

void ioDev_ModbusTcp::onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo)
{
	IOLogRecv(pData, len, connInfo->getRemoteAddr(), connInfo->getLocalAddr(), getIOAddrStr());

	stream2pkt* pab = &m_pab;
	pab->PushStream(pData, len);
	while (pab->PopPkt(IsValidPkt_ModbusTcp, false))
	{
		onRecvPkt(pab->pkt, pab->iPktLen);
	}
}


bool ioDev_ModbusTcp::onRecvPkt(unsigned char* pData, size_t iLen)
{
	setOnline();
	MB_TCP_PKT respPkt;
	respPkt.setData((unsigned char*)pData, iLen);
	respPkt.unpack();

	string aduResp = str::bytesToHexStr(pData, iLen);
	m_transaction.setResp(&respPkt.pdu, aduResp);
	return true;
}



bool ioDev_ModbusTcp::isConnected()
{
	if (pIOSession != nullptr && pIOSession->isConnected())
	{
		return true;
	}
	return false;
}
