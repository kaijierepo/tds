#pragma once
#include "tcpSrv.h"

class tcpSwitch : public  ITcpServerCallBack {
public:
	tcpSwitch();

	tcpSrv sLeft;
	vector<tcpSession*> sessionLeft;
	vector<tcpSrv*> sRight;

	int portLeft;
	int portRight;

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
};