#pragma once
#include <string>
#include <windows.h>
using namespace std;



class ICallback_serial {
public:
	virtual void statusChange_serial(bool bIsOpen) = 0;
	virtual void onRecvData_serial(unsigned char* pData, size_t iLen) = 0;
};


class Serial
{
public:
	Serial(void);
	~Serial(void);

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
	bool open(string confPort, int baudRate, string parity, int byteSize, string stopBits); // call openCom before run
	bool isOpen();
	bool closeCom();
	int parseStopBits(string s);
	int parseParity(string s);
	bool open();
	bool run(ICallback_serial* callback);
	void stop();

	bool read(char* buf, size_t& len);
	bool write(unsigned char* buf, int len);
	HANDLE m_hCom;

	ICallback_serial* m_pCallbackUser;

	//串口参数
	string m_comPort;
	int m_baudRate;
	int m_byteSize;  
	string m_parity;//None,Odd,Even,Mark,Space
	string m_stopBits; //1 , 1,5 ,2 

	HANDLE m_hRecvThread;
	OVERLAPPED m_ovWaitEvent;
	OVERLAPPED m_ovRead;
	OVERLAPPED m_ovWrite;

	string m_strErrorInfo;
};