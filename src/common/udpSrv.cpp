#include "udpSrv.h"
#ifdef TDS
#include "logger.h"
#elif
namespace udpServer {
	void LOG(const char* pszFmt, ...) {}
}
using namespace udpServer;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif


namespace tds_udpSrv {
#ifdef _WIN32
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
#endif
}

void udpRecvThread(void* lpParam);


udpServer::udpServer(void)
{
	m_sock = 0;
	m_bindIP = "0.0.0.0";
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
	m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (-1 == m_sock)
	{
		//int iErr = GetLastError();
		//LOG("create udp sock error,%d", iErr);
		return;
	}
	else {

	}
	SetHandleInformation((HANDLE)m_sock, HANDLE_FLAG_INHERIT, 0);
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)(m_port));
	if (m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(m_bindIP.c_str());

	}
	int nBind = ::bind(m_sock, (sockaddr*)&addr, sizeof(addr));//成功返回0
	if (0 != nBind)
	{
		char sz[200] = { 0 };
		sprintf(sz,"[error]UPD port can not bind,IP=%s,Port=%d", m_bindIP.c_str(), m_port);
		string str = sz;
		LOG(str);
		return;
	}

	//获得已经绑定的端口号
	struct sockaddr_in localAddr;
	int addrLen = sizeof(localAddr);
	getsockname(m_sock, (struct sockaddr*)&localAddr, &addrLen);
	m_port = ntohs(localAddr.sin_port);


	thread t(udpRecvThread, this);
	t.detach();
}

void udpServer::stop()
{
#ifdef _WIN32
	closesocket(m_sock);//关闭套接字
#else
	close(m_sock);
#endif
	m_sock = 0;
}

void udpServer::startMultiCast(string multiCastAddr, int multiCastPort)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	SetHandleInformation((HANDLE)sock, HANDLE_FLAG_INHERIT, 0);

	struct in_addr localInterface;
	localInterface.s_addr = inet_addr(m_bindIP.c_str());
	
	//int iRet = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localInterface, sizeof(localInterface));
	//if (iRet != 0) {
	//	printf("setsockopt fail:%d", WSAGetLastError());
	//	return;
	//}

	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	// group cast port can not be the same with udp server port. don't know why
	addr.sin_port = htons((u_short)(m_port +10));
	if (m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(m_bindIP.c_str());

	}
	int nBind = ::bind(sock, (sockaddr*)&addr, sizeof(addr));
	if (0 != nBind)
	{
		char sz[200] = { 0 };
		sprintf(sz, "[error]UPD port can not bind,IP=%s,Port=%d", m_bindIP.c_str(), m_port);
		string str = sz;
		LOG(str);
		return;
	}


	int ttl = 255;
	int iRet = setsockopt(sock, IPPROTO_IP, 10, (char*)&ttl, sizeof(ttl));//IP_MULTICAST_TTL 10
	if (iRet != 0) {
		//printf("setsockopt fail:%d", WSAGetLastError());
		return;
	}

	m_multiCastSendAddr = multiCastAddr;
	m_multiCastSendPort = multiCastPort;
	m_multiCastSendSock = sock;
}

void udpServer::multiCast(char* pData, int len)
{
	sockaddr_in addr;
	addr.sin_addr.s_addr = inet_addr(m_multiCastSendAddr.c_str());
	addr.sin_family = AF_INET;
	addr.sin_port = htons(m_multiCastSendPort);

	int iSend = sendto(m_multiCastSendSock, pData, len, 0, (sockaddr*)&addr, sizeof(sockaddr));
}

void udpServer::addToMultiCast(string multiCastAddr, int port)
{


}

size_t udpServer::onRecvData(unsigned char* recvData, size_t recvDataLen, string strIP, int port)
{
	if (m_pCallback)
	{
		UDP_SESSION us;
		us.remoteIP = strIP;
		us.remotePort = port;
		us.localIP = m_bindIP;
		us.localPort = m_port;
		m_pCallback->OnRecvUdpData(recvData, recvDataLen,us);
	}
	return 0;
}

size_t udpServer::SendData(unsigned char* pData, size_t iLen, string remoteIP, int remotePort)
{
	if (m_sock)
	{
		sockaddr_in addrCli;
		memset(&addrCli,0, sizeof(addrCli));
		addrCli.sin_family = AF_INET;
		addrCli.sin_addr.s_addr = inet_addr(remoteIP.c_str());
		addrCli.sin_port = htons((u_short)remotePort);

		size_t nSent = sendto(m_sock, (char*)pData, (int)iLen, 0, (sockaddr*)&addrCli, sizeof(addrCli));

		return nSent;
	}
	return 0;
}

void udpRecvThread(void* lpParam)
{
	udpServer* pServ = (udpServer*)lpParam;
	unsigned char szBuff[10025];
	while (true)
	{
		sockaddr_in addrCli;
		memset(&addrCli,0, sizeof(addrCli));
#ifdef _WIN32
		int fromlen = sizeof(addrCli);
#else
		unsigned int fromlen = sizeof(addrCli);
#endif
		

		int recvlen = recvfrom(pServ->m_sock, (char*)szBuff, 1024, 0, (sockaddr*)&addrCli, &fromlen);
		if (recvlen < 0)
		{
			//int iErr = GetLastError();
			continue;
		}
		else
		{
			szBuff[recvlen] = 0;
			pServ->onRecvData(szBuff, recvlen, inet_ntoa(addrCli.sin_addr), ntohs(addrCli.sin_port));
		}
	}

	pServ->stop();
}

size_t UdpClt::sendData(unsigned char* pData, size_t iLen)
{
	return udpServer::SendData(pData, iLen, m_remoteIP, m_remotePort);
}

string UDP_SESSION::getRemoteIOAddr(){
	char sz[50] = {};
	sprintf(sz,"UDP-%s:%d", remoteIP.c_str(), remotePort);
	string s = sz;
	return s;
}