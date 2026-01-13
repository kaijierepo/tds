#include "udpSrv.h"
#ifdef TDS
#include "logger.h"
#else
namespace UdpServer {
	void LOG(string s) {}
}
using namespace UdpServer;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET SOCKET_FD;
#define INVALID_SOCK_FD INVALID_SOCKET
#define CLOSE_SOCK(fd) closesocket(fd)
#define SOCK_ERROR_CODE GetLastError()
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
typedef int SOCKET_FD;
#define INVALID_SOCK_FD -1
#define CLOSE_SOCK(fd) close(fd)
#define SOCKET_ERROR -1
#define SOCK_ERROR_CODE errno
#define u_short unsigned short
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
	m_recvThreadRunning = false;
}


udpServer::~udpServer(void)
{
}

bool udpServer::run(ICallback_udpSrv* pcb, int port,string serverIP)
{
	m_port = port;
	m_pCallback = pcb;

	if (serverIP == "")
		serverIP = "0.0.0.0";
	m_bindIP = serverIP;

	return start();
}

bool udpServer::run_multicast(ICallback_udpSrv* pcb, int localPort, string multicastGroup, string localIP)
{
	m_port = localPort;
	m_pCallback = pcb;

	if (localIP == "")
		localIP = "0.0.0.0";
	m_bindIP = localIP;
	m_multicastRecvIP = multicastGroup;

	startMulticast();
	return true;
}

void udpServer::startMulticast()
{
	// 1. 创建UDP套接字（跨平台）
	m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_sock == INVALID_SOCK_FD)  // 跨平台无效套接字判断
	{
		// Linux用errno，Windows用WSAGetLastError()（已通过宏统一）
		printf("create udp sock error, errno:%d\n", SOCK_ERROR_CODE);
		return;
	}

	// 2. 设置套接字不被继承（跨平台）
#ifdef _WIN32
	SetHandleInformation((HANDLE)m_sock, HANDLE_FLAG_INHERIT, 0);
#else
	fcntl(m_sock, F_SETFD, fcntl(m_sock, F_GETFD) | FD_CLOEXEC);
#endif

	// 3. 加入组播组（核心兼容逻辑）
	struct ip_mreq mreq;
	memset(&mreq, 0, sizeof(mreq));

	// 转换组播地址（inet_pton跨平台，比inet_addr更安全）
	if (inet_pton(AF_INET, m_multicastRecvIP.c_str(), &(mreq.imr_multiaddr.s_addr)) <= 0) {
		printf("inet_pton error, errno:%d\n", SOCK_ERROR_CODE);
		CLOSE_SOCK(m_sock);
		m_sock = INVALID_SOCK_FD;
		return;
	}
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);

	// 跨平台判断setsockopt返回值 + 关闭套接字
	if (setsockopt(m_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		(const char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
		printf("setsockopt(IP_ADD_MEMBERSHIP) 失败，错误码:%d\n", SOCK_ERROR_CODE);
		CLOSE_SOCK(m_sock);  // 跨平台关闭套接字
		m_sock = INVALID_SOCK_FD;
		return;
	}

	// 4. 绑定端口和IP（跨平台）
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(m_port);
	if (m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(m_bindIP.c_str());
	}

	// Linux bind失败返回-1，Windows返回SOCKET_ERROR（已统一为-1）
	int nBind = ::bind(m_sock, (sockaddr*)&addr, sizeof(addr));
	if (nBind != 0)
	{
		char sz[200] = { 0 };
		snprintf(sz, sizeof(sz), "[error]UDP port can not bind,IP=%s,Port=%d, errno:%d",
			m_bindIP.c_str(), m_port, SOCK_ERROR_CODE);
		std::string str = sz;
		// LOG(str); // 保留你的日志逻辑
		CLOSE_SOCK(m_sock);
		m_sock = INVALID_SOCK_FD;
		return;
	}

	// 5. 获取实际绑定的端口号（跨平台）
	struct sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr);  // Linux需要socklen_t，Windows兼容int
	if (getsockname(m_sock, (struct sockaddr*)&localAddr, &addrLen) == 0) {
		m_port = ntohs(localAddr.sin_port);
	}

	// 6. 启动接收线程（注意：std::thread需要链接-pthread）
	std::thread t(udpRecvThread, this);
	t.detach();
}


bool udpServer::start()
{
	// 1. 创建UDP套接字（跨平台判断无效套接字）
	m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_sock == INVALID_SOCK_FD)
	{
		// 跨平台获取错误码（Windows=GetLastError，Linux=errno）
		int iErr = SOCK_ERROR_CODE;
		std::string s = "create udp sock error," + std::to_string(iErr);
		m_lastError = s; // 错误信息存入成员变量
		return false;
	}

	// 2. 设置套接字不被继承（跨平台）
#ifdef _WIN32
	SetHandleInformation((HANDLE)m_sock, HANDLE_FLAG_INHERIT, 0);
#else
	fcntl(m_sock, F_SETFD, fcntl(m_sock, F_GETFD) | FD_CLOEXEC);
#endif

	// 3. 绑定IP和端口（跨平台）
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)(m_port));
	if (m_bindIP == "0.0.0.0")
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		// inet_addr跨平台，但建议用inet_pton更安全（可选）
		addr.sin_addr.s_addr = inet_addr(m_bindIP.c_str());
	}

	// Linux bind失败返回-1，Windows返回SOCKET_ERROR（已统一为-1）
	int nBind = ::bind(m_sock, (sockaddr*)&addr, sizeof(addr));
	if (0 != nBind)
	{
		// 替换sprintf为snprintf，避免缓冲区溢出（跨平台更安全）
		char sz[200] = { 0 };
		snprintf(sz, sizeof(sz), "[error]UDP port can not bind,IP=%s,Port=%d, errno:%d",
			m_bindIP.c_str(), m_port, SOCK_ERROR_CODE);
		std::string str = sz;
		LOG(str); // 保留你的日志逻辑
		m_lastError = str;
		CLOSE_SOCK(m_sock); // 跨平台关闭套接字
		m_sock = INVALID_SOCK_FD;
		return false;
	}

	// 4. 获取实际绑定的端口号（修复Linux下addrLen类型问题）
	struct sockaddr_in localAddr;
	socklen_t addrLen = sizeof(localAddr); // Linux需要socklen_t，Windows兼容int
	if (getsockname(m_sock, (struct sockaddr*)&localAddr, &addrLen) == 0) {
		m_port = ntohs(localAddr.sin_port);
	}

	// 5. 启动接收线程（std::thread需Linux编译时加-pthread）
	std::thread t(udpRecvThread, this);
	t.detach();

	return true;
}

void udpServer::stop()
{
#ifdef _WIN32
	closesocket(m_sock);//关闭套接字
#else
	close(m_sock);
#endif
	m_sock = 0;

	while (1) {
#ifdef _WIN32
		Sleep(1);
#else
		sleep(1);
#endif
		if (!m_recvThreadRunning) {
			break;
		}
	}
}

void udpServer::startMultiCast(string multiCastAddr, int multiCastPort)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
	SetHandleInformation((HANDLE)sock, HANDLE_FLAG_INHERIT, 0);
#else
	fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);
#endif
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
		if (m_multicastRecvIP.length() > 0) {
			us.multicast = true;
		}
		m_pCallback->OnRecvUdpData(recvData, recvDataLen,us);
	}
	return 0;
}

size_t udpServer::SendData(unsigned char* pData, size_t iLen, string remoteIP, int remotePort) {
	if (m_sock) {
		sockaddr_in addrCli;
		memset(&addrCli,0, sizeof(addrCli));

		addrCli.sin_family      = AF_INET;
		addrCli.sin_addr.s_addr = inet_addr(remoteIP.c_str());
		addrCli.sin_port        = htons((u_short)remotePort);

		int nSent = sendto(m_sock, (char*)pData, (int)iLen, 0, (sockaddr*)&addrCli, sizeof(addrCli));
		return nSent;
	}

	return 0;
}

void udpRecvThread(void* lpParam)
{
	udpServer* pServ = (udpServer*)lpParam;
	pServ->m_recvThreadRunning = true;
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
			break;
		}
		else
		{
			szBuff[recvlen] = 0;
			pServ->onRecvData(szBuff, recvlen, inet_ntoa(addrCli.sin_addr), ntohs(addrCli.sin_port));
		}
	}
	pServ->m_recvThreadRunning = false;
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