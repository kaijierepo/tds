#include "common.h"
#include "tcpClt.h"
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

namespace tds_tcpClt {
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

string tcpSessionClt::getRemoteAddr() {
	string s = str::format("%s:%d", remoteIP.c_str(), remotePort);
	return s;
}


void TcpClientRecvThread(void* lpParam)
{
	tcpClt *pTcpClt=(tcpClt*)lpParam;
	pTcpClt->m_bRecvThreadRunning = true;
	int sock = pTcpClt->sockClient;

	pTcpClt->m_session.remoteIP = pTcpClt->m_remoteIP;
	pTcpClt->m_session.remotePort = pTcpClt->m_remotePort;
	pTcpClt->m_session.sock = sock;

	pTcpClt->m_pCallBackUser->statusChange_tcpClt(&pTcpClt->m_session, true);

	vector<unsigned char> recvBuff;
	int iRecvBuffLen = 0;
	int ret;
	while(1)
	{
		//buffer full , dynamicly increase 10k
		if(recvBuff.size() == iRecvBuffLen)
		{
			recvBuff.resize(recvBuff.size() + 100000);
		}

		ret=recv(sock,(char*)recvBuff.data() + iRecvBuffLen,recvBuff.size() - iRecvBuffLen,0);
		if(ret<=0)
		{
#ifdef _WIN32
			closesocket(sock);
#else
			close(sock);
#endif

			pTcpClt->m_pCallBackUser->statusChange_tcpClt(&pTcpClt->m_session, false);
			pTcpClt->sockClient = 0;
			break;
		}

		pTcpClt->m_session.iRecvCount += ret;
		iRecvBuffLen += ret;

#ifdef _WIN32
		//keep recv   prevent callback to applayer too many times.
		unsigned long bytesToRecv = 0;
		int iRet = ioctlsocket(sock, FIONREAD, &bytesToRecv);
		if (iRet == 0)
		{
			if(bytesToRecv > 0)
				continue;
		}
		else
		{
		}
#endif

		timeopt::now(&pTcpClt->m_session.stLastActive);
		pTcpClt->m_pCallBackUser->OnRecvData_TCPClient(recvBuff.data(), iRecvBuffLen, &pTcpClt->m_session);
	
		iRecvBuffLen = 0;
	}

	pTcpClt->sockClient=0;
	pTcpClt->m_bConn = false;
	pTcpClt->m_bRecvThreadRunning = false;
}



void AsynConnectThread(void* lpParam)
{
	tcpClt* p = (tcpClt*)lpParam;
	p->connect();
}


map<tcpClt*, tcpClt*> mapAllTcpClt;
mutex csAllTcpClt;
bool connectThreadRunning = false;


void ConnectThread(void* lpParam)
{
	while (1)
	{
#ifdef _WIN32
		Sleep(500);
#else
		sleep(500);
#endif

		for (map<tcpClt*, tcpClt*>::iterator  i = mapAllTcpClt.begin(); i != mapAllTcpClt.end(); i++) {
			tcpClt* p = i->first;
			if (!p->m_bRun)
				continue;

			if (p->IsConnect()) {
				if (p->m_keepAliveTimeout > 0) {
					if (timeopt::CalcTimePassSecond(p->m_session.stLastActive) > p->m_keepAliveTimeout) {
						p->DisConnect();
						printf("disconnect inactive connection %s:%d\r\n", p->m_remoteIP.c_str(), p->m_remotePort);
					}
				}
				continue;
			}
				
			if (p->m_bIsConnectting)
				continue;

			if (timeopt::CalcTimePassMilliSecond(p->lastConnTime) > 3000) {
				timeopt::now(&p->lastConnTime);
				thread t(AsynConnectThread, p);
				t.detach();
			}
		}
	}
}

tcpClt::tcpClt(void)
{
	sockClient = 0;
	m_session.tcpClt = this;
	m_remoteIP = "127.0.0.1";
	m_remotePort = 0;
	m_bConn = false;
	m_bRun = false;
	m_bIsConnectting = false;
	timeopt::now(&lastConnTime);
	m_vecTCPIOCPClient.push_back(this);
	m_bRecvThreadRunning = false;
	m_bConnThreadRunning = false;
	csAllTcpClt.lock();
	mapAllTcpClt[this] = this;
	if (!connectThreadRunning){
		connectThreadRunning = true;
		thread t(ConnectThread,this);
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

bool tcpClt::connect(ITcpClientCallBack* pUser, string strServIP,int iServPort,string strLocalIp,int iLocalPort )
{
	DisConnect();
	m_pCallBackUser = pUser;
	m_remoteIP = strServIP;
	m_remotePort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;

	return connect();
}

bool tcpClt::connect(ITcpClientCallBack* pUser, string host, string strLocalIp, int iLocalPort)
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

bool tcpClt::run(ITcpClientCallBack* pUser, string host, string strLocalIp, int iLocalPort)
{
	m_pCallBackUser = pUser;
	size_t pos = host.find(":");
	string ip = host.substr(0, pos);
	string strPort = host.substr(pos + 1, host.length() - pos - 1);
	m_remoteIP = ip;
	m_remotePort = atoi(strPort.c_str());
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	m_bRun = true;
	return true;
}

bool tcpClt::run(ITcpClientCallBack* pUser, string strServIP, int iServPort, string strLocalIp, int iLocalPort)
{
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
	m_bRun = false; //触发重连线程退出
	DisConnect();   //触发接收线程退出。
	while (1) {
#ifdef _WIN32
		Sleep(1);
#else
		sleep(1);
#endif
		if (!m_bConnThreadRunning && !m_bRecvThreadRunning)
			break;
	}
}

void tcpClt::AsynConnect(ITcpClientCallBack* pUser,string strServIP, int iServPort, string strLocalIp /*= ""*/, int iLocalPort /*= -1*/)
{
	if(m_bIsConnectting)
		return;
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

bool tcpClt::connect()
{
	if (sockClient != 0)
	{
		return true;
	}


	int nConnect;
	struct hostent* hptr;
	m_bIsConnectting = true;
	bool ret = false;

	//将域名解析成ip地址
	hptr = gethostbyname(m_remoteIP.c_str());
	if (hptr == NULL || hptr->h_addr == NULL) {
		m_bIsConnectting = false;
		return false;
	}

	//create socket
	sockaddr_in sAddTemp;
	sAddTemp.sin_family = AF_INET;
	sAddTemp.sin_addr.s_addr=inet_addr(m_strLocalIP.c_str());
	sAddTemp.sin_port = htons(0);

	sockClient=socket(AF_INET,SOCK_STREAM,0);
	if(m_strLocalIP.length() > 0 && m_iLocalPort != -1)
	{
		if(-1 == ::bind(sockClient, (sockaddr*)&sAddTemp,sizeof(sockaddr)))
		{
			m_strErrorInfo = "绑定IP失败";
			m_bIsConnectting = false;
			return false;
		}
	}


	sockaddr_in addrSrv;
	memcpy(&addrSrv.sin_addr.s_addr, hptr->h_addr_list[0], hptr->h_length);
	addrSrv.sin_family=AF_INET;
	addrSrv.sin_port=htons(m_remotePort);
	
	 nConnect = ::connect(sockClient,(sockaddr*)&addrSrv,sizeof(sockaddr));


	if(nConnect == -1)
	{
		m_strErrorInfo = "连接失败:" + sys::getLastError();
		m_bIsConnectting = false;
		return false;
	}
	//在创建TcpClientRecvThread之前设置m_bConn为true,因为TcpClientRecvThread中回调statucChange的时候可能会读取该变量
	m_bConn = true;
	ret = true;
	timeopt::now(&lastConnTime);
	timeopt::now(&m_session.stLastActive);
	m_strErrorInfo = "";

	thread t(TcpClientRecvThread,this);
	t.detach();

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
	m_bIsConnectting = false;
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

	size_t iRet = send(sockClient, (char*)pData, (int)iLen,0);
	if(iRet <= 0)
	{
#ifdef _WIN32
	closesocket(sockClient);
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