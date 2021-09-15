#pragma once
#include "tdscore.h"
#include "tcpClt.h"
#include "tcpSrv.h"
#include <mutex>
#include "stream2pkt.h"


class MP;

struct TCP_DATA_BUFF {
	char* pData;
	int iLen;
};


class TDS_SESSION{
public:
    TDS_SESSION();
	~TDS_SESSION();
	string role;
	string name;
	string user;
	string loginTime;
	string encode;
	string ip;
	string type;//session type
	string lastMethodCalled;
	SYSTEMTIME lastRecvTime;
	SYSTEMTIME lastSendTime;
	int port;
	map<string, string> mapTagDataSubscribe;
	bool bSubAll;//订阅所有
	string streamFmt;// vp9/bmp/fmp4
    bool bInitSegSended;
	MP* streamMp; //tds拉流的源
	SOCKET sock;
	bool bMainWnd; //为true时，该连接断开就退出程序
	queue<TCP_DATA_BUFF> dataBuff;
	void* dsCltStream; //转发给httplib的流
	string sendContent; //text or binary

	void onTcpDisconnect();
	void setActivityCheck(bool bEnable);
	class CBridgedTcpClientHandler:public ITcpClientCallBack {
	public:
		virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
		virtual void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
		TDS_SESSION* pTdsSession;
	} bridgedTcpCltHandler;

    void Init();
    string GetClientIp();
    int send(char* p,int len);
    std::recursive_mutex m_mutex; //使用递归锁的原因是 接收处理线程当中可能调用session进行发送，可能锁两次
	string iTLProto; //应用层的传输层协议 可以是websocket  websocket相对于 tcpServer 属于应用层数据。相对于tdsrpc，属于传输层协议
	string iALProto;
	bool bConnected; //指针的使用者检测到该变量为false后，应该弃用并释放该session对象
	CTLServer* pTLServer; //传输层服务器
	tcpSession* pTcpSession; //服务端被动连接的 session 代码中仅有两处设置。1是tdssession创建时 2.是tcp连接断开回调时
	tcpClt* pTcpSessionClt; //作为客户端连接数据中心的 主动式tcpSession
	SYSTEMTIME stCreateTime;
	stream2pkt m_alBuf; //stream buff for app layer data
	stream2pkt m_tlBuf; //stream buff for transport layer data
	string bridgedLocalCom; //和本地串口桥接
	string bridgedTcpServer; //和tcp服务器的一个连接桥接
	tcpClt* pBridgedTcpClient;
};