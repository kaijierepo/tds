#pragma once

/*
web server of tds
dispatch http request to rpcHandler
*/

#include "tdsSession.h"
#include "common/mongoose.h"

class WebServer {
public:
	bool handle_zlmhook(mg_http_message* hm, mg_connection* c);
	bool handle_stream_redirect(mg_http_message* hm, mg_connection* c);
	bool handle_rpc_rest_post(mg_http_message* hm, mg_connection* c);
	bool handle_rpc_rest(mg_http_message* hm, mg_connection* c);

	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	void sendToAllWs(string& s);
	static int sendToAllWebsock(string& s);
	static int sendToWs(unsigned char* p, size_t len, int sockPipe);
	bool m_isHttps;

	std::shared_ptr<TDS_SESSION> getWsSession(void* conn);
	void initWsSessionInfo(string& strData, std::shared_ptr<TDS_SESSION> tdsSession);
	void parseParamFromUrl(string& url, map<string, string>& mapParams);
	void parseParamFromQuery(string& query, map<string, string>& mapParams);
	json parseParamFromQuery(string& query);
	static bool handleAppLayerData_Bridge(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsSessions;
	std::mutex m_csWsSessions;
	int m_restApiID;
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
extern void sendToPktMonitorClient(char* p, size_t len);
extern void IOLogSend(unsigned char* p, size_t len, bool success, string remoteAddr);
extern void IOLogRecv(unsigned char* p, size_t len, string remoteAddr);

extern vector<std::shared_ptr<TDS_SESSION>> rpcPktMonitorClient;
extern shared_mutex csRpcPktMonitorClient;
extern void sendToRpcPktMonitorClient(char* p, size_t len);
extern void RpcLogSend(unsigned char* p, size_t len, bool success, string remoteAddr);
extern void RpcLogRecv(unsigned char* p, size_t len, string remoteAddr);

extern vector<std::shared_ptr<TDS_SESSION>> logTdsSessions;
extern void logToWebsock(string text);

extern bool runWebServers();