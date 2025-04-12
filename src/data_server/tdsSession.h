#pragma once

#include "tcpClt.h"
#include "tcpSrv.h"
#include <mutex>
#include "stream2pkt.h"
#include "tds.h"
#include <queue>

//the protocol used as a transportation layer (no command specified in this layer,only for data transfer)
namespace TRANSFER_LAYER_PROTO_TYPE
{
	const string TLT_UNKNOWN = "tlp_unknonw";
	const string TLT_NONE = "tlp_none"; //no transportation layer
	const string TLT_CAN_V1 = "tlp_can_v1"; //payload is self framing protocol
	const string TLT_CAN_V2 = "tlp_can_v2"; //payload use can head subpkt info for framing
	const string TLT_WEB_SOCKET = "tlp_websocket";
	const string TLT_HTTP = "tlp_http";
};

class MP;
class ioDev;

struct TCP_DATA_BUFF {
	char* pData;
	size_t iLen;
};

namespace TDSP_SUB_TYPE{
	const string streamPusher = "streamPusher";
	const string childTds = "childTds";
}


//有状态会话信息，包含通信链路信息
class TDS_SESSION : public RPC_SESSION, public std::enable_shared_from_this<TDS_SESSION> {
public:
    TDS_SESSION();
	~TDS_SESSION();

	TDS_SESSION(tcpSession* p);
	TDS_SESSION(tcpSessionClt* p);

	void setRpcSession(RPC_SESSION* pRpc);

	void Init();
	bool isConnected();
	bool disconnect();
	string GetClientIp();
	size_t send(char* p, size_t len, bool bNeedLog = true);
	size_t send(unsigned char* p, size_t len, bool bNeedLog = true);
	size_t sendStr(string str, bool bNeedLog = true);
	size_t getSendedBytes();
	size_t getRecvedBytes();
	RPC_SESSION getRpcSession();
	void onTcpDisconnect();
	void setActivityCheck(bool bEnable);

	//地址信息与通信数据结构

	int sock;
	tcpSession* pTcpSession; //服务端被动连接的 session 代码中仅有两处设置。1是tdssession创建时 2.是tcp连接断开回调时,断开时设为null
	tcpSessionClt* pTcpSessionClt;  //tds作为客户端主动连接远端
	void* webServer;
	unsigned long conn_id; //mongoose conn id
	string getRemoteAddr();
	string getRemoteIP();
	string getLocalAddr();
	std::recursive_mutex m_mutexTcpLink; //tcp连接锁。处理连接断开修改tcpLink,数据发送线程使用tcpLink冲突的问题

	//基本属性
	string type;//session type
	bool m_bNeedLog; //该session是否要记录日志。调试类的一般不需记录日志
	string m_charset; //如果链接上走的是文本协议。文本协议的编码
	string iTLProto; //应用层的传输层协议 可以是websocket  websocket相对于 tcpServer 属于应用层数据。相对于tdsrpc，属于传输层协议
	string iALProto;
	bool m_bParentTds;

	//通信状态
	TIME stCreateTime;
	TIME lastRecvTime;
	TIME lastSendTime;
	bool bConnected; //指针的使用者检测到该变量为false后，应该弃用并释放该session对象
	bool m_bAppDataRecved;
	size_t abandonLen;
	std::atomic<size_t> sendedLen;
	std::atomic<size_t> recvedLen;

	//tcp数据缓存处理 
	queue<TCP_DATA_BUFF> dataBuff;
	std::mutex m_mutexTcpBuff;
	bool m_bSessionProcessing;


	//bridge session
	string bridgedLocalCom; //和本地串口桥接
	string bridgedTcpServer; //和tcp服务器的一个连接桥接
	tcpClt* pBridgedTcpClient;
	tcpSrv* pBridgedTcpServer;
	class CBridgedTcpClientHandler :public ICallback_tcpClt {
	public:
		virtual void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
		virtual void onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo);
		TDS_SESSION* pTdsSession;
	} bridgedTcpCltHandler;

	class CBridgedTcpServerHandler :public ICallback_tcpSrv {
	public:
		virtual void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
		virtual void onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pTcpSess);
		TDS_SESSION* pTdsSession;
	} bridgedTcpSrvHandler;


	//io device session
	string ioDevType;
	string tdspSubType;
	stream2pkt m_alBuf; //stream buff for app layer data
	stream2pkt m_tlBuf; //stream buff for transport layer data
	std::shared_ptr<TDS_SESSION> bridgedIoSession; //this是界面session,保留ioSession指针
	std::shared_ptr<TDS_SESSION> bridgedIoSessionClient; //this是ioSession指针，保留界面session指针


	//和ioSession绑定的ioDev
	//1个ioSession可以绑定多个ioDev(ioSession为设备网关时候)
	//1个ioDev只能绑定1个ioSession
	map<ioDev*, ioDev*> m_mapBindIoDev;
	ioDev* getBindDev(string ioAddr);

	//历史曾经在这个连接上上线过的io设备。当该设备重新在新的连接上线时。
	//m_mapBindIoDev中的关联关系会被删除。m_vecHistIoDev中的依然保留
	//在通过当前连接进行诊断时，方便追溯连接的来源。
	//因为4g模块可能会不断产生连接，特别是没电的时候。导致设备不断在新的连接上上线。解除老连接的关联
	//此时同一个设备可能有多个连接，这些连接在debugio的sessionList里面，有的当前绑定位号为空，但是历史绑定位号不为空
	//有的当前绑定位号不为空，说明这个是最新的连接，那些历史绑定位号不为空的，tcp老连接还在，但是在新连接上线了，
	//该功能可以在debugio的sessionList里面去分析到哪些设备在tcp连接上出了异常。
	map<string,string> m_vecHistIoDev; 
	map<string,string> m_vecHistIoBindTag;

	string m_ioAddr;     //当1个io设备对应一个tcp连接时，该ioAddr为设备io地址
	void bindIoDev(ioDev* ioDev);
	void unbindIoDev(ioDev* ioDev);

	//单设备模式，只有第一个注册包的地址信息有用。后面的地址信息无效.防止地址信息错误导致的问题，以第一包的地址信息为准
	bool m_bSingleDevMode; //单设备模式，默认可以多设备。 收到imei首发数据包则转换为单设备模式。
	vector<unsigned char> regPkt; //透传网关常用的注册包机制，此数据为第一包注册包
	bool m_bEnableIO;

	//hmr session
	string webHMRPath;


	//child tds session
	string m_childTdsTag;
	int m_childTdsHttpPort;
	int m_childTdsHttpsPort;

	//stream pusher session
	string streamId;
	vector<std::shared_ptr<TDS_SESSION>> m_vecPuller;
	mutex m_csPuller;
};

extern vector<std::shared_ptr<TDS_SESSION>> ioPktMonitorClient;