#pragma once
#include <string>
#include "udpSrv.h"
#include "tcpSrv.h"
#include "tcpClt.h"
#include "stream2pkt.h"

enum SOCK_CONN_TYPE {
	UDP_SOCK,
	TCP_SOCK
};

struct SOCK_SESSION {
	int sock;
	std::string remoteIP;
	int remotePort;
	std::string localIP;
	int localPort;
	SOCK_CONN_TYPE type;
	stream2pkt recvBuff;
	std::shared_ptr<void> appLayerSession;

	SOCK_SESSION(tcpSessionClt* p) {
		sock = p->sock;
		remoteIP = p->remoteIP;
		remotePort = p->remotePort;
		localIP = p->localIP;
		localPort = p->localPort;
		type = TCP_SOCK;
	}

	SOCK_SESSION(tcpSession* p) {
		sock = p->sock;
		remoteIP = p->remoteIP;
		remotePort = p->remotePort;
		localIP = p->localIP;
		localPort = p->localPort;
		type = TCP_SOCK;
	}
};

typedef void (*sockSessionStatusCallback)(bool conn,std::shared_ptr<SOCK_SESSION> sockSess);
typedef void (*sockSessionRecvCallback)(char* pData, size_t iLen, std::shared_ptr<SOCK_SESSION> sockSess);


struct SOCK_SRV_CONF {
	vector<std::string> masterTdsAddrs;
	std::string childTdsIP;
	int tcpKeepAliveSec;
	int tcpSrvPort;
	int udpSrvPort;
	std::string tcpClientRegPkt;
	std::string tcpServerRegPkt;

	SOCK_SRV_CONF() {
		tcpKeepAliveSec = 0;
		tcpSrvPort = 0;
		udpSrvPort = 0;
	}
};


class tSockSrv : public ICallback_tcpSrv,public ICallback_tcpClt,public ICallback_udpSrv
{
public:
	bool run(SOCK_SRV_CONF& conf);
	void addTdspUplinkClients(std::vector<std::string>& addrs,
		std::string childTdsIP);
	void stop();
	tSockSrv();
	virtual ~tSockSrv();
	bool sendToSockSession(std::shared_ptr<SOCK_SESSION> sockSession,unsigned char* pData, size_t len);
	void sendToAllSessions(unsigned char* pData, size_t len, bool specialNotify = false);
	void sendToAllSessions(std::string& s, bool specialNotify = false);
	map<int, std::shared_ptr<SOCK_SESSION>> m_sockSessions;
	mutex m_mutexSessions;
	sockSessionRecvCallback m_pCallback;
	sockSessionStatusCallback m_pStatusCallback;

	SOCK_SRV_CONF m_conf;
public:
	//tcp client 
	void onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) override;
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
	void onTcpCltEvent_error(tcpClt* pClt,std::string error) override;
	std::vector<tcpClt*> m_tcpClt_ParentTds; //client as child service
	map<tcpClt*, tcpClt*> m_tcpClt_streamPusher;	//data steam pusher
	mutex m_csTcpClt_streamPusher;
	std::string m_masterTdsIP;
	int m_masterTdsPort;

	//tcp server 
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) override;
	void onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;
	tcpSrv m_tcpSrv;

	//udp server
	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession) override;
	udpServer m_udpSrv;

public:
	void OnRecvData_TCP(char* pData, size_t iLen, std::shared_ptr<SOCK_SESSION> ss);
};


extern tSockSrv sockSrv;