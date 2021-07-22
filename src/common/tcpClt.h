#pragma once
#include <WinSock2.h>
#include <string>
#include <vector>
#include <mutex>

using namespace std;

//连接信息
struct tcpSessionClt
{
	SOCKET sock;
	std::string peerIP;//对端ip
	int peerPort;//对端端口
	void* pAppLayerClient;

	tcpSessionClt()
	{
		sock = 0;
		pAppLayerClient = NULL;
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

	vector<char> heartbeat;

	void AsynConnect(ITcpClientCallBack* pUser,string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = -1);
	bool connect(ITcpClientCallBack* pUser,string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = -1);
	bool connect(ITcpClientCallBack* pUser, string host, string strLocalIp = "", int iLocalPort = -1);
	bool Run(ITcpClientCallBack* pUser, string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = -1);
	bool connect();
	bool ReConnect();
	bool DisConnect();
	inline bool IsConnect(){
		return m_bConn;
	};
	int SendData(char* pData, int iLen);
	static string GetLocalIP();

	SOCKET sockClient;
	string m_strServerIP;
	int m_iServerPort;
	string m_strLocalIP;
	int m_iLocalPort;
	bool m_bConn;
	SYSTEMTIME lastConnTime;
	bool m_bIsConnectting;
	string m_strErrorInfo;
	ITcpClientCallBack* m_pCallBackUser;
	std::mutex m_csLock;
};
