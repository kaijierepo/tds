#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <map>

using namespace std;

class tcpClt;
class TDS_SESSION;
struct tcpSessionClt
{
	int sock;
	std::string remoteIP;
	int remotePort;
	std::string localIP;
	int localPort;
	std::shared_ptr<TDS_SESSION> pALSession;
	tcpClt* pTcpClt;
	int bridgeSock;

	size_t iSendSucCount;
	size_t iSendFailCount;
	size_t iRecvCount;
	bool bEnable;
	string stLastActive;

	string getRemoteAddr();
	string getLocalAddr();
	tcpSessionClt()
	{
		sock = 0;
		bridgeSock = 0;
		pALSession = nullptr;
		pTcpClt = nullptr;
		iSendSucCount = 0;
		iSendFailCount = 0;
		iRecvCount = 0;
		remotePort = 0;
		bEnable = true;
	}
};


class ICallback_tcpClt {
public:
	virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) = 0;
	virtual void onTcpCltEvent_error(tcpClt* pClt,string error) {};
	virtual void onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo) = 0;
};

class tcpClt
{
	friend bool operator==(const tcpClt&, const tcpClt&);
	friend bool operator!=(const tcpClt&, const tcpClt&);
public:
	tcpClt(void);
	~tcpClt(void);

	bool run(ICallback_tcpClt* pUser, string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = 0);
	void stop();

	tcpSessionClt m_session;
	vector<char> heartbeat;

	void AsynConnect(ICallback_tcpClt* pUser,string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = 0);
	bool connect(ICallback_tcpClt* pUser,string strServIP, int iServPort, string strLocalIp = "", int iLocalPort = 0);
	bool connect(ICallback_tcpClt* pUser, string host, string strLocalIp = "", int iLocalPort = 0);

	bool run(ICallback_tcpClt* pUser, string host, string strLocalIp = "", int iLocalPort = 0);
	
	bool connect();
	bool ReConnect();
	bool DisConnect();
	inline bool IsConnect(){
		return m_bConn;
	};

	int SendData( char* pData, size_t iLen);
	int SendData(unsigned char* pData, size_t iLen);

	int sockClient;
	string m_remoteIP;
	int m_remotePort;
	string m_strLocalIP;
	int m_iLocalPort;
	bool m_bConn;
	bool m_bRun;
	int m_keepAliveTimeout;
	string lastConnTime;
	bool m_bIsConnectting;
	string m_strErrorInfo;
	ICallback_tcpClt* m_pCallBackUser;
	bool m_bRecvThreadRunning;
	bool m_bConnThreadRunning;
};
