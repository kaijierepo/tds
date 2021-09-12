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

	bool OpenCom(string confPort,int baudRate,int parity,int byteSize,int stopBits); // call openCom before run
	bool OpenCom();
	bool isOpen();
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

	//´®¿Ú²ÎÊý
	string m_portNum;
	int m_baudRate;
	int m_byteSize;  
	string m_parity;//None,Odd,Even,Mark,Space
	string m_stopBits; //1 , 1,5 ,2 


	string m_strErrorInfo;
	HANDLE m_hRecvThread;
	OVERLAPPED m_ovWaitEvent;
	OVERLAPPED m_ovRead;
	OVERLAPPED m_ovWrite;


};

