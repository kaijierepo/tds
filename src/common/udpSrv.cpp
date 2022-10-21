#include "pch.h"
#include "udpSrv.h"
#include "logger.h"

DWORD WINAPI RecvThread(LPVOID lpParam);


udpServer::udpServer(void)
{
	m_sock = 0;
	m_bindIP = _T("0.0.0.0");
	m_port = 660;
	m_pCallback = NULL;
}


udpServer::~udpServer(void)
{
}

bool udpServer::run(IUdpServerCallBack* pcb, int port)
{
	m_port = port;
	m_pCallback = pcb;
	start();
	return true;
}

void udpServer::start()
{
	// initial socket library
	WORD wVerisonRequested;
	WSADATA wsaData;
	int err;
	wVerisonRequested = MAKEWORD(1, 1);
	err = WSAStartup(wVerisonRequested, &wsaData);

	DWORD dwThread = 0;
	HANDLE hThread = CreateThread(NULL, 0, RecvThread, (LPVOID)this, 0, &dwThread);
	if (hThread == NULL)
	{
		
	}
	else
	{
		CloseHandle(hThread);
	}
}

void udpServer::stop()
{
	closesocket(m_sock);//关闭套接字
	m_sock = 0;
}

int udpServer::OnRecvData(char* recvData, int recvDataLen, string strIP, int port)
{
	if (m_pCallback)
	{
		m_pCallback->OnRecvUdpData(recvData, recvDataLen, strIP, port);
	}
	return 0;
}

int udpServer::SendData(char* pData, int iLen, string strIP, int port)
{
	if (m_sock)
	{
		SOCKADDR_IN addrCli;
		ZeroMemory(&addrCli, sizeof(addrCli));
		addrCli.sin_family = AF_INET;
		addrCli.sin_addr.s_addr = inet_addr(strIP.c_str());
		addrCli.sin_port = htons((u_short)port);

		int nSent = sendto(m_sock, pData, iLen, 0, (sockaddr*)&addrCli, sizeof(addrCli));

		if (0 == nSent)
		{
		}
		else
		{
		}
	}
	return 0;
}

DWORD WINAPI RecvThread(LPVOID lpParam)
{
	udpServer* pServ = (udpServer*)lpParam;

	//创建socket套接字
	pServ->m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == pServ->m_sock)
	{
		int iErr = GetLastError();
		LOG("create udp sock error,%d", iErr);
		return 0;
	}
	else {

	}

	//绑定
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)(pServ->m_port));
	if (pServ->m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(pServ->m_bindIP.c_str());

	}
	int nBind = ::bind(pServ->m_sock, (sockaddr*)&addr, sizeof(addr));//成功返回0
	if (0 != nBind)
	{
		string strData = str::format("[error]UDP服务器端口被占用,IP=%s,Port=%d", pServ->m_bindIP.c_str(), pServ->m_port);
		LOG(strData);
		return 0;
	}

	//获得已经绑定的端口号
	int nLen = sizeof(addr);
	getsockname(pServ->m_sock, (sockaddr*)&addr, &nLen);

	//等待并接收数据
	char szBuff[10025];
	while (true)
	{
		SOCKADDR_IN addrCli;
		ZeroMemory(&addrCli, sizeof(addrCli));
		int fromlen = sizeof(addrCli);

		int recvlen = recvfrom(pServ->m_sock, szBuff, 1024, 0, (sockaddr*)&addrCli, &fromlen);
		if (recvlen < 0)
		{
			int iErr = GetLastError();
			continue;
		}
		else
		{
			szBuff[recvlen] = 0;
			pServ->OnRecvData(szBuff, recvlen, inet_ntoa(addrCli.sin_addr), ntohs(addrCli.sin_port));
		}
	}

	pServ->stop();
	return 0;
}
