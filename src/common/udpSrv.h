#pragma once
#include <WINSOCK.H>

class IUdpServerCallBack {
public:
	virtual void OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port) = 0;
};


class udpServer
{
public:
	udpServer(void);
	~udpServer(void);

	int OnRecvData(char* recvData, int recvDataLen, string strIP, int port);
	size_t SendData(char* pData, size_t iLen, string strIP, int port);

	bool run(IUdpServerCallBack* pcb,int localPort = 0, string localIP = "");
	void start();
	void stop();

	void startMultiCast(string multiCastAddr, int port);
	void multiCast(char* pData, int len);

	void addToMultiCast(string multiCastAddr, int port);

	SOCKET m_sock;
	SOCKET m_multiCastSendSock;
	string m_multiCastSendAddr;
	int m_multiCastSendPort;

	SOCKET m_multiCastRecvSock;
	string m_bindIP;
	int m_port;
	IUdpServerCallBack*  m_pCallback;
};


