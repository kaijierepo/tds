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
	string remoteIP;
	int remotePort;
	string localIP;
	int localPort;
	SOCK_CONN_TYPE type;
	stream2pkt recvBuff;

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


typedef void (*sockSessionRecvCallback)(char* pData, size_t iLen, std::shared_ptr<SOCK_SESSION> sockSess);


class tSockSrv : public ITcpServerCallBack,public ITcpClientCallBack,public IUdpServerCallBack
{
public:
	bool run();
	void stop();
	tSockSrv();
	virtual ~tSockSrv();
	bool sendToSockSession(std::shared_ptr<SOCK_SESSION> sockSession,unsigned char* pData, size_t len);
	void sendToAllSessions(unsigned char* pData, size_t len, bool specialNotify = false);
	void sendToAllSessions(string& s, bool specialNotify = false);
	map<int, std::shared_ptr<SOCK_SESSION>> m_sockSessions;
	mutex m_mutexSessions;
	sockSessionRecvCallback m_pCallback;

public:
	//tcp client 
	void OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) override;
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
	void onTcpCltEvent_error(tcpClt* pClt,string error) override;
	map<tcpClt*, tcpClt*> m_tcpClt_ParentTds; //client as child service
	string m_tcpClientRegPkt;
	map<tcpClt*, tcpClt*> m_tcpClt_streamPusher;	//data steam pusher
	mutex m_csTcpClt_streamPusher;
	string m_masterTdsIP;
	int m_masterTdsPort;

	//tcp server 
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) override;
	void OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;
	tcpSrv m_tcpSrv;

	//udp server
	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession) override;
	udpServer m_udpSrv;

public:
	void OnRecvData_TCP(char* pData, size_t iLen, std::shared_ptr<SOCK_SESSION> ss);
};


extern tSockSrv sockSrv;