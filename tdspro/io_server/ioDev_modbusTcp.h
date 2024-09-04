#pragma once
#include "ioDev_modbusSlave.h"
#include "proto_rtu.hpp"


class ioDev_ModbusTcp : public ioDev_ModbusSlave
{
public:
	ioDev_ModbusTcp(void);
	~ioDev_ModbusTcp(void);

	bool isOnline() override;
	bool sendADU(MB_PDU& req) override;
	bool onRecvPkt(unsigned char* pData, size_t iLen) override;
	bool isConnected() override;
	bool sendData(unsigned char* pData, size_t iLen) override;
	void OnRecvData_TCPClient(unsigned char* pData, size_t len, tcpSessionClt* connInfo);
	unsigned short m_transId;
};


