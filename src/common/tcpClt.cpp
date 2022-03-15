#include "pch.h"
#include "tcpClt.h"
#pragma warning(disable:4996)
std::vector<tcpClt*> m_vecTCPIOCPClient;


DWORD WINAPI TcpClientRecvThread(LPVOID lpParam)
{
	tcpClt *pTcpClt=(tcpClt*)lpParam;
	pTcpClt->m_csLock.lock();
	SOCKET sock = pTcpClt->sockClient;

	pTcpClt->m_session.srvIP = pTcpClt->m_remoteIP;
	pTcpClt->m_session.srvPort = pTcpClt->m_remotePort;
	pTcpClt->m_session.sock = sock;
	pTcpClt->m_session.tcpClt = pTcpClt;

	pTcpClt->m_pCallBackUser->statusChange_tcpClt(&pTcpClt->m_session, true);

	vector<char> recvBuff;
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
			closesocket(sock);

			pTcpClt->m_pCallBackUser->statusChange_tcpClt(&pTcpClt->m_session, false);
			pTcpClt->sockClient = 0;
			break;
		}

		pTcpClt->m_session.iRecvCount += ret;
		iRecvBuffLen += ret;

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

		pTcpClt->m_pCallBackUser->OnRecvData_TCPClient(recvBuff.data(), iRecvBuffLen, &pTcpClt->m_session);
	
		iRecvBuffLen = 0;
	}

	pTcpClt->sockClient=0;
	pTcpClt->m_bConn = false;
	pTcpClt->m_csLock.unlock();
	return 0;
}

DWORD WINAPI ConnectThread(LPVOID lpParam)
{
	tcpClt* p = (tcpClt*) lpParam;
	int ct = 0;
	while (1)
	{
		if(!p->IsConnect())
				p->connect();
		Sleep(1000);

		ct++;
		if (ct == 15)
		{
			ct = 0;
			if (p->IsConnect() && p->heartbeat.size() > 0 )
				p->SendData(p->heartbeat.data(), p->heartbeat.size());
		}
	}
	return 0;
}

DWORD WINAPI AsynConnectThread(LPVOID lpParam)
{
	tcpClt* p = (tcpClt*)lpParam;
	p->connect();
	return 0;
}



tcpClt::tcpClt(void)
{
	sockClient = 0;
	m_remoteIP = "127.0.0.1";
	m_remotePort = 0;
	m_bConn = false;
	m_bIsConnectting = false;
	lastConnTime.wYear = 0; 
	lastConnTime.wMonth = 0;
	lastConnTime.wDay = 0;
	lastConnTime.wHour = 0;
	lastConnTime.wMinute = 0;
	lastConnTime.wSecond = 0;
	lastConnTime.wMilliseconds = 0;
	ZeroMemory(&lastConnTime, 0);
	m_vecTCPIOCPClient.push_back(this);
}

tcpClt::~tcpClt(void)
{
	stop();
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
	int pos = host.find(":");
	string ip = host.substr(0, pos);
	string strPort = host.substr(pos + 1, host.length() - pos - 1);
	m_remoteIP = ip;
	m_remotePort = atoi(strPort.c_str());
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	return connect();
}

bool tcpClt::run(ITcpClientCallBack* pUser, string strServIP, int iServPort, string strLocalIp, int iLocalPort)
{
	m_pCallBackUser = pUser;
	m_remoteIP = strServIP;
	m_remotePort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	DWORD dwThread;
	HANDLE hThread = CreateThread(NULL, 0, ConnectThread, (LPVOID)this, 0, &dwThread);
	return 0;
}

void tcpClt::stop()
{
	DisConnect();
	m_csLock.lock();
	m_csLock.unlock();
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
	DWORD dwThread;
	HANDLE hThread = CreateThread(NULL,0, AsynConnectThread,(LPVOID)this,0,&dwThread);
}

bool tcpClt::connect()
{
	if(sockClient !=0)
	{
		return true;
	}

	// initial socket library
	WORD wVerisonRequested;
	WSADATA wsaData;
	int err;
	wVerisonRequested = MAKEWORD(1, 1);
	err = WSAStartup(wVerisonRequested, &wsaData);
	if (err != 0)
	{
		return false;
	}

	//create socket
	SOCKADDR_IN sAddTemp;
	sAddTemp.sin_family = AF_INET;
	sAddTemp.sin_addr.S_un.S_addr=inet_addr(m_strLocalIP.c_str());
	sAddTemp.sin_port = htons(0);

	sockClient=socket(AF_INET,SOCK_STREAM,0);
	if(m_strLocalIP.length() > 0 && m_iLocalPort != -1)
	{
		if(::bind(sockClient, (SOCKADDR *)&sAddTemp, sizeof(SOCKADDR))==SOCKET_ERROR)
		{
			m_strErrorInfo = "绑定IP失败";
			return false;
		}
	}

	SOCKADDR_IN addrSrv;
	addrSrv.sin_addr.S_un.S_addr=inet_addr(m_remoteIP.c_str());
	addrSrv.sin_family=AF_INET;
	addrSrv.sin_port=htons(m_remotePort);
	m_bIsConnectting = true;
	int nConnect = ::connect(sockClient,(SOCKADDR*)&addrSrv,sizeof(SOCKADDR));
	m_bIsConnectting = false;

	if(nConnect == SOCKET_ERROR)
	{
		closesocket(sockClient);
		sockClient = 0;
		return false;
	}

	DWORD dwThread;
	HANDLE hThread = CreateThread(NULL,0,TcpClientRecvThread,(LPVOID)this,0,&dwThread);
	GetLocalTime(&lastConnTime);
	m_bConn = true;

	return true;
}

bool tcpClt::ReConnect()
{
	if (!m_pCallBackUser || m_remoteIP == "") return false;
	return connect(m_pCallBackUser, m_remoteIP, m_remotePort);
}

int tcpClt::SendData(char* pData, int iLen)
{
	if(iLen==0)
	{
		return 0;
	}
	if(sockClient == 0)
	{
		return 0;
	}

	int iRet = send(sockClient, (char*)pData, iLen,0);
	if(iRet <= 0)
	{
		closesocket(sockClient);
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

string tcpClt::GetLocalIP()
{
	string remoteIP;
	WSADATA wsaData;
	char name[155];
	char *ip;
	PHOSTENT hostinfo;
	if ( WSAStartup( MAKEWORD(2,0), &wsaData ) == 0 )
	{
		if( gethostname ( name, sizeof(name)) == 0)
		{
			if((hostinfo = gethostbyname(name)) != NULL)
			{
				ip = inet_ntoa (*(struct in_addr *)*hostinfo->h_addr_list); //得到地址字符串
				remoteIP = ip;
			}
		}
		WSACleanup( );
	}
	return remoteIP;
};

bool tcpClt::DisConnect()
{
	if (sockClient)
	{
		closesocket(sockClient);
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