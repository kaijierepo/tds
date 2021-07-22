#include "tcpRoute.h"
#include "logger.h"
#include "common.h"

void tcpRoute::run()
{
	bool bsl = sLeft.run(this, portLeft);
	if (bsl)
	{
		LOG("Server at port " + str::fromInt(portLeft) + "   start sucess");
	}
	else
	{
		LOG("Server at port " + str::fromInt(portLeft) + "   start fail");
	}

	bool bsr = sRight.run(this, portRight);
	if (bsl)
	{
		LOG("Server at port " + str::fromInt(portRight) + "   start sucess");
	}
	else
	{
		LOG("Server at port " + str::fromInt(portRight) + "   start fail");
	}
}

void tcpRoute::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			LOG("S" + str::fromInt(portLeft) + ": " + pCltInfo->strIP + " connected");
		}
		else if (pCltInfo->pTcpServer == &sRight)
		{
			LOG("S" + str::fromInt(portRight) + ": " + pCltInfo->strIP + " connected");
		}
	}
	else
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			LOG("S" + str::fromInt(portLeft) + ": " + pCltInfo->strIP + " disconnected");
		}
		else if (pCltInfo->pTcpServer == &sRight)
		{
			LOG("S" + str::fromInt(portRight) + ": " + pCltInfo->strIP + " disconnected");
		}
	}
	
}

void tcpRoute::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
	if (pCltInfo->pTcpServer == &sLeft)
	{
		sRight.SendData(pData, iLen);
		char* p = new char[iLen + 1];
		memset(p, 0, iLen + 1);
		memcpy(p, pData, iLen);
		string s = p;
		LOG( "Server" + str::fromInt(portLeft) + "-->Server" + str::fromInt(portRight) + " " + str::fromInt(iLen) + "bytes   "  + s);
		delete p;
	}
	else if (pCltInfo->pTcpServer == &sRight)
	{
		sLeft.SendData(pData, iLen);
		char* p = new char[iLen + 1];
		memset(p, 0, iLen + 1);
		memcpy(p, pData, iLen);
		string s = p;
		LOG("Server" + str::fromInt(portLeft) + "<--Server" + str::fromInt(portRight) + " " + str::fromInt(iLen) + "bytes   " + s);
		delete p;
	}
}
