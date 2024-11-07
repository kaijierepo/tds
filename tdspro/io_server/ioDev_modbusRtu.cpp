#include "pch.h"
#include "ioDev_modbusRtu.h"
#include "ioGW_localSerial.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"

namespace ns_ioDev_ModbusRtu {
	ioDev* createDev()
	{
		return new ioDev_ModbusRtu();
	}
	class createReg {
	public:
		createReg() {
			mapDevCreateFunc["modbus-rtu-slave"] = createDev;
			mapDevTypeLabel["modbus-rtu-slave"] = "ModbusRTU";
		};
	};
	createReg reg;
}


ioDev_ModbusRtu::ioDev_ModbusRtu(void)
{
	m_devType = "modbus-rtu-slave";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_level = IO_DEV_LEVEL::device;
	m_addrType = DEV_ADDR_MODE::deviceID;
}


ioDev_ModbusRtu::~ioDev_ModbusRtu(void)
{
}

bool ioDev_ModbusRtu::isConnected()
{
	return m_pParent->isConnected();
}


bool ioDev_ModbusRtu::sendADU(MB_PDU& req)
{
	MB_RTU_PKT rtuReq;
	rtuReq.eqp_addr = atoi(getDevAddrStr().c_str());
	rtuReq.pdu = req;
	rtuReq.pack();

	m_transaction.req_adu = str::bytesToHexStr(rtuReq.data, rtuReq.len);

	return sendData(rtuReq.data,rtuReq.len);
}


bool ioDev_ModbusRtu::sendData(unsigned char* pData, size_t iLen)
{
	statisOnSend(pData, iLen, getIOAddrStr());
	if (m_pParent)
	{
		return m_pParent->sendData(pData, iLen);
	}
	return false;
}


bool ioDev_ModbusRtu::onRecvData(unsigned char* pData, size_t iLen) {
	return ioDev_ModbusSlave::onRecvData(pData, iLen);
}

bool ioDev_ModbusRtu::onRecvPkt(unsigned char* pData, size_t iLen)
{
	MB_RTU_PKT respPkt;
	respPkt.setData(pData, iLen);
	respPkt.unpack();
	int mbId = atoi(getDevAddrStr().c_str());

	if (respPkt.eqp_addr != mbId)
		return false;

	setOnline();
	string adu_resp = str::bytesToHexStr(pData, iLen);
	m_transaction.setResp(&respPkt.pdu,adu_resp);
	return true;
}
