#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <map>
#include <atomic>

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
	std::string stLastActive;

	std::string getRemoteAddr();
	std::string getLocalAddr();
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
	virtual void onTcpCltEvent_error(tcpClt* pClt,std::string error) {};
	virtual void onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo) = 0;
};

enum class enTcpCltConnectStatus : uint8_t
{
	FREE = 0,
	CONNECT_SOON = 1,
	CONNECTING = 2
};

class tcpClt
{
	friend bool operator==(const tcpClt&, const tcpClt&);
	friend bool operator!=(const tcpClt&, const tcpClt&);
public:
	tcpClt(void);
	~tcpClt(void);

	bool run(ICallback_tcpClt* pUser, std::string strServIP, int iServPort, std::string strLocalIp = "", int iLocalPort = 0);
	void stop();

	tcpSessionClt m_session;
	std::vector<char> heartbeat;

	void AsynConnect(ICallback_tcpClt* pUser,std::string strServIP, int iServPort, std::string strLocalIp = "", int iLocalPort = 0);
	bool connect(ICallback_tcpClt* pUser,std::string strServIP, int iServPort, std::string strLocalIp = "", int iLocalPort = 0);
	bool connect(ICallback_tcpClt* pUser, std::string host, std::string strLocalIp = "", int iLocalPort = 0);

	bool run(ICallback_tcpClt* pUser, std::string host, std::string strLocalIp = "", int iLocalPort = 0);
	
	bool connect();
	bool ReConnect();
	bool DisConnect();
	inline bool IsConnect(){
		return m_bConn;
	};

	int SendData( char* pData, size_t iLen);
	int SendData(unsigned char* pData, size_t iLen);

	int sockClient;
	std::string m_remoteIP;
	int m_remotePort;
	std::string m_strLocalIP;
	int m_iLocalPort;
	bool m_bConn;
	bool m_bRun;
	int m_keepAliveTimeout;
	std::string lastConnTime;
	std::atomic<enTcpCltConnectStatus> m_isConnectting = enTcpCltConnectStatus::FREE;
	std::string m_strErrorInfo;
	std::string m_lastErrTime;
	ICallback_tcpClt* m_pCallBackUser;
	bool m_bRecvThreadRunning;
	bool m_bConnThreadRunning;
};
