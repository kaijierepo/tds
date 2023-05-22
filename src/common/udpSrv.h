#pragma once
#include <string>
using namespace std;

class IUdpServerCallBack {
public:
	virtual void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, std::string strIP, int port) = 0;
};


class udpServer
{
public:
	udpServer(void);
	~udpServer(void);

	size_t onRecvData(unsigned char* recvData, size_t recvDataLen, string strIP, int port);
	size_t SendData(unsigned char* pData, size_t iLen, string strIP, int port);

	bool run(IUdpServerCallBack* pcb,int localPort = 0, string localIP = "");
	void start();
	void stop();

	void startMultiCast(string multiCastAddr, int port);
	void multiCast(char* pData, int len);

	void addToMultiCast(string multiCastAddr, int port);

	int m_sock;
	int m_multiCastSendSock;
	string m_multiCastSendAddr;
	int m_multiCastSendPort;

	int m_multiCastRecvSock;
	string m_bindIP;
	int m_port;
	IUdpServerCallBack*  m_pCallback;
};

class UdpClt : public udpServer {
public:
	string m_remoteIP;
	int m_remotePort;

	size_t sendData(unsigned char* pData, size_t iLen);
};


