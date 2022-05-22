#pragma once
#include "common/mongoose.h"
#include "tdsSession.h"


class WebServer {
public:
	WebServer();
	~WebServer();
	void run(int port, bool https = false);
	void sendToWs(string& s);
	bool enableHttps;

	mg_mgr* pMgr;
	std::map<void*, std::shared_ptr<TDS_SESSION>>  m_wsSessions;
	std::mutex m_csWsSessions;
};

extern string rootDir;
extern string confDir;
extern string filesDir;
