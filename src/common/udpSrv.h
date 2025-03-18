#pragma once
#include <string>
using namespace std;

struct UDP_SESSION {
	string remoteIP;
	int remotePort;
	string localIP;
	int localPort;

	string getRemoteIOAddr();
};

class ICallback_udpSrv {
public:
	virtual void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION  udpSession) = 0;
};


class udpServer
{
public:
	udpServer(void);
	~udpServer(void);

	size_t onRecvData(unsigned char* recvData, size_t recvDataLen, string strIP, int port);
	size_t SendData(unsigned char* pData, size_t iLen, string strIP, int port);

	bool run(ICallback_udpSrv* pcb,int localPort = 0, string localIP = "");
	void start();
	void stop();

	void startMultiCast(string multiCastAddr, int port);
	void multiCast(char* pData, int len);

	void addToMultiCast(string multiCastAddr, int port);

	bool m_recvThreadRunning;

	int m_sock;
	int m_multiCastSendSock;
	string m_multiCastSendAddr;
	int m_multiCastSendPort;

	int m_multiCastRecvSock;
	string m_bindIP;
	int m_port;
	ICallback_udpSrv*  m_pCallback;
};

class UdpClt : public udpServer {
public:
	string m_remoteIP;
	int m_remotePort;

	size_t sendData(unsigned char* pData, size_t iLen);
};


