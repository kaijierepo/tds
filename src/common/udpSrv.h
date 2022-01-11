#pragma once
#include <WINSOCK.H>

class IUdpServerCallBack {
public:
	virtual int OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port) = 0;
};


class udpServer
{
public:
	udpServer(void);
	~udpServer(void);

	int OnRecvData(char* recvData, int recvDataLen, string strIP, int port);
	int SendData(char* pData, int iLen, string strIP, int port);

	void start();
	void stop();

	SOCKET m_sock;
	string m_bindIP;
	int m_port;
	IUdpServerCallBack*  m_pCallback;
};


