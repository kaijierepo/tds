#pragma once
#include "ioDev.h"
#include "tcpClt.h"

class ioGW_LocalSerial : public ioDev
{
public:
	ioGW_LocalSerial(void);
	~ioGW_LocalSerial(void);
	bool OpenCom();
	bool OnRecvData(char* pData, int iLen) override;
	bool run() override;

	void SendData(char* pData, int iLen);

	bool ReadCom(LPVOID buf, int& len);
	bool WriteCom(LPVOID buf, int len);
	HANDLE m_hCom;
	string m_strBaudrate;
};

