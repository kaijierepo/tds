#include "pch.h"
#include "tcpHub.h"
#include "logger.h"
#include "common.hpp"

void tcpHub::run()
{
	bool bsl = sLeft.run(this, sPortLeft);
	if (bsl)
	{
		LOG("Server at port " + str::fromInt(sPortLeft) + "   start sucess");
	}
	else
	{
		LOG("Server at port " + str::fromInt(sPortLeft) + "   start fail");
	}

	bool bsr = sRight.run(this, sPortRight);
	if (bsl)
	{
		LOG("Server at port " + str::fromInt(sPortRight) + "   start sucess");
	}
	else
	{
		LOG("Server at port " + str::fromInt(sPortRight) + "   start fail");
	}
}

void tcpHub::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			LOG("S" + str::fromInt(sPortLeft) + ": " + pCltInfo->remoteIP + " connected");
		}
		else if (pCltInfo->pTcpServer == &sRight)
		{
			LOG("S" + str::fromInt(sPortRight) + ": " + pCltInfo->remoteIP + " connected");
		}
	}
	else
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			LOG("S" + str::fromInt(sPortLeft) + ": " + pCltInfo->remoteIP + " disconnected");
		}
		else if (pCltInfo->pTcpServer == &sRight)
		{
			LOG("S" + str::fromInt(sPortRight) + ": " + pCltInfo->remoteIP + " disconnected");
		}
	}
	
}

void tcpHub::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
	if (pCltInfo->pTcpServer == &sLeft)
	{
		sRight.SendData(pData, iLen);
		char* p = new char[iLen + 1];
		memset(p, 0, iLen + 1);
		memcpy(p, pData, iLen);
		string s = p;
		LOG( "Server" + str::fromInt(sPortLeft) + "-->Server" + str::fromInt(sPortRight) + " " + str::fromInt(iLen) + "bytes\r\n"  + s);
		delete p;
	}
	else if (pCltInfo->pTcpServer == &sRight)
	{
		sLeft.SendData(pData, iLen);
		char* p = new char[iLen + 1];
		memset(p, 0, iLen + 1);
		memcpy(p, pData, iLen);
		string s = p;
		LOG("Server" + str::fromInt(sPortLeft) + "<--Server" + str::fromInt(sPortRight) + " " + str::fromInt(iLen) + "bytes\r\n" + s);
		delete p;
	}
}
