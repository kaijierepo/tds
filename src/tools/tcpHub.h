#pragma once
#include "tcpSrv.h"
#include "tcpClt.h"
#include "tdsConf.h"


class TDS_INI1 {
public:
	TDS_INI1() {};

	map<string, string> mapConf;
};

class tcpHub : public  ITcpServerCallBack,public ITcpClientCallBack {
public:
	tcpSrv sLeft;
	tcpSrv sRight;
	tcpClt cLeft;
	tcpClt cRight;


	int left_s_port;
	int left_c_port;
	string left_c_ip;


	int right_s_port;
	int right_c_port;
	string right_c_ip;

	bool logInText;

	
	string defaultConf();
	bool loadConf();

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);

	 virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	 virtual void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
};