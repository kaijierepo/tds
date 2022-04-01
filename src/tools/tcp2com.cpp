#include "pch.h"
#include "tcp2com.h"
#include "logger.h"
#include "common.hpp"
#include "json.hpp"
using json = nlohmann::json;

tcp2com::tcp2com()
{
	m_iDestPort = 0;
}

void  serialRecvCallback(void* user, char* pData, int iLen)
{
	tcp2com* pt2c = (tcp2com*)user;
	pt2c->tcpClt.SendData(pData, iLen);      
	string log = str::bytesToHexStr(pData, iLen);
	LOG(pt2c->serial.m_portNum + " --> " + pt2c->m_strDestIp + ":" + str::fromInt(pt2c->m_iDestPort) + "  " + log);
}




void tcp2com::run()
{
	if (m_iDestPort != 0 && m_strDestIp != "")
	{
		tcpClt.run(this, m_strDestIp, m_iDestPort);
		LOG("启动tcp客户端，服务器地址:%s:%d", m_strDestIp.c_str(), m_iDestPort);
	}

	
	if (serial.OpenCom())
	{
		serial.setRecvCallback(this, serialRecvCallback);
		serial.run();
		LOG("打开串口成功: " + serial.m_portNum);
		string comParam = "波特率:" + str::fromInt(serial.m_baudRate) + ",";
		comParam += "停止位:" + serial.m_stopBits + ",";
		comParam += "数据位:" + str::fromInt(serial.m_byteSize) + ",";
		comParam += "校验位:" + serial.m_parity ;
		LOG(comParam);
	}
	else
	{
		LOG("打开串口失败: " + serial.m_portNum);
		getchar();
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
		LOG("连接Tcp服务成功: " + m_strDestIp + ":" + str::fromInt(m_iDestPort));
		string s = str::parseEscapeChar(tds->conf->conf_tcp2com.registerPktStr);
		tcpClt.SendData((char*)s.c_str(), s.length());
		LOG("发送首发注册包,长度=%d,[%s]",s.length(), s.c_str());
	}
	else
	{
		
	}
}

void tcp2com::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
{
	serial.sendData(pData, iLen);
	string log = str::bytesToHexStr(pData, iLen);
	LOG(serial.m_portNum + " <-- " + m_strDestIp + ":" + str::fromInt(m_iDestPort) + "  " + log);
}
