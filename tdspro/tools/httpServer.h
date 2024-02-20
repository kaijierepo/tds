#pragma once
#include "tcpSrv.h"
#include "tcpClt.h"
#include <memory>

class HttpServer{
public:
	HttpServer();

	void run();

	string defaultConf();
	int m_port;
	string m_ProcName;
};