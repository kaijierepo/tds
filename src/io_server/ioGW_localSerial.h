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
	int parseStopBits(string s);
	int parseParity(string s);

	bool OnRecvData(char* pData, int iLen) override;
	bool run() override;

	bool sendData(char* pData, int iLen) override;

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


};

