#pragma once

/*
web server of tds
dispatch http request to rpcHandler
*/

#define MG_TLS MG_TLS_BUILTIN  // Enable built-in TLS 1.3 stack

#include "tdsSession.h"
#include "common/mongoose.h"

class WebServer {
public:
	bool handle_zlmhook(mg_http_message* hm, mg_connection* c);
	bool handle_stream_redirect(mg_http_message* hm, mg_connection* c);

	bool handle_rpc_rest(mg_http_message* hm, mg_connection* c);

	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	void sendToAllWs(string& s);
	static int sendToAllWebsock(string& s);
	bool m_isHttps;
	int sendToWebSock(unsigned char* p, size_t len, unsigned long conn_id);
	std::shared_ptr<TDS_SESSION> getWsSession(void* conn);
	void initWsSessionInfo(string& strData, std::shared_ptr<TDS_SESSION> tdsSession);
	void parseParamFromUrl(string& url, map<string, string>& mapParams);
	void parseParamFromQuery(string& query, map<string, string>& mapParams);
	json parseParamFromQuery(string& query);
	static bool handleAppLayerData_Bridge(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void removeWsSession(mg_connection* c);
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsSessions; //这些session接受rpc通知
	std::mutex m_csWsSessions;
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsBridgeSessions;
	std::mutex m_csWsBridgeSessions;
	int m_restApiID;
	struct mg_mgr m_mgr;
	string m_certData;
	string m_keyData;
};

extern string rootDir;
extern string confDir;
extern string filesDir;

extern vector<WebServer*> g_WebServerList;

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