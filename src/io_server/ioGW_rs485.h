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
	bool run() override;
	bool sendData(char* pData, int iLen) override;

	void DoCycleTask() override;

	string m_strErrorInfo;
};

