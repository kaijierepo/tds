#pragma once
#include "tdscore.h"
#include "tcpClt.h"
#include "tcpSrv.h"
#include <mutex>
#include "stream2pkt.h"


class MP;
class streamSrvNode;
class ioDev;

struct TCP_DATA_BUFF {
	char* pData;
	int iLen;
};


class FILE_WRITER {
public:
	FILE_WRITER() {
		fp = nullptr;
		totalLen = 0;
		writedLen = 0;
	}
	~FILE_WRITER() {
		if (fp)
		{
			fclose(fp);
		}
	}

	bool startWrite(string path,long len);
	void stopWrite();
	long write(char* pData, long iLen);
	FILE* fp;
	long totalLen;
	long writedLen;
	string filePath;
};


class TDS_SESSION{
public:
    TDS_SESSION();
	~TDS_SESSION();
	string role;
	string name; //name is defined by tds client
	string user;
	string org; //用户所在组织。根位号
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
	streamSrvNode* videoServiceNode; //tds拉流的源
	SOCKET sock;
	bool bMainWnd; //为true时，该连接断开就退出程序

	//会话通信数据处理控制
	queue<TCP_DATA_BUFF> dataBuff;
	std::mutex m_mutexTcpBuff;
	bool m_bSessionProcessing;

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
	bool isConnected();
    string GetClientIp();
    int send(char* p,int len);
	int getSendedBytes();
    std::mutex m_mutexTcpLink; //tcp连接锁。处理连接断开修改tcpLink,数据发送线程使用tcpLink冲突的问题
	string iTLProto; //应用层的传输层协议 可以是websocket  websocket相对于 tcpServer 属于应用层数据。相对于tdsrpc，属于传输层协议
	string iALProto;
	bool bConnected; //指针的使用者检测到该变量为false后，应该弃用并释放该session对象
	tcpSession* pTcpSession; //服务端被动连接的 session 代码中仅有两处设置。1是tdssession创建时 2.是tcp连接断开回调时,断开时设为null
	tcpClt* pTcpSessionClt; //作为客户端连接数据中心的 主动式tcpSession
	SYSTEMTIME stCreateTime;
	stream2pkt m_alBuf; //stream buff for app layer data
	stream2pkt m_tlBuf; //stream buff for transport layer data
	string bridgedLocalCom; //和本地串口桥接
	string bridgedTcpServer; //和tcp服务器的一个连接桥接
	tcpClt* pBridgedTcpClient;
	std::shared_ptr<TDS_SESSION> bridgedIoSession; //this是界面session,保留ioSession指针
	std::shared_ptr<TDS_SESSION> bridgedIoSessionClient; //this是ioSession指针，保留界面session指针
	ioDev* getIODev(string ioAddr);
	vector<ioDev*> m_vecIoDev;  //通过该tdsSession和tds通信的io设备
	ioDev* m_IoDevTcpLink;      //建立了tcp直连的io设备
	FILE_WRITER m_fileUploader;  //大文件上传控制
	DWORD httpReqHandleThreadID;  //处理http请求的线程id
};