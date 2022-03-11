#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"

class ioGW_rs485 : public ioDev
{
public:
	ioGW_rs485(void);
	~ioGW_rs485(void);

	bool isAcqing() override;

	bool OnRecvData(char* pData, int iLen) override;
	bool onRecvPkt(char* pData, int iLen) override;
	bool run() override;
	bool sendData(char* pData, int iLen) override;
	void checkAcqReqTimeout() override;
	void DoCycleTask() override;

	stream2pkt m_stream2pkt;

	string m_strErrorInfo;
};

