#pragma once
#include "tcpSrv.h"
#include "tcpClt.h"

class tcpHub : public  ITcpServerCallBack  {
public:
	tcpSrv sLeft;
	tcpSrv sRight;
	int sPortLeft;
	int sPortRight;

	tcpClt cLeft;
	tcpSrv cRight;
	string cIPLeft;
	string cIPRight;
	int cPortLeft;
	int cPortRight;



	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
};