#pragma once
#include "httplib.h"
#include "json.hpp"
#include "tcpSrv.h"

using json = nlohmann::json;

struct HMR_SESSION {
	string webHMRPath;
	int sock;
};

class HMRServer : public ITcpServerCallBack {
public:
	HMRServer();
	void watchFile_process(string dir_path);
	void run(const std::string dir_path);
	void websocketSend(string s, int sock);
	void statusChange_tcpSrv(tcpSession* pTcpSession, bool bIsConn) override;
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess) override;

	tcpSrv* m_httpHotUpdateSrv;
	json m_jConf;

	map<tcpSession*, HMR_SESSION> m_mapSessions;
	mutex m_mutexSessions;
};

extern HMRServer hmrServer;