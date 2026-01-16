#include "tcpClt.h"
#include "tds.h"
#include <thread>
#include <fcntl.h>

#pragma warning(disable:4996)
std::vector<tcpClt*> m_vecTCPIOCPClient;

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

#include "logger.h"

namespace tcpClient {
	TIME str2time(const std::string& s) {
		TIME t;
		//2022-02-22 11:11:11.123   23bytes
		if (s.length() == 23) {
			sscanf(s.c_str(), "%hu-%hu-%hu %hu:%hu:%hu.%hu",
				&t.wYear,
				&t.wMonth,
				&t.wDay,
				&t.wHour,
				&t.wMinute,
				&t.wSecond,
				&t.wMilliseconds);
		}
		//2022-02-22 11:11:11   19bytes
		else if (s.length() == 19) {
			t.wMilliseconds = 0;
			sscanf(s.c_str(), "%hu-%hu-%hu %hu:%hu:%hu",
				&t.wYear,
				&t.wMonth,
				&t.wDay,
				&t.wHour,
				&t.wMinute,
				&t.wSecond);
		}
		return t;
	}

	time_t time2unixstamp(TIME t)
	{
		tm temptm = { t.wSecond, t.wMinute, t.wHour,
			t.wDay, t.wMonth - 1, t.wYear - 1900, t.wDayOfWeek, 0, 0 };
		time_t iReturn = mktime(&temptm);
		return iReturn;
	}

	int calcTimePassSecond(string sTime) {
		time_t now = time(nullptr);
		TIME tlast = str2time(sTime);
		time_t last = time2unixstamp(tlast);
		return now - last;
	}

	string getNowStr() {
		TIME t;
		t.setNow();
		char buff[50] = { 0 };
		sprintf(buff, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		return buff;
	}

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

using namespace tcpClient;

string tcpSessionClt::getRemoteAddr() {
	char buffer[50] = {0}; 
	sprintf(buffer, "%s:%d", remoteIP.c_str(), remotePort);
	string s = buffer;
	return s;
}

string tcpSessionClt::getLocalAddr() {
	char buffer[50] = { 0 };
	sprintf(buffer, "%s:%d", localIP.c_str(), localPort);
	string s = buffer;
	return s;
}

void TcpClientRecvThread(void* lpParam) {
	tcpClt *pTcpClt=(tcpClt*)lpParam;
	pTcpClt->m_bRecvThreadRunning = true;

	int sock = pTcpClt->sockClient;

	pTcpClt->m_session.remoteIP = pTcpClt->m_remoteIP;
	pTcpClt->m_session.remotePort = pTcpClt->m_remotePort;
	pTcpClt->m_session.sock = sock;

	if (pTcpClt->m_pCallBackUser) {
		pTcpClt->m_pCallBackUser->statusChange_tcpClt(&pTcpClt->m_session, true);
	}

	vector<unsigned char> recvBuff;
	int iRecvBuffLen = 0;
	int ret;

	while(1){
		//buffer full , dynamicly increase 10k
		if(recvBuff.size() == iRecvBuffLen) {
			recvBuff.resize(recvBuff.size() + 100000);
		}

		ret = recv(sock, (char*)recvBuff.data() + iRecvBuffLen, recvBuff.size() - iRecvBuffLen, 0);
		if(ret <= 0) {
#ifdef _WIN32
			closesocket(sock);
			if (ret == 0) {
				pTcpClt->m_strErrorInfo = "[tcpClt]recv error,ret=0,connection closed by peer";
			}
			else {
				pTcpClt->m_strErrorInfo = "[tcpClt]recv error,err code=" + std::to_string(WSAGetLastError());
			}
			pTcpClt->m_strErrorInfo += " " + pTcpClt->m_remoteIP + ":" + std::to_string(pTcpClt->m_remotePort);
			printf(pTcpClt->m_strErrorInfo.c_str());
			pTcpClt->m_lastErrTime = getNowStr();
#else
			close(sock);
#endif

			if (pTcpClt->m_pCallBackUser) {
				pTcpClt->m_pCallBackUser->statusChange_tcpClt(&pTcpClt->m_session, false);
			}

			pTcpClt->sockClient = 0;
			break;
		}

		pTcpClt->m_session.iRecvCount += ret;
		iRecvBuffLen += ret;

#ifdef _WIN32
		//keep recv   prevent callback to applayer too many times.
		unsigned long bytesToRecv = 0;
		int iRet = ioctlsocket(sock, FIONREAD, &bytesToRecv);
		if (iRet == 0) {
			if (bytesToRecv > 0) {
				continue;
			}
		}
		else {
		}
#endif

		pTcpClt->m_session.stLastActive = getNowStr();

		if (pTcpClt->m_pCallBackUser) {
			pTcpClt->m_pCallBackUser->onRecvData_tcpClt(recvBuff.data(), iRecvBuffLen, &pTcpClt->m_session);
		}
	
		iRecvBuffLen = 0;
	}

	pTcpClt->sockClient = 0;
	pTcpClt->m_bConn = false;
	pTcpClt->m_bRecvThreadRunning = false;
}

void AsynConnectThread(void* lpParam) {
	tcpClt* p = (tcpClt*)lpParam;

	p->m_isConnectting = enTcpCltConnectStatus::CONNECTING;
	p->connect();
	p->m_isConnectting = enTcpCltConnectStatus::FREE;
}


map<tcpClt*, tcpClt*> mapAllTcpClt;
mutex csAllTcpClt;
bool connectThreadRunning = false;


void ConnectThread(void* lpParam) {
	while (1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		for (map<tcpClt*, tcpClt*>::iterator  i = mapAllTcpClt.begin(); i != mapAllTcpClt.end(); i++) {
			tcpClt* p = i->first;
			if (!p->m_bRun) {
				continue;
			}

			if (p->IsConnect()) {
				if (p->m_keepAliveTimeout > 0) {
					if (tcpClient::calcTimePassSecond(p->m_session.stLastActive) > p->m_keepAliveTimeout) {
						p->DisConnect();
						string s = getNowStr() + " disconnect inactive connection " + p->m_remoteIP + to_string(p->m_remotePort);
						printf(s.c_str());
					}
				}

				continue;
			}
				
			if (p->m_isConnectting != enTcpCltConnectStatus::FREE) {
				continue;
			}

			if (tcpClient::calcTimePassSecond(p->lastConnTime) > 3) {
				p->lastConnTime = getNowStr();
				p->m_isConnectting = enTcpCltConnectStatus::CONNECT_SOON;

				thread t(AsynConnectThread, p);
				t.detach();
			}
		}
	}
}

tcpClt::tcpClt(void) {
	sockClient = 0;
	m_iLocalPort = 0;
	m_session.pTcpClt = this;
	m_remoteIP = "127.0.0.1";
	m_remotePort = 0;
	m_bConn = false;
	m_bRun = false;
	lastConnTime = getNowStr();
	m_vecTCPIOCPClient.push_back(this);
	m_bRecvThreadRunning = false;
	m_bConnThreadRunning = false;

	csAllTcpClt.lock();

	mapAllTcpClt[this] = this;

	if (!connectThreadRunning){
		connectThreadRunning = true;

		thread t(ConnectThread, this);
		t.detach();
	}

	csAllTcpClt.unlock();

	m_keepAliveTimeout = 0;
}

tcpClt::~tcpClt(void)
{
	stop();
	csAllTcpClt.lock();
	mapAllTcpClt.erase(this);
	csAllTcpClt.unlock();
}

bool tcpClt::connect(ICallback_tcpClt* pUser, string strServIP,int iServPort,string strLocalIp,int iLocalPort )
{
	DisConnect();
	m_pCallBackUser = pUser;
	m_remoteIP = strServIP;
	m_remotePort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;

	return connect();
}

bool tcpClt::connect(ICallback_tcpClt* pUser, string host, string strLocalIp, int iLocalPort)
{
	DisConnect();
	m_pCallBackUser = pUser;
	size_t pos = host.find(":");
	string ip = host.substr(0, pos);
	string strPort = host.substr(pos + 1, host.length() - pos - 1);
	m_remoteIP = ip;
	m_remotePort = atoi(strPort.c_str());
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	return connect();
}

bool tcpClt::run(ICallback_tcpClt* pUser, string host, string strLocalIp, int iLocalPort)
{
	m_pCallBackUser = pUser;
	size_t pos = host.find(":");
	string ip = host.substr(0, pos);
	string strPort = host.substr(pos + 1, host.length() - pos - 1);
	m_remoteIP = ip;
	m_remotePort = atoi(strPort.c_str());
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	m_session.localIP = strLocalIp;
	m_session.localPort = iLocalPort;
	m_bRun = true;
	return true;
}

bool tcpClt::run(ICallback_tcpClt* pUser, string strServIP, int iServPort, string strLocalIp, int iLocalPort) {
	m_pCallBackUser = pUser;
	m_remoteIP = strServIP;
	m_remotePort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;

	m_bRun = true;
	return true;
}

void tcpClt::stop()
{
	m_bRun = false; 
	DisConnect();  
	while (1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (!m_bConnThreadRunning && !m_bRecvThreadRunning)
			break;
	}
}

void tcpClt::AsynConnect(ICallback_tcpClt* pUser,string strServIP, int iServPort, string strLocalIp /*= ""*/, int iLocalPort /*= -1*/)
{
	if(m_isConnectting != enTcpCltConnectStatus::FREE)
		return;
	m_isConnectting = enTcpCltConnectStatus::CONNECT_SOON;
	if(m_bConn)
		DisConnect();

	m_pCallBackUser = pUser;
	m_remoteIP = strServIP;
	m_remotePort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	thread t(AsynConnectThread,this);
	t.detach();
}

bool tcpClt::connect() {
	if (sockClient != 0) {
		return true;
	}

	int nConnect;
	struct hostent* hptr;
	bool ret = false;

	hptr = gethostbyname(m_remoteIP.c_str());
	if (hptr == NULL || hptr->h_addr == NULL) {
		m_strErrorInfo = "can not resolve addr " + m_remoteIP;

		if (m_pCallBackUser) {
			m_pCallBackUser->onTcpCltEvent_error(this, m_strErrorInfo);
		}

		goto CONN_END;
	}

	sockClient = socket(AF_INET,SOCK_STREAM,0);

#ifdef _WIN32
	SetHandleInformation((HANDLE)sockClient, HANDLE_FLAG_INHERIT, 0);
#else
	fcntl(sockClient, F_SETFD, fcntl(sockClient, F_GETFD) | FD_CLOEXEC);
#endif

	if(m_strLocalIP.length() > 0 && m_iLocalPort != 0) {
		sockaddr_in sAddTemp;
		sAddTemp.sin_family = AF_INET;
		sAddTemp.sin_addr.s_addr = inet_addr(m_strLocalIP.c_str());
		sAddTemp.sin_port = htons(m_iLocalPort);

		if(-1 == ::bind(sockClient, (sockaddr*)&sAddTemp,sizeof(sockaddr))) {
			m_strErrorInfo = "bind ip fail";

			if (m_pCallBackUser) {
				m_pCallBackUser->onTcpCltEvent_error(this, m_strErrorInfo);
			}

			goto CONN_END;
		}
	}

	sockaddr_in addrSrv;
	memcpy(&addrSrv.sin_addr.s_addr, hptr->h_addr_list[0], hptr->h_length);
	addrSrv.sin_family=AF_INET;
	addrSrv.sin_port=htons(m_remotePort);
	
	nConnect = ::connect(sockClient,(sockaddr*)&addrSrv,sizeof(sockaddr));
	if(nConnect == -1) {
		if (m_pCallBackUser) {
			m_pCallBackUser->onTcpCltEvent_error(this, m_strErrorInfo);
		}
	}
	else {
		//set m_bConn to true before TcpClientRecvThread created,when TcpClientRecvThread callback statucChange,will read this variable
		m_bConn = true;
		ret = true;

		lastConnTime = getNowStr();
		m_session.stLastActive = getNowStr();
		m_strErrorInfo = "";

		thread t(TcpClientRecvThread, this);
		t.detach();
	}


CONN_END:

	if (ret == false) {
		if (sockClient > 0) {
#ifdef _WIN32
			closesocket(sockClient);
#else
			close(sockClient);
#endif
		}
		sockClient = 0;
	}

	return ret;
}

bool tcpClt::ReConnect()
{
	if (!m_pCallBackUser || m_remoteIP == "") return false;
	return connect(m_pCallBackUser, m_remoteIP, m_remotePort);
}

int tcpClt::SendData(char* pData, size_t iLen)
{
	return SendData((unsigned char*)pData,iLen);
}

int tcpClt::SendData(unsigned char* pData, size_t iLen)
{
	if(iLen==0)
	{
		return 0;
	}
	if(sockClient == 0)
	{
		return 0;
	}

	int iRet = send(sockClient, (char*)pData, (int)iLen,0);
	if(iRet <= 0)
	{
#ifdef _WIN32
	closesocket(sockClient); 
	if (iRet == 0) {
		m_strErrorInfo = "[tcpClt]send error,ret=0,connection closed by peer";
	}
	else {
		m_strErrorInfo = "[tcpClt]send error,err code=" + std::to_string(WSAGetLastError());
	}
	m_strErrorInfo += " " + m_remoteIP + ":" + std::to_string(m_remotePort);
	printf(m_strErrorInfo.c_str());
	m_lastErrTime = getNowStr();
#else
	close(sockClient);
#endif
		sockClient = 0;
		m_bConn = false;
		m_session.iSendFailCount += iRet;
	}
	else
	{
		m_session.iSendSucCount += iRet;
	}

	return iRet;
}

bool tcpClt::DisConnect()
{
	if (sockClient)
	{
#ifdef _WIN32
	closesocket(sockClient);
#else
	close(sockClient);
#endif
		sockClient = 0;
		m_bConn = false;
	}

	return true;
}

bool operator==(const tcpClt& lhs, const tcpClt& rhs) {
	if (lhs.m_remoteIP == rhs.m_remoteIP &&
		lhs.m_remotePort == rhs.m_remotePort &&
		lhs.m_strLocalIP == rhs.m_strLocalIP &&
		lhs.m_iLocalPort == rhs.m_iLocalPort ) 
	{
		return true;
	}
	else {
		return false;
	}
}
bool operator!=(const tcpClt& lhs, const tcpClt& rhs) {
	return !operator==(lhs, rhs);
}