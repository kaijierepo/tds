#pragma once
#include "tdscore.h"
#include "stream2pkt.h"
#include "mutex"
#include "tcpSrv.h"
#include "tcpClt.h"

class ioDev;

struct CAN_FRAME  //size = 5 bytes
{
	char DLC : 4;   //bit0-3  数据域长度 取值为1~8
	char r0 : 2;   //bit4-5
	char RTR : 1;  //bit6
	char FF : 1;  //bit7    =1表示扩展帧  =0表示标准帧
	char byIDHighHigh;
	char byIDHighLow;
	char byIDLowHigh;
	char byIDLowLow;
	char arrData[8];

	bool IsCanEx()
	{
		return FF == 1;
	}
};

struct PktQueue {
	HANDLE m_mutex;
	std::queue<PKT_DATA*> queueBufPacket;
	PktQueue() {
		m_mutex = CreateMutex(NULL, FALSE, NULL);
	}
	~PktQueue() {
		if (m_mutex) {
			CloseHandle(m_mutex);
			m_mutex = NULL;
		}

		while (!queueBufPacket.empty()) {
			PKT_DATA* pkt = queueBufPacket.front();
			queueBufPacket.pop();
			delete pkt;
		}
	}


	bool Lock() {
		if (m_mutex) {
			WaitForSingleObject(m_mutex, INFINITE);
			return true;
		}
		return false;
	}
	void Unlock() {
		if (m_mutex) ReleaseMutex(m_mutex);
	}
	void Clear();

	PKT_DATA* Pop()
	{
		PKT_DATA* pkt = NULL;
		Lock();
		if (!queueBufPacket.empty()) {
			pkt = queueBufPacket.front();
			queueBufPacket.pop();
			Unlock();
			return pkt;
		}
		else {
			Unlock();
			return NULL;
		}
	}

	void Push(PKT_DATA* pkt)
	{
		Lock();
		queueBufPacket.push(pkt);
		Unlock();
	}
};

class ioAddrSession {
public:
	ioAddrSession()
	{
		ClearStatis();
		bridgeSock = 0;
		pIODev = NULL;
	}

	void ClearStatis()
	{
		up_RevFrameNum = 0;
		up_SendFrameNum = 0;
		down_RevFrameNum = 0;
		down_SendFrameNum = 0;
		errorFrameNum = 0;
		RecvBytes = 0;
		SendBytes = 0;
		AbandonBytes = 0;
	}

	void CommLock();
	bool CommLockWithTime(int dwTimeoutMS = 0);


	void CommUnlock()
	{
		m_csCommLock.unlock();
	}

	ioPath addr;

	int up_RevFrameNum;
	int up_SendFrameNum;
	int down_RevFrameNum;
	int down_SendFrameNum;
	int errorFrameNum;
	int RecvBytes;
	int SendBytes;
	int AbandonBytes;
	
	char bridgeStationNoClientNo[4];
	SOCKET bridgeSock; //当该socket为非0时，将设备数据桥接传输到该socket  
	stream2pkt stream2pkt;
	string strInSyncCmdID; //正在进行同步通讯的命令id 设置为 * ，所有的命令都进同步队列
	PktQueue SyncPktQueue; // 同步处理队列组
	PktQueue AysnPktQueue; // 异步处理队列组// 异步处理队列。同步模式下的通知包。 异步模式下的所有包进入该队列。有专门的处理线程处理
	std::mutex m_queueAysnLock;
	std::timed_mutex m_csCommLock;
	DWORD m_dwLockThread;
	ioDev* pIODev; //ioDev with this addr
};
//Can中继缓冲，用于Can中继的字节流组包，防止单个can包被分为多次回调上送
//如果can载荷是没有头尾的无法进行字节流组包的数据，利用Can帧的编号进行组装，则缓存在canPayloadBuff中
struct CAN_TRANSMIT_BUF {
	string strIP;
	char canBuff[10000];
	char canPayloadBuff[10000];
	int iBuffLen;
	int iPayloadBuffLen;

	string type;
	TDS::IO_GATEWAY_TYPE gwType;
	CAN_TRANSMIT_BUF()
	{
		memset(canBuff, 0, 10000);
		memset(canPayloadBuff, 0, 10000);
		iBuffLen = 0;
		iPayloadBuffLen = 0;
	}
};





//1.function
//	send and recv data using ioPath
//2.key point
/*
data comm based on ioPath
data buffer in commserver ,not in ioDev instance,so data comm can be excuted without an ioDev instance
commserver can be bridged using tdsRPC
ioPath can be lock,preventing concurrent operation
comm statis based on ioPath

对于网关下的设备的数据通讯，有两种设计方案
1.将数据包转发给ioGateway对象，由网关对象转发给网关下设备的ioDev对象
2.由commSrv进行链路层处理，直接转发给ioGateway下设备的ioDev对象
目前采用方案2的设计方式
*/

class commServer : public ITcpServerCallBack, public ITcpClientCallBack
{
public:
	commServer(void);
	~commServer(void);

	bool RequestAndWaitResponse(PKT_DATA* req, PKT_DATA* resp, ioPath addr, REQ_PARAM* reqParam = NULL);

	void Run();
	void Stop();

	//通信设备互斥锁 
	ioAddrSession* GetCommAddrInfo(string iID, string strIP);
	ioAddrSession* GetCommAddrInfo(ioPath addr);
	void CommLock(ioPath addr);
	bool CommLock(ioPath addr, int iMilliSecond);
	void CommUnlock(ioPath addr);


	//传输层通信(Can包作为设备应用层数据的承载协议包，因此Can传输看作是通信传输层，实现应用层数据的传输，commServer实现对传输层协议封装)
	virtual void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	void ClearRecvBuff(ioPath addr);
	bool GetResponse(PKT_DATA& req, PKT_DATA& resp, ioPath addr, REQ_PARAM* reqParam);
	bool IsAddrConnected(ioPath addr);
	//应用层通信
	void OnRecvData_EqpAppLayerData(char* pData, int iLen, string strID, string strIP, bool bIsWholePkt = false); //bIsWholePkt=0表示回调上来的是应用层字节流，需要进行应用层组包；为1表示回调上来的是一个完整的应用层数据包
	void OnRecvData_EqpAppLayerPkt(PKT_DATA* ppd, ioAddrSession* pAddrInfo);


	//组装
	int StaticConnData(PKT_DATA_WITH_CONNDIR* datawithdir);

	void StatisOnRecv(char* recvData, int len, ioPath addr, recvPktType dealType = RECV_PKT_UNKNOWN);
	void StatisOnSend(char* sendData, int len, ioPath addr);
	bool m_bEndSession;

	//数据收发
	bool SendData(char* pData, int iLen, ioPath addr);
	bool SendCanFrameRaw(void* buf, int iDataLen, int iID);

	//用于发送自带组包功能的can载荷
	void sendCanV1(char* pData, int iLen, string strIP, int iID);
	void sendCanV3(char* pData, int iLen, string strIP, int iID);
	//用于利用Can分帧组包的Can载荷
	void sendCanV2(char* pData, int iLen, string strIP, int iID);
	virtual void ConnStatusChange(ConnInfo* connInfo, bool bIsConn);
	virtual void ConnStatusChange(tcpSession* pCltInfo, bool bIsConn);
	virtual void OnRecvData(char* pData, int iLen, ConnInfo* connInfo);
	void OnRecvData_TCPClient(char* pData, int iLen, ConnInfo* connInfo);
	bool CheckIsGateWay(string strIP);
	bool DealPackageAsyn();

public:
	//网络通信
	tcpSrv m_tcpServer;
	map<ioPath, CTCPClient*> m_tcpClientList;

	//中继通讯接收缓冲
	std::map<string, CAN_TRANSMIT_BUF*> m_mapGateWay;

	//设备通讯接受缓冲
	std::map<ioPath, ioAddrSession*> m_mapCommAddrInfo;  //一个通讯地址的所有管理信息.首次收到该通信地址的数据，加入管理信息。后续就不再删除
	mutex m_csRecvBuffListLock;
	HANDLE m_threadPackageDeal;
	SOCKET m_RemoteBridgeSock;
	//保存每个cmd的rtt
	std::map<string, std::vector<int>*> m_mapCmdRtt;
};

extern commServer commSrv;
void CommServer_SendData(char* pData, int iLen, ioPath addr);