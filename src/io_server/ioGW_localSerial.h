#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"

class ioGW_LocalSerial : public ioDev
{
public:
	ioGW_LocalSerial(void);
	~ioGW_LocalSerial(void);

	/*
	use json string as conf
	{
	   "port":"COM1",
	   "baudRate" : 19200,
	   "byteSize" : 8,
	   "parity" : 0,
	   "stopBits" : 0
	}
	*/

	bool OpenCom(string conf); // call openCom before run
	bool closeCom(); 
	bool OnRecvData(char* pData, int iLen) override;
	bool run() override;

	void SendData(char* pData, int iLen);

	bool ReadCom(char* buf, int& len);
	bool WriteCom(char* buf, int len);
	HANDLE m_hCom;
	string m_portNum;
	int m_baudRate;
	int m_byteSize;
	int m_parity;
	int m_stopBits;
	string m_strErrorInfo;
	HANDLE m_hRecvThread;

	OVERLAPPED m_ovWaitEvent;
	OVERLAPPED m_ovRead;
	OVERLAPPED m_ovWrite;

	std::shared_ptr<TDS_SESSION> pTdsSession;
};

