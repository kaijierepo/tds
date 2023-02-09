#pragma once 
#include <map>
#include <winsock2.h>
#include <vector>
#include <mutex>
#include <memory>
#include "common.h"

using namespace std;

struct tcpSession
{
	unsigned int sock;
	SOCKADDR_IN clientAddr;
	string remoteIP;
	int remotePort;
	bool bIsTransmit;
	size_t iSendSucCount;
	size_t iSendFailCount;
	size_t iRecvCount;
	int iKeepAliveTimeout;
	void* pTcpServer;  
	void* pALSession; 
	SOCKET bridgeSock;
	bool bEnableActivityCheck; //是否进行活动检测

	TIME stLastActive;
	void* pData1;

	tcpSession()
	{
		timeopt::now(&stLastActive);
		sock = 0;
		pALSession = nullptr;
		pTcpServer = nullptr;
		bIsTransmit = false;
		iSendSucCount = 0;
		iSendFailCount = 0;
		iRecvCount = 0;
		iKeepAliveTimeout = 0;
		bEnableActivityCheck = true;
		remotePort = 0;
		bridgeSock = 0;
		pData1 = nullptr;
	}

	tcpSession* GenerateClienInfo() {
		tcpSession* ptr;
		ptr = new tcpSession();
		if (ptr) {
			*ptr = *this;
			ptr->pTcpServer = NULL;
		}
		return ptr;
	}

	bool send(char* pData, size_t iLen);
};

class ITcpServerCallBack {
public:
	virtual void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) = 0;
	virtual void OnRecvData_TCPServer(char* pData, size_t iLen, tcpSession* pTcpSess) = 0;
};


class tcpSrv  {
public:
	bool run(ITcpServerCallBack* pUser, int port, string strLocalIP = "");
	void stop();

	void disconnect(string remoteAddr);

	bool SendData(char* pData, size_t iLen, string remoteIP);
	bool SendData(char* pData, size_t iLen);
	ITcpServerCallBack* m_pCallBackUser;

	std::map<tcpSession*, tcpSession*> m_mapTcpSessions;
	std::mutex m_csClientVectorLock;

	bool m_bStarted;
	bool m_bReuseAddr;

	string m_strName;

	void Log(char* sz);
	void (*pLog)(char*);

public:
	tcpSrv();
	~tcpSrv();
	
	string m_strServerIP;
	int m_iServerPort;
	int keepAliveTimeout;
};