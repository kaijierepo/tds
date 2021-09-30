#include "pch.h"
#include "tcpSwitch.h"
#include "logger.h"
#include "common.h"

tcpSwitch::tcpSwitch()
{

}

void tcpSwitch::run()
{
	bool bsl = sLeft.run(this, portLeft);
	if (bsl)
	{
		LOG("Server Left at port " + str::fromInt(portLeft) + "   start sucess");
	}
	else
	{
		LOG("Server Left at port " + str::fromInt(portLeft) + "   start fail");
	}


	for (int i = 0; i < 4; i++)
	{
		tcpSrv* p = new tcpSrv();
		int port = portRight + i;
		bool bsr = p->run(this, port);
		if (bsl)
		{
			LOG("Server Right at port " + str::fromInt(port) + "   start sucess");
		}
		else
		{
			LOG("Server Right at port " + str::fromInt(port) + "   start fail");
		}
		sRight.push_back(p);
	}
	
}

void tcpSwitch::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		if (pCltInfo->pTcpServer == &sLeft)
		{
			sessionLeft.push_back(pCltInfo);
		}
		LOG("S" + str::fromInt( ((tcpSrv*)pCltInfo->pTcpServer)->m_iServerPort) + ": " + pCltInfo->strIP + " connected");
	}
	else
	{
		LOG("S" + str::fromInt(((tcpSrv*)pCltInfo->pTcpServer)->m_iServerPort) + ": " + pCltInfo->strIP + " disconnected");
	}
	
}

void tcpSwitch::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
	if (pCltInfo->pTcpServer == &sLeft)
	{
		int idx = -1;
		for (int i = 0; i < sessionLeft.size(); i++)
		{
			tcpSession* p = sessionLeft[i];
			if (p == pCltInfo)
			{
				idx = i;
				break;
			}
		}

		if (idx == -1)
		{
			return;
		}

		if (idx <= 3)
		{
			tcpSrv* pSrv = sRight.at(idx);
			pSrv->SendData(pData, iLen);
			char* p = new char[iLen + 1];
			memset(p, 0, iLen + 1);
			memcpy(p, pData, iLen);
			string s = p;
			LOG("Server" + str::fromInt(portLeft) + "-->Server" + str::fromInt(pSrv->m_iServerPort) + " " + str::fromInt(iLen) + "bytes   " + s);
			delete p;
		}
	}
	else
	{
		int fromPort = ((tcpSrv*)pCltInfo->pTcpServer)->m_iServerPort;
		int idx = fromPort - portRight;

		if (idx < sessionLeft.size())
		{
			tcpSession* pS = sessionLeft.at(idx);
			sLeft.SendData(pData, iLen,pS);
			char* p = new char[iLen + 1];
			memset(p, 0, iLen + 1);
			memcpy(p, pData, iLen);
			string s = p;
			LOG("Server" + str::fromInt(portLeft) + "<--Server" + str::fromInt(fromPort) + " " + str::fromInt(iLen) + "bytes   " + s);
			delete p;
		}
	}
}
