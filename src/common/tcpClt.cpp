#include "pch.h"
#include "tcpClt.h"
#pragma warning(disable:4996)
std::vector<CTCPClient*> m_vecTCPIOCPClient;


DWORD WINAPI TcpClientRecvThread(LPVOID lpParam)
{
	CTCPClient *pServ=(CTCPClient*)lpParam;

	SOCKET sock = pServ->sockClient;
	ConnInfo ci;
	ci.peerIP = pServ->m_strServerIP;
	ci.peerPort = pServ->m_iServerPort;
	ci.sock = sock;

	pServ->m_pCallBackUser->ConnStatusChange(&ci, true);

	vector<char> recvBuff;
	int iRecvBuffLen = 0;
	int ret;
	while(1)
	{
		//如果接收缓冲区满，动态增大10k
		if(recvBuff.size() == iRecvBuffLen)
		{
			recvBuff.resize(recvBuff.size() + 100000);//动态调整接收缓冲区大小
		}

		ret=recv(sock,(char*)recvBuff.data() + iRecvBuffLen,recvBuff.size() - iRecvBuffLen,0);
		if(ret<=0)
		{
			closesocket(sock);

			pServ->m_pCallBackUser->ConnStatusChange(&ci, false);
			pServ->sockClient = 0;
			break;
		}

		iRecvBuffLen += ret;

		//如果接收缓冲区还有数据，继续接收(该机制可以防止大数据分多次接收导致多次回调应用层，造成不必要的计算消耗)
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

		//接收完成，送到应用层
		pServ->m_pCallBackUser->OnRecvData_TCPClient(recvBuff.data(), iRecvBuffLen, &ci);
		
		//发送到监控界面
		PKT_DATA_WITH_CONNDIR* datawithdir = new PKT_DATA_WITH_CONNDIR((char*)recvBuff.data(), iRecvBuffLen);
		datawithdir->dir = CONNDIR_RECV;
		SYSTEMTIME st; GetLocalTime(&st);
		datawithdir->st = st;
		datawithdir->serverIP = pServ->m_strServerIP;
		datawithdir->serverPort = pServ->m_iServerPort;
		pServ->StaticConnData(datawithdir);

		iRecvBuffLen = 0;
	}

	pServ->sockClient=0;
	pServ->m_bConn = false;
	return 0;
}

DWORD WINAPI ConnectThread(LPVOID lpParam)
{
	CTCPClient* p = (CTCPClient*) lpParam;
	int ct = 0;
	while (1)
	{
		if(!p->IsConnect())
				p->connect();
		Sleep(5000);

		ct++;

		if (ct == 3)
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
	CTCPClient* p = (CTCPClient*)lpParam;
	p->connect();
	return 0;
}



CTCPClient::CTCPClient(void)
{
	sockClient = 0;
	m_strServerIP = "127.0.0.1";
	m_iServerPort = 0;
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

CTCPClient::~CTCPClient(void)
{
}

bool CTCPClient::connect(ITcpClientCallBack* pUser, string strServIP,int iServPort,string strLocalIp,int iLocalPort )
{
	DisConnect();
	m_pCallBackUser = pUser;
	m_strServerIP = strServIP;
	m_iServerPort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;

	return connect();
}

bool CTCPClient::connect(ITcpClientCallBack* pUser, string host, string strLocalIp, int iLocalPort)
{
	DisConnect();
	m_pCallBackUser = pUser;
	int pos = host.find(":");
	string ip = host.substr(0, pos);
	string strPort = host.substr(pos + 1, host.length() - pos - 1);
	m_strServerIP = ip;
	m_iServerPort = atoi(strPort.c_str());
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	return connect();
}

bool CTCPClient::Run(ITcpClientCallBack* pUser, string strServIP, int iServPort, string strLocalIp, int iLocalPort)
{
	m_pCallBackUser = pUser;
	m_strServerIP = strServIP;
	m_iServerPort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	DWORD dwThread;
	HANDLE hThread = CreateThread(NULL, 0, ConnectThread, (LPVOID)this, 0, &dwThread);
	return 0;
}

void CTCPClient::AsynConnect(ITcpClientCallBack* pUser,string strServIP, int iServPort, string strLocalIp /*= ""*/, int iLocalPort /*= -1*/)
{
	if(m_bIsConnectting)
		return;
	if(m_bConn)
	DisConnect();
	m_pCallBackUser = pUser;
	m_strServerIP = strServIP;
	m_iServerPort = iServPort;
	m_strLocalIP = strLocalIp;
	m_iLocalPort = iLocalPort;
	DWORD dwThread;
	HANDLE hThread = CreateThread(NULL,0, AsynConnectThread,(LPVOID)this,0,&dwThread);
}

bool CTCPClient::connect()
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

	//1.创建套接字(socket)
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

	//2.向服务器发送连接请求(connect)
	SOCKADDR_IN addrSrv;
	addrSrv.sin_addr.S_un.S_addr=inet_addr(m_strServerIP.c_str());
	addrSrv.sin_family=AF_INET;
	addrSrv.sin_port=htons(m_iServerPort);
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

bool CTCPClient::ReConnect()
{
	if (!m_pCallBackUser || m_strServerIP == "") return false;
	return connect(m_pCallBackUser, m_strServerIP, m_iServerPort);
}

int CTCPClient::SendData(char* pData, int iLen)
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
	}

	//发送到监控界面
	PKT_DATA_WITH_CONNDIR* datawithdir = new PKT_DATA_WITH_CONNDIR((char*)pData, iLen);
	datawithdir->dir = CONNDIR_SEND;
	SYSTEMTIME st; GetLocalTime(&st);
	datawithdir->st = st;
	datawithdir->serverIP = m_strServerIP;
	datawithdir->serverPort = m_iServerPort;
	datawithdir->iSendResult = iRet;
	StaticConnData(datawithdir);

	return iRet;
}

string CTCPClient::GetLocalIP()
{
	string strIP;
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
				strIP = ip;
			}
		}
		WSACleanup( );
	}
	return strIP;
};

bool CTCPClient::DisConnect()
{
	closesocket(sockClient);
	sockClient = 0;
	m_bConn = false;
	return true;
}

int CTCPClient::StaticConnData(PKT_DATA_WITH_CONNDIR* datawithdir) {
	
	return 0;
}

bool operator==(const CTCPClient& lhs, const CTCPClient& rhs) {
	if (lhs.m_strServerIP == rhs.m_strServerIP &&
		lhs.m_iServerPort == rhs.m_iServerPort &&
		lhs.m_strLocalIP == rhs.m_strLocalIP &&
		lhs.m_iLocalPort == rhs.m_iLocalPort ) 
	{
		return true;
	}
	else {
		return false;
	}
}
bool operator!=(const CTCPClient& lhs, const CTCPClient& rhs) {
	return !operator==(lhs, rhs);
}