#ifndef TDS_DATA_SERVER_WEBSRV_H
#define TDS_DATA_SERVER_WEBSRV_H


/*
web server of tds
dispatch http request to rpcHandler
*/
#include "tdsSession.h"
#include "mongoose.h"
#include "udpSrv.h"

struct SESSION_STATIS {
	string remoteIP;
	int remotePort;
	string lastMethod;
	size_t recv;
	size_t send;
	size_t reqCount;
	TIME lastRecvTime;

	SESSION_STATIS() {
		recv = 0;
		send = 0;
		reqCount = 0;
	}
};

class WebServer {
public:
	bool handle_zlmhook(mg_http_message* hm, mg_connection* c);
	bool handle_stream_redirect(mg_http_message* hm, mg_connection* c);

	bool handle_rpc_rest(mg_http_message* hm, mg_connection* c);

	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	int notifyWs(unsigned char* p, size_t len, unsigned long conn_id);
	void notifyAllWs(const string& method, const string& tag, const string& s);
	static int notifyAllSrvAllWs(const string& method, const string& tag, const string& s);
	int m_port;
	bool m_isHttps;
	std::shared_ptr<TDS_SESSION> getWsSession(void* conn);
	void initWsSessionInfo(string& strData, std::shared_ptr<TDS_SESSION> tdsSession);
	void parseParamFromUrl(string& url, map<string, string>& mapParams);
	void parseParamFromQuery(string& query, map<string, string>& mapParams);
	json parseParamFromQuery(string& query);
	static bool handleAppLayerData_Bridge(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void removeWsSession(mg_connection* c);
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsSessions; //sessions receives rpc notification
	std::mutex m_csWsSessions;
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsBridgeSessions;
	std::map<string, SESSION_STATIS*>  m_httpSessions;
	std::mutex m_csWsBridgeSessions;
	int m_restApiID;
	struct mg_mgr m_mgr;
	string m_certData;
	string m_keyData;
};

extern string rootDir;
extern string confDir;
extern string filesDir;
extern string dbDir;

extern vector<WebServer*> g_WebServerList;
WebServer* getWebServer(int port, bool isHttps);

extern bool g_enableWsNotify;
extern vector<std::shared_ptr<TDS_SESSION>> commpktSessions;
extern void sendToCommLog(string s);

extern vector<std::shared_ptr<TDS_SESSION>> ioPktMonitorClient;
extern shared_mutex csIoPktMonitorClient;
extern void sendToPktMonitorClient(char* p, size_t len);
extern void IOLogSend(unsigned char* p, size_t len, bool success, string remoteAddr, string localAddr,string ioAddr);
extern void IOLogRecv(unsigned char* p, size_t len, string remoteAddr, string localAddr,string ioAddr);
extern void IOLogRecv(unsigned char* p, size_t len, UDP_SESSION udpSession);
extern vector<std::shared_ptr<TDS_SESSION>> rpcPktMonitorClient;
extern shared_mutex csRpcPktMonitorClient;
extern void sendToRpcPktMonitorClient(char* p, size_t len);
extern void RpcLogSend(unsigned char* p, size_t len, bool success, string remoteAddr);
extern void RpcLogRecv(unsigned char* pHead, size_t headLen, unsigned char* pBody, size_t bodyLen, string remoteAddr);

extern vector<std::shared_ptr<TDS_SESSION>> logTdsSessions;
extern void logToWebsock(string text);

extern bool runWebServers();
extern std::map<string, string> apiBlackList; // apiIP des
extern std::map<string, string> ioBlackList; // ioIP des
extern bool loadApiBlackList();
extern bool saveApiBlackList();
extern bool loadIoBlackList();
extern bool saveIoBlackList();

extern bool addBlackList(string type, json paramVal);
extern bool delBlackList(string type, string ip);
extern bool setBlackList(string type, json listVal);
extern bool getBlackList(string type, json& listVal);

typedef bool (*fn_http_handler)(mg_http_message* hm, struct mg_connection* c);
extern std::map<string,fn_http_handler> g_mapHttpHandler;


#endif /* TDS_DATA_SERVER_WEBSRV_H */
