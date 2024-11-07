#pragma once
#include "ioDev_modbusSlave.h"
#include "proto_rtu.hpp"


class ioDev_ModbusRtu : public ioDev_ModbusSlave
{
public:
	ioDev_ModbusRtu(void);
	~ioDev_ModbusRtu(void);
	bool isConnected() override;
	bool sendADU(MB_PDU& req) override;
	bool onRecvData(unsigned char* pData, size_t iLen) override;
	bool onRecvPkt(unsigned char* pData, size_t iLen) override;
	bool sendData(unsigned char* pData, size_t iLen) override;
};


