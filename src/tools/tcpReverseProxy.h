#ifndef TDS_TOOLS_TCPREVERSEPROXY_H
#define TDS_TOOLS_TCPREVERSEPROXY_H

#include "tcpSrv.h"
#include "tcpClt.h"
#include <memory>

class tcpReverseProxy : public  ITcpServerCallBack ,public ITcpClientCallBack{
public:
	tcpReverseProxy();

	map<int, shared_ptr<tcpClt>> sessionReal;
	tcpSrv proxyServer;

	string realHost;
	int proxyPort;

	void run();

	 void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	 void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);

	 void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	 void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
};
#endif /* TDS_TOOLS_TCPREVERSEPROXY_H */
