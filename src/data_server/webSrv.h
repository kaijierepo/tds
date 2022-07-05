#pragma once

#include "tdsSession.h"


class WebServer {
public:
	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	void sendToAllWs(string& s);
	static int sendToAllWebsock(string& s);
	static int sendToWs(char* p,int len, int sockPipe);
	bool enableHttps;

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

extern vector<std::shared_ptr<TDS_SESSION>> sessionPktSessions;
extern shared_mutex csSessionPktSessions;
extern void sendToSessionPktSessions(char* p, int len);

extern vector<std::shared_ptr<TDS_SESSION>> logTdsSessions;
extern void logToWebsock(string text);

extern bool runWebServers();