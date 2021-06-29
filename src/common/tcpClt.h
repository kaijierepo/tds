#pragma once
#include <WinSock2.h>
#include <string>
#include <vector>
#include <mutex>

using namespace std;

#define TCPIOCP_CLIENT "TcpIOCP client"
#define UPDATE_CLIENT_ONE_DATA WM_USER + 1004
// #define ADD_TCPIOCP WM_USER + 1002
// #define ADD_CONNECT_TCPTOCP WM_USER + 1003

//连接信息
struct ConnInfo
{
	SOCKET sock;
	std::string peerIP;//对端ip
	int peerPort;//对端端口
	void* p1;

	ConnInfo()
	{
		sock = 0;
		p1 = NULL;
	}
};

typedef enum ConnDir {
	CONNDIR_SEND = 0,
	CONNDIR_RECV
}ConnDir;

struct PKT_DATA_WITH_CONNDIR {

	char* pData;
	int iLen;
	std::string serverIP;
	int serverPort;
	ConnDir dir;
	SYSTEMTIME st;
	int iSendResult;
	PKT_DATA_WITH_CONNDIR(char* p, int l)
	{
		pData = new char[l];
		memcpy(pData, p, l);
		iLen = l;
		iSendResult = 0;
	}

	~PKT_DATA_WITH_CONNDIR() {
		delete pData;
	}
};


class ITcpClientCallBack {
public:
	virtual void ConnStatusChange(ConnInfo* connInfo, bool bIsConn) = 0;
	virtual void OnRecvData_TCPClient(char* pData, int iLen, ConnInfo* connInfo) = 0;
};

class CTCPClient
{
	friend bool operator==(const CTCPClient&, const CTCPClient&);
	friend bool operator!=(const CTCPClient&, const CTCPClient&);
public:
	CTCPClient(void);
	~CTCPClient(void);

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
	int StaticConnData(PKT_DATA_WITH_CONNDIR* datawithdir);

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
