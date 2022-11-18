#pragma once

#include "tdsSession.h"
#include "common/mongoose.h"

class WebServer {
public:
	void handle_stream(mg_http_message* hm, mg_connection* c);
	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	void sendToAllWs(string& s);
	static int sendToAllWebsock(string& s);
	static int sendToWs(char* p, size_t len, int sockPipe);
	bool m_isHttps;

	std::shared_ptr<TDS_SESSION> getWsSession(void* conn);
	void initWsSessionInfo(string& strData, std::shared_ptr<TDS_SESSION> tdsSession);
	void getUrlParams(string& url, map<string, string>& mapParams);
	static bool handleAppLayerData_Bridge(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsSessions;
	std::mutex m_csWsSessions;
	char a[10];
};

extern string rootDir;
extern string confDir;
extern string filesDir;


extern WebServer* webSrv;
extern WebServer* webSrvS;
extern WebServer* webSrv2;
extern WebServer* webSrvS2;

extern vector<std::shared_ptr<TDS_SESSION>> commpktSessions;
extern void sendToCommLog(string s);

extern vector<std::shared_ptr<TDS_SESSION>> ioPktMonitorClient;
extern shared_mutex csIoPktMonitorClient;
extern void sendToPktMonitorClient(char* p, int len);
extern void IOLogSend(char* p, int len, bool success, string remoteAddr);
extern void IOLogRecv(char* p, int len, string remoteAddr);

extern vector<std::shared_ptr<TDS_SESSION>> logTdsSessions;
extern void logToWebsock(string text);

extern bool runWebServers();