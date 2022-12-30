#include "pch.h"
#include "udpSrv.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <WS2tcpip.h>


namespace tds_udpSrv {
	class WinSockInit {
	public:
		WinSockInit() {
			WSADATA wsaData;
			if (WSAStartup(0x0002, &wsaData) == 0) is_valid_ = true;
		}

		~WinSockInit() {
			if (is_valid_) WSACleanup();
		}

		bool is_valid_ = false;
	};
	static WinSockInit wsinit;
}

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

bool udpServer::run(IUdpServerCallBack* pcb, int port,string serverIP)
{
	m_port = port;
	m_pCallback = pcb;

	if (serverIP == "")
		serverIP = "0.0.0.0";
	m_bindIP = serverIP;

	start();
	return true;
}

void udpServer::start()
{
	//创建socket套接字
	m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == m_sock)
	{
		int iErr = GetLastError();
		LOG("create udp sock error,%d", iErr);
		return;
	}
	else {

	}

	//绑定
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)(m_port));
	if (m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(m_bindIP.c_str());

	}
	int nBind = ::bind(m_sock, (sockaddr*)&addr, sizeof(addr));//成功返回0
	if (0 != nBind)
	{
		string strData = str::format("[error]UDP服务器端口被占用,IP=%s,Port=%d", m_bindIP.c_str(), m_port);
		LOG(strData);
		return;
	}

	//获得已经绑定的端口号
	int nLen = sizeof(addr);
	getsockname(m_sock, (sockaddr*)&addr, &nLen);


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

void udpServer::startMultiCast(string multiCastAddr, int multiCastPort)
{
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
	//sock = m_sock;

	//绑定
	struct in_addr localInterface;
	localInterface.s_addr = inet_addr(m_bindIP.c_str());
	
	//int iRet = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface));
	//if (iRet != 0) {
	//	printf("setsockopt fail:%d", WSAGetLastError());
	//	return;
	//}


	//绑定
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	//组播端口和udp服务端口不能使用同一端口，待研究
	addr.sin_port = htons((u_short)(m_port +10));
	if (m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(m_bindIP.c_str());

	}
	int nBind = ::bind(sock, (sockaddr*)&addr, sizeof(addr));//成功返回0
	if (0 != nBind)
	{
		DWORD dwErr = GetLastError();
		string strData = str::format("[error]UDP服务器端口被占用,IP=%s,Port=%d,错误码:%d", m_bindIP.c_str(), m_port,dwErr);
		LOG(strData);
		return;
	}


	int ttl = 255;
	int iRet = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));
	if (iRet != 0) {
		printf("setsockopt fail:%d", WSAGetLastError());
		return;
	}

	m_multiCastSendAddr = multiCastAddr;
	m_multiCastSendPort = multiCastPort;
	m_multiCastSendSock = sock;
}

void udpServer::multiCast(char* pData, int len)
{
	sockaddr_in addr;
	addr.sin_addr.S_un.S_addr = inet_addr(m_multiCastSendAddr.c_str());
	addr.sin_family = AF_INET;
	addr.sin_port = htons(m_multiCastSendPort);

	int iSend = sendto(m_multiCastSendSock, pData, len, 0, (sockaddr*)&addr, sizeof(sockaddr));
}

void udpServer::addToMultiCast(string multiCastAddr, int port)
{


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
