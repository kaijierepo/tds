#include "tcp2com.h"
#include "logger.h"
#include "common.h"
#include "ioGW_localSerial.h"

tcp2com::tcp2com()
{
	m_iDestPort = 0;
}

void  serialRecvCallback(void* user, char* pData, int iLen)
{
	tcp2com* pt2c = (tcp2com*)user;
	pt2c->tcpClt.SendData(pData, iLen);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  
}




void tcp2com::run()
{
	if (m_iDestPort != 0 && m_strDestIp != "")
	{
		tcpClt.Run(this, m_strDestIp, m_iDestPort);
	}

	if (serial.OpenCom(m_strComPort))
	{
		LOG("打开串口成功: " + m_strComPort);
	}
	else
	{
		LOG("打开串口失败: " + m_strComPort);
		exit(0);
	}
}

void tcp2com::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		
	}
	else
	{
		
	}
	
}

void tcp2com::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
	if (pCltInfo->bridgeSock)
		send(pCltInfo->bridgeSock, pData, iLen, 0);
}

void tcp2com::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{
		LOG("连接Tcp服务成功: " + m_strDestIp + str::fromInt(m_iDestPort));
	}
	else
	{
		
	}
}

void tcp2com::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
{
	serial.sendData(pData, iLen);
}
