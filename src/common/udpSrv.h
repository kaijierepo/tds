#pragma once
#include <string>
using namespace std;

struct UDP_SESSION {
	std::string remoteIP;
	int remotePort;
	std::string localIP;
	int localPort;
	bool multicast;
	std::string getRemoteIOAddr();

	UDP_SESSION() {
		multicast = false;
	}
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

	size_t onRecvData(unsigned char* recvData, size_t recvDataLen, std::string strIP, int port);
	size_t SendData(unsigned char* pData, size_t iLen, std::string strIP, int port);

	bool run(ICallback_udpSrv* pcb,int localPort = 0, std::string localIP = "");
	bool run_multicast(ICallback_udpSrv* pcb, int localPort = 0, std::string multicastGroup = "", std::string localIP = "");
	void startMulticast();
	bool start();
	void stop();

	void startMultiCast(std::string multiCastAddr, int port);
	void multiCast(char* pData, int len);

	void addToMultiCast(std::string multiCastAddr, int port);

	bool m_recvThreadRunning;

	int m_sock;
	int m_multiCastSendSock;
	std::string m_multiCastSendAddr;
	int m_multiCastSendPort;
	std::string m_multicastRecvIP;
	int m_multiCastRecvSock;
	std::string m_bindIP;
	int m_port;
	ICallback_udpSrv*  m_pCallback;
	std::string m_lastError;
};

class UdpClt : public udpServer {
public:
	std::string m_remoteIP;
	int m_remotePort;

	size_t sendData(unsigned char* pData, size_t iLen);
};


