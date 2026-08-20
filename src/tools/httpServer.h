#ifndef TDS_TOOLS_HTTPSERVER_H
#define TDS_TOOLS_HTTPSERVER_H

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
#endif /* TDS_TOOLS_HTTPSERVER_H */
