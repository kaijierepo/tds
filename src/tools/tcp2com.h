#pragma once
#include "tcpSrv.h"
#include "tcpClt.h"
#include <memory>
#include "ioGW_localSerial.h"

class tcp2com : public  ITcpServerCallBack ,public ITcpClientCallBack{
public:
	tcp2com();

	tcpSrv tcpServer;
	tcpClt tcpClt;
	ioGW_LocalSerial serial;

	int m_iSrvPort;
	int m_iDestPort;
	string m_strDestIp;

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);

	 void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	 void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
};