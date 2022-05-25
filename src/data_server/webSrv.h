#pragma once

#include "tdsSession.h"


class WebServer {
public:
	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	//void sendToWs(string& s);
	void sendToWs1(string& s);
	bool enableHttps;

	//mg_mgr* pMgr;
	//std::vector<SOCKET> m_vecPipe;
	//void addPipeSock(SOCKET s);
	//void delPipeSock(SOCKET s);

	std::map<SOCKET,SOCKET>  m_wsSessions;
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