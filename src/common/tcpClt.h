#pragma once
#include <WinSock2.h>
#include <string>
#include <vector>
#include <mutex>

using namespace std;

//连接信息
class tcpClt;
struct tcpSessionClt
{
	SOCKET sock;
	std::string srvIP;//对端ip
	int srvPort;//对端端口
	void* pALSession;
	tcpClt* tcpClt;
	SOCKET bridgeSock;

	int iSendSucCount;
	int iSendFailCount;
	int iRecvCount;

	tcpSessionClt()
	{
		sock = 0;
		bridgeSock = 0;
		pALSession = NULL;
		tcpClt = NULL;
		iSendSucCount = 0;
		iSendFailCount = 0;
		iRecvCount = 0;
	}
};


class ITcpClientCallBack {
public:
	virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) = 0;
	virtual void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo) = 0;
};

class tcpClt
{
	friend bool operator==(const tcpClt&, const tcpClt&);
	friend bool operator!=(const tcpClt&, const tcpClt&);
public:
	tcpClt(void);
	~tcpClt(void);

	//指定服务器运行，断线自动重连
	bool run(ITcpClientCallBack* pUser, string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = -1);
	void stop();

	tcpSessionClt m_session;
	vector<char> heartbeat;

	void AsynConnect(ITcpClientCallBack* pUser,string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = -1);
	bool connect(ITcpClientCallBack* pUser,string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = -1);
	bool connect(ITcpClientCallBack* pUser, string host, string strLocalIp = "", int iLocalPort = -1);
	
	bool connect();
	bool ReConnect();
	bool DisConnect();
	inline bool IsConnect(){
		return m_bConn;
	};
	int SendData(char* pData, size_t iLen);
	static string GetLocalIP();

	SOCKET sockClient;
	string m_remoteIP;
	int m_remotePort;
	string m_strLocalIP;
	int m_iLocalPort;
	bool m_bConn;
	bool m_bRun;
	SYSTEMTIME lastConnTime;
	bool m_bIsConnectting;
	string m_strErrorInfo;
	ITcpClientCallBack* m_pCallBackUser;
	bool m_bRecvThreadRunning;
	bool m_bConnThreadRunning;
};
