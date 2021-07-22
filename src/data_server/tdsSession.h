#pragma once
#include "tdscore.h"
#include "tcpClt.h"
#include "tcpSrv.h"
#include <mutex>
#include "stream2pkt.h"


class MP;
class DS_TRANS_LAYER_SESSION 
{
public:
	DS_TRANS_LAYER_SESSION() {
		Init();
	}
	string iTLProto; //应用层的传输层协议 可以是websocket  websocket相对于 tcpServer 属于应用层数据。相对于tdsrpc，属于传输层协议
	string iALProto;
	bool boolConnected;
	CTLServer* pTLServer; //传输层服务器
	tcpSession* pTcpSession; //服务端被动连接的 session
	tcpClt* pTcpSessionClt; //作为客户端连接数据中心的 主动式tcpSession
	SYSTEMTIME rCreateTime;
	stream2pkt m_alBuf; //stream buff for app layer data
	stream2pkt m_tlBuf; //stream buff for transport layer data
	string bridgedLocalCom; //和本地串口桥接
	string bridgedTcpServer; //和tcp服务器的一个连接桥接
	tcpClt* pBridgedTcpClient;
	
	virtual void Init()
	{
		iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN;
		iALProto = APP_LAYER_PROTO_TYPE::PROTOCOL_UNKNOWN;
		pTcpSession = NULL;
		m_alBuf.Init();
	}
};


class TDS_SESSION : public DS_TRANS_LAYER_SESSION{
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
	int port;
	map<string, string> mapTagDataSubscribe;
	bool bSubAll;//订阅所有
	string streamFmt;// vp9/bmp/fmp4
    bool bInitSegSended;
	MP* streamMp; //tds拉流的源
	SOCKET sock;
	bool bMainWnd; //为true时，该连接断开就退出程序

	void onTcpDisconnect();

	class CBridgedTcpClientHandler:public ITcpClientCallBack {
	public:
		virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
		virtual void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
		TDS_SESSION* pTdsSession;
	} bridgedTcpCltHandler;

    void Init() override;
    string GetClientIp();
    int send(char* p,int len);
    std::mutex m_mutex;
};