#include "pch.h"
#include "tcpReverseProxy.h"
#include "logger.h"
#include "common.h"

tcpReverseProxy::tcpReverseProxy()
{

}

void tcpReverseProxy::run()
{
	bool bsl = proxyServer.run(this, proxyPort);
	if (bsl)
	{
		LOG("代理服务启动成功,主机地址: " + realHost + "代理端口:" + str::fromInt(proxyPort));
	}
	else
	{
		LOG("代理服务启动失败,主机地址: " + realHost + "代理端口:" + str::fromInt(proxyPort));
	}
}

void tcpReverseProxy::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		//创建real端session
		LOG("S" + str::fromInt( ((tcpSrv*)pCltInfo->pTcpServer)->m_iServerPort) + ": " + pCltInfo->remoteIP + " connected");
		shared_ptr<tcpClt> p(new tcpClt());
		if (p->connect(this, realHost))
		{
			pCltInfo->bridgeSock = p->sockClient;
			p->m_session.bridgeSock = pCltInfo->sock;
			sessionReal[p->sockClient]=p;
		}
		else
		{
			//连接real端失败，断开proxy端session
			closesocket(pCltInfo->sock);
		}
	}
	else
	{
		//断开real端session
		LOG("S" + str::fromInt(((tcpSrv*)pCltInfo->pTcpServer)->m_iServerPort) + ": " + pCltInfo->remoteIP + " disconnected");
		if (pCltInfo->bridgeSock)
		{
			closesocket(pCltInfo->bridgeSock);
			sessionReal.erase(pCltInfo->bridgeSock);
		}
	}
	
}

void tcpReverseProxy::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
	if (pCltInfo->bridgeSock)
		send(pCltInfo->bridgeSock, pData, iLen, 0);
}

void tcpReverseProxy::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{

	}
	else
	{
		//删除real端session
		sessionReal.erase(connInfo->sock);

		//删除proxy端session
		if (connInfo->bridgeSock)
		{
			closesocket(connInfo->bridgeSock);
		}
	}
}

void tcpReverseProxy::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
{
	if(connInfo->bridgeSock)
		send(connInfo->bridgeSock, pData, iLen, 0);
}
