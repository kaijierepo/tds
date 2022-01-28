#pragma once 
#include <map>
#include <winsock2.h>
#include <vector>
#include <mutex>
#include <memory>

using namespace std;

struct tcpSession
{
	SOCKET sock;
	SOCKADDR_IN clientAddr;
	string remoteIP;
	int remotePort;
	bool bIsTransmit;
	int iSendSucCount;
	int iSendFailCount;
	int iRecvCount;
	int iKeepAliveTimeout;
	void* pTcpServer;  
	void* pALSession; 
	SOCKET bridgeSock;
	bool bEnableActivityCheck; //是否进行活动检测

	SYSTEMTIME stLastActive;

	tcpSession()
	{
		GetLocalTime(&stLastActive);
		sock = NULL;
		pALSession = NULL;
		pTcpServer = NULL;
		bIsTransmit = false;
		iSendSucCount = 0;
		iSendFailCount = 0;
		iRecvCount = 0;
		iKeepAliveTimeout = 0;
		bEnableActivityCheck = true;
	}

	tcpSession* GenerateClienInfo() {
		tcpSession* ptr;
		ptr = new tcpSession();
		if (ptr) {
			*ptr = *this;
			ptr->pTcpServer = NULL;
		}
		return ptr;
	}
};

class ITcpServerCallBack {
public:
	virtual void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) = 0;
	virtual void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo) = 0;
};


class CIOCP
{
public:
	CIOCP(int nMaxConcurrent = -1)
	{
		m_hIOCP = NULL;
		if (nMaxConcurrent != -1)
		{
			Create(nMaxConcurrent);
		}
	}
	~CIOCP()
	{
		if (m_hIOCP != NULL)
		{
			CloseHandle(m_hIOCP);
		}
	}

	bool Close()
	{
		bool bResult = CloseHandle(m_hIOCP);
		m_hIOCP = NULL;
		return bResult;
	}

	//创建IOCP， nMaxConcurrency指定最大线程并发数量， 0 默认为CPU核数
	bool Create(int nMaxConcurrency = 0)
	{
		m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, nMaxConcurrency);
		//ASSERT(m_hIOCP != NULL);
		return (m_hIOCP != NULL);
	}

	//为设备（文件，socket,邮件槽，管道等）关联一个IOCP
	bool AssociateDevice(HANDLE hDevice, ULONG_PTR Compkey)
	{
		bool fOk = (CreateIoCompletionPort(hDevice, m_hIOCP, Compkey, 0) == m_hIOCP);
		return fOk;
	}

	bool AssociateSocket(SOCKET hSocket, ULONG_PTR CompKey)
	{
		return AssociateDevice((HANDLE)hSocket, CompKey);
	}

	bool PostStatus(ULONG_PTR CompKey, DWORD dwNumBytes = 0, OVERLAPPED* po = NULL)
	{
		bool fOk = PostQueuedCompletionStatus(m_hIOCP, dwNumBytes, CompKey, po);
		return fOk;
	}
	bool GetStatus(ULONG_PTR* pCompKey, PDWORD pdwNumBytes, OVERLAPPED** ppo, DWORD dwMillseconds = INFINITE)
	{
		return GetQueuedCompletionStatus(m_hIOCP, pdwNumBytes, pCompKey, ppo, dwMillseconds);
	}
	const HANDLE GetIOCP()
	{
		return m_hIOCP;
	}

private:
	HANDLE m_hIOCP; 
};

/*
在IOCP编程模型中，需要用到GetQueuedCompletionStatus()函数获取已完成的事件
但是该函数的返回参数无 Socket或者buffer的描述信息

一个简单的方法是：创建一个新的结构，该结构第一个参数是OVERLAPPED
由于AcceptEx, WSASend 等重叠IO操作传入的是Overlapped结构体的地址，调用AcceptEx等重叠IO操作，在Overlapped结构体后面开辟新的空间
写入socket或者buffer的信息，即可将socket或者buffer的信息由GetQueuedComletionStatus带回
*/
#define MAXBUF 8*1024
enum IOOperType
{
	TYPE_ACP,					//accept事件到达，有新的连接请求
	TYPE_RECV,				   //数据接收事件
	TYPE_SEND,               //数据发送事件
	TYPE_CLOSE_SOCK,			//服务端主动关闭socket
	TYPE_CLOSE,             //关闭事件
	TYPE_NO_OPER
};

class COverlappedIOInfo : public OVERLAPPED
{
public:
	COverlappedIOInfo(void)
	{
		m_socket = INVALID_SOCKET;
		ResetOverlapped();
		ResetRecvBuffer();
		ResetSendBuffer();
	}
	~COverlappedIOInfo(void)
	{
		if (m_socket != INVALID_SOCKET)
		{
			closesocket(m_socket);
			m_socket = INVALID_SOCKET;
		}
	}

	void ResetOverlapped()
	{
		Internal = InternalHigh = 0;
		Offset = OffsetHigh = 0;
		hEvent = NULL;
	}
	void ResetRecvBuffer()
	{
		ZeroMemory(m_crecvBuf, MAXBUF);
		m_recvBuf.buf = m_crecvBuf;
		m_recvBuf.len = MAXBUF;
	}
	void ResetSendBuffer()
	{
		ZeroMemory(m_csendBuf, MAXBUF);
		m_sendBuf.buf = m_csendBuf;
		m_sendBuf.len = MAXBUF;
	}


public:
	SOCKET m_socket;		
	WSABUF m_recvBuf;
	char m_crecvBuf[MAXBUF];
	WSABUF m_sendBuf;
	char m_csendBuf[MAXBUF];
	sockaddr_in m_addr;
	tcpSession m_cltInfo;
};

#define HISTORY_CONN_STATIC_COUNT 500


class tcpSrv  {
public:
	bool run(ITcpServerCallBack* pUser, int remotePort, string strLocalIP = "");
	void stop();

	bool SendData(char* pData, int iLen, tcpSession* pCltInfo);
	bool SendData(char* pData, int iLen, string remoteIP);
	bool SendData(char* pData, int iLen);
	ITcpServerCallBack* m_pCallBackUser;

	bool m_bStarted;

	void Log(char* sz);
	void (*pLog)(char*);

public:
	tcpSrv();
	~tcpSrv();
	bool StartListen(unsigned short port, string ip);
	DWORD SysTime2Unix(SYSTEMTIME& sDT);
	int CalcTimePassSecond(SYSTEMTIME* stLast, SYSTEMTIME* stNow = NULL);
	void ConnectionMaintain();
	/*
	释放3个部分步骤：
	1. 清空IOCP线程队列，退出线程
	2. 清空等待accept套接字m_vecAcps
	3. 清空已连接套接字m_vecContInfo并清空缓存
	*/
	void CloseServer();
	//启动CPU*2个线程，返回已启动的线程个数
	int StartWorkThreadPool();
	std::string formatStr(const char* pszFmt, ...);
	//处理accept请求，NumberOfBytes=0表示没有收到第一帧数据， >0时表示收到了第一帧数据
	bool DoAccept(SOCKET sockAccept, SOCKADDR_IN* ClientAddr);
	bool PostRecv(COverlappedIOInfo* info);
	bool DoRecv(COverlappedIOInfo* info);
	bool DeleteLink(SOCKET s);

	bool IsIPOnline(string remoteIP);
	inline string GetIOCPName() {
		return m_strName;
	}
	inline void SettIOCPName(string strName) {
		m_strName = strName;
	}
	inline void SetMonitoring(string value) {
		m_strMonitoringIP = value;
	}

	void ClearConnHistoryInfo();

	WSAData m_wsaData;
	SOCKET m_sListen;
	std::vector<COverlappedIOInfo*> m_vecContInfo;
	std::mutex m_csClientVectorLock;
	CIOCP m_iocp;
	string m_strMonitoringIP;

	static void ListenThread_IOCPServer(LPVOID lpParam);
	string m_strServerIP;
	int m_iServerPort;
	int keepAliveTimeout;

	string m_strName;
	bool m_bStopRecv;//该功能用于通信调试分析诊断问题，本地停止从Tcp缓冲接收数据，导致对端Tcp缓冲区满，测试对端程序处理缓冲区满时候的健壮性
	DWORD m_lastError;
};