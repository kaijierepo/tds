#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"

#ifdef ENABLE_SERIAL

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
	bool toJson(json& conf, DEV_QUERIER querier) override;
	bool loadConf(json& conf) override;
	string getConnInfo() override;
	bool isConnected() override;
	bool connect() override;
	bool disconnect() override;
	bool connect(json params) override;
	bool OpenCom(string confPort,int baudRate,int parity,int byteSize,int stopBits); // call openCom before run
	bool OpenCom();
	bool isOpen();
	bool OpenCom(string conf); // call openCom before run
	bool closeCom();
	int parseStopBits(string s);
	int parseParity(string s);

	bool onRecvData(unsigned char* pData, size_t iLen) override;
	bool run() override;
	void stop() override;

	bool sendData(unsigned char* pData, size_t iLen) override;

	void DoCycleTask() override;

	bool ReadCom(char* buf, size_t& len);
	bool WriteCom(unsigned char* buf, int len);
	HANDLE m_hCom;

	//串口参数
	int m_baudRate;
	int m_byteSize;  
	string m_parity;//None,Odd,Even,Mark,Space
	string m_stopBits; //1 , 1,5 ,2 

	HANDLE m_hRecvThread;
	OVERLAPPED m_ovWaitEvent;
	OVERLAPPED m_ovRead;
	OVERLAPPED m_ovWrite;
};

#endif