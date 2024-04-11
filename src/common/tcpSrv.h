#pragma once 
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include "common.h"
#include "mongoose.h"

using namespace std;

//为了方便linux和win兼容，sock句柄win下SOCKET类型，linux下为int类型，统一使用 int 来存放 
//64位win下SOCKET是8字节，但是是安全的
/*
 * Even though sizeof(SOCKET) is 8, it's safe to cast it to int, because
 * the value constitutes an index in per-process table of limited size
 * and not a real pointer.
 */


struct tcpSession
{
	int sock;
	string remoteIP;
	int remotePort;
	bool bIsTransmit;
	size_t iSendSucCount;
	size_t iSendFailCount;
	size_t iRecvCount;
	int iKeepAliveTimeout;
	void* pTcpServer;  
	void* pALSession; 
	int bridgeSock;  
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
	virtual void OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pTcpSess) = 0;
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

	struct mg_mgr mgr;

	void Log(char* sz);
	void (*pLog)(char*);

public:
	tcpSrv();
	~tcpSrv();
	
	string m_strServerIP;
	int m_iServerPort;
	int keepAliveTimeout;
};