#pragma once
#include "tcpSrv.h"

class tcpHub : public  ITcpServerCallBack {
public:
	tcpSrv sLeft;
	tcpSrv sRight;

	int portLeft;
	int portRight;

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
};