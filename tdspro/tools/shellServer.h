#pragma once
#include "tcpSrv.h"
#include "stream2pkt.h"

class SHELL_CMD {
public:
	string method;
	vector<string> params;
};

class shellServer : public i_tdsPlugin,public ITcpServerCallBack {
public:
	shellServer();
	bool run() override;
	bool init() override;
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	void packGzhResp(string resp);
	void handleCmd(string cmd ,tcpSession* pCltInfo);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	SHELL_CMD parseCmd(string req);
	tcpSrv m_tcpSrv;
	stream2pkt m_s2p;
};


extern shellServer shellSrv;