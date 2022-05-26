#pragma once

#include "tdsSession.h"


class WebServer {
public:
	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	void sendToWs(string& s);
	bool enableHttps;

	std::map<SOCKET, std::shared_ptr<TDS_SESSION>>  m_wsSessions;
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