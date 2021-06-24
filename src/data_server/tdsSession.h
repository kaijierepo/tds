#pragma once
#include "tdscore.h"
#include "tcpClt.h"
#include "tcpSrv.h"
#include <mutex>
#include "stream2pkt.h"

class DS_TRANS_LAYER_SESSION 
{
public:
	DS_TRANS_LAYER_SESSION() {
		Init();
	}
	string iTLProto; //应用层的传输层协议 可以是websocket  websocket相对于 tcpServer 属于应用层数据。相对于tdsrpc，属于传输层协议
	string iALProto;
	bool boolConnected;
	tcpSession* pTcpClt;
	SYSTEMTIME rCreateTime;
	SOCKET sock;
	stream2pkt m_alBuf; //stream buff for app layer data
	stream2pkt m_tlBuf; //stream buff for transport layer data
	virtual void Init()
	{
		iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN;
		iALProto = APP_LAYER_PROTO_TYPE::PROTOCOL_UNKNOWN;
		pTcpClt = NULL;
		sock = 0;
		m_alBuf.Init();
	}
};

//Client表示的是应用协议层的client,通信层可能关联 tcpclient或者tcpServer
class TDS_SESSION : public DS_TRANS_LAYER_SESSION{
public:
    TDS_SESSION();
	string role;
	string type;
	string name;
	string user;
	string loginTime;
	string encode;
	string ip;
	map<string, string> mapTagDataSubscribe;
	bool bSubAll;//订阅所有
	CTCPClient* pTcpServ; //一个会话可能关联 一个tcpclient也可能是一个tcpServer
	CTLServer* pTLServer; //传输层服务器
	bool bVideoStream;
    bool bInitSegSended;
	string bridgedLocalCom; //和本地串口桥接
	string bridgedTcpServer; //和tcp服务器的一个连接桥接
	CTCPClient* pBridgedTcpClient;
	class CBridgedTcpClientHandler:public ITcpClientCallBack {
	public:
		virtual void ConnStatusChange(ConnInfo* connInfo, bool bIsConn);
		virtual void OnRecvData_TCPClient(char* pData, int iLen, ConnInfo* connInfo);
		TDS_SESSION* pTdsSession;
	} bridgedTcpCltHandler;

    void Init() override;
    string GetClientIp();
    int send(char* p,int len);
    std::mutex m_mutex;
};