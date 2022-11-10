#include "pch.h"
#include "tcpClt.h"
#pragma warning(disable:4996)
std::vector<tcpClt*> m_vecTCPIOCPClient;


DWORD WINAPI TcpClientRecvThread(LPVOID lpParam)
{
	tcpClt *pTcpClt=(tcpClt*)lpParam;
	pTcpClt->m_bRecvThreadRunning = true;
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
	pTcpClt->m_bRecvThreadRunning = false;
	return 0;
}



DWORD WINAPI AsynConnectThread(LPVOID lpParam)
{
	tcpClt* p = (tcpClt*)lpParam;
	p->connect();
	return 0;
}


map<tcpClt*, tcpClt*> mapAllTcpClt;
mutex csAllTcpClt;
bool connectThreadRunning = false;


DWORD WINAPI ConnectThread(LPVOID lpParam)
{
	while (1)
	{
		Sleep(500);
		for (map<tcpClt*, tcpClt*>::iterator  i = mapAllTcpClt.begin(); i != mapAllTcpClt.end(); i++) {
			tcpClt* p = i->first;
			if (!p->m_bRun)
				continue;

			if (p->IsConnect())
				continue;

			if (p->m_bIsConnectting)
				continue;

			if (timeopt::CalcTimePassMilliSecond(p->lastConnTime) > 3000) {
				GetLocalTime(&p->lastConnTime);
				thread t(AsynConnectThread, p);
				t.detach();
			}
		}
	}
	return 0;
}

tcpClt::tcpClt(void)
{
	sockClient = 0;
	m_remoteIP = "127.0.0.1";
	m_remotePort = 0;
	m_bConn = false;
	m_bRun = false;
	m_bIsConnectting = false;
	GetLocalTime(&lastConnTime);
	m_vecTCPIOCPClient.push_back(this);
	m_bRecvThreadRunning = false;
	m_bConnThreadRunning = false;
	csAllTcpClt.lock();
	mapAllTcpClt[this] = this;
	if (!connectThreadRunning){
		connectThreadRunning = true;
		DWORD dwThread;
		HANDLE hThread = CreateThread(NULL, 0, ConnectThread, (LPVOID)this, 0, &dwThread);
	}
	csAllTcpClt.unlock();
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
		Sleep(1);
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
	DWORD dwThread;
	HANDLE hThread = CreateThread(NULL,0, AsynConnectThread,(LPVOID)this,0,&dwThread);
}

bool tcpClt::connect()
{
	DWORD dwThread;
	HANDLE hThread;
	int nConnect;
	struct hostent* hptr;
	m_bIsConnectting = true;
	bool ret = false;
	if(sockClient !=0)
	{
		ret = true;
		goto CONN_END;
	}

	// initial socket library
	WORD wVerisonRequested;
	WSADATA wsaData;
	int err;
	wVerisonRequested = MAKEWORD(1, 1);
	err = WSAStartup(wVerisonRequested, &wsaData);
	if (err != 0)
	{
		ret = false;
		goto CONN_END;
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
			ret = false;
			goto CONN_END;
		}
	}


	 hptr = gethostbyname(m_remoteIP.c_str());
	if (hptr == NULL || hptr->h_addr == NULL) {
		ret = false;
		goto CONN_END;
	}
		

	SOCKADDR_IN addrSrv;
	CopyMemory(&addrSrv.sin_addr.S_un.S_addr, hptr->h_addr_list[0], hptr->h_length);
	addrSrv.sin_family=AF_INET;
	addrSrv.sin_port=htons(m_remotePort);
	
	 nConnect = ::connect(sockClient,(SOCKADDR*)&addrSrv,sizeof(SOCKADDR));


	if(nConnect == SOCKET_ERROR)
	{
		m_strErrorInfo = "连接失败:" + sys::getLastError();
		closesocket(sockClient);
		sockClient = 0;
		ret = false;
		goto CONN_END;
	}
	//在创建TcpClientRecvThread之前设置m_bConn为true,因为TcpClientRecvThread中回调statucChange的时候可能会读取该变量
	m_bConn = true;
	GetLocalTime(&lastConnTime);
	m_strErrorInfo = "";

	 hThread = CreateThread(NULL,0,TcpClientRecvThread,(LPVOID)this,0,&dwThread);

CONN_END:
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