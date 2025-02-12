#pragma once
#include "tcpSrv.h"
#include "tcpClt.h"
#include "tdsConf.h"


class TDS_INI1 {
public:
	TDS_INI1() {};

	map<string, string> mapConf;
};

class tcpHub : public  ICallback_tcpSrv,public ICallback_tcpClt {
public:
	tcpSrv sLeft;
	tcpSrv sRight;
	tcpClt cLeft;
	tcpClt cRight;

	bool enable_pkt_log;


	int left_s_port;
	int left_c_port;
	string left_c_ip;
	string left_reg_pkt;

	int right_s_port;
	int right_c_port;
	string right_c_ip;
	string right_reg_pkt;

	bool logInText;

	
	string defaultConf();
	bool loadConf();

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) override;
	 void onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;

	 virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
	 virtual void onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) override;
};