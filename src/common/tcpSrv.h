#pragma once 
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include "mongoose.h"
#include "tds.h"

using namespace std;

//for compatialble with linux and win,sock in win is SOCKET ,int linux is int ,use int for both 
//64win SOCKET is 8 bytes,but is safe
/*
 * Even though sizeof(SOCKET) is 8, it's safe to cast it to int, because
 * the value constitutes an index in per-process table of limited size
 * and not a real pointer.
 */

namespace tcpServer {
	TIME str2time(const std::string& s);
	time_t time2unixstamp(TIME t);
	int calcTimePassSecond(string sTime);
	string getNowStr();
}

struct tcpSession
{
	int sock;
	string remoteIP;
	int remotePort;
	string localIP;
	int localPort;
	bool bIsTransmit;
	size_t iSendSucCount;
	size_t iSendFailCount;
	size_t iRecvCount;
	int iKeepAliveTimeout;
	void* pTcpServer;  
	void* pALSession; 
	int bridgeSock;  
	bool bEnableActivityCheck; 

	bool bEnable;

	string stLastActive;
	void* pData1;

	tcpSession()
	{
		stLastActive = tcpServer::getNowStr();
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
		localPort = 0;
		bridgeSock = 0;
		pData1 = nullptr;
		bEnable = true;
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

class ICallback_tcpSrv {
public:
	virtual void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) = 0;
	virtual void onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pTcpSess) = 0;
};

typedef void (*fp_statisSend)(int port, size_t len);



class tcpSrv  {
public:
	bool run(ICallback_tcpSrv* pUser, int port, string strLocalIP = "");
	void stop();

	void disconnect(string remoteAddr);

	bool SendData(char* pData, size_t iLen, string remoteIP);
	bool SendData(char* pData, size_t iLen);
	ICallback_tcpSrv* m_pCallBackUser;

	std::map<tcpSession*, tcpSession*> m_mapTcpSessions;
	std::mutex m_csClientVectorLock;

	bool m_bStarted;
	bool m_stop;
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

extern fp_statisSend g_fp_tcpSrv_statisSend;