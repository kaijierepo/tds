#pragma once
#include <string>
using namespace std;

typedef void (*fp_ioAddrRecv)(void* user, char* pData, int iLen);
typedef string (*fp_rpcHandler)(string strReq, string& strResp, string& error);
typedef bool (*fp_startStream)(bool start,void* puller); //启动码流，并传入拉流者id


namespace TDS_SESSION_TYPE {
	const string none = "none";
	const string rpc = "rpc";
	const string video = "video";
	const string web = "web";
	const string log = "log";
	const string tunnel = "tunnel"; //tunnel to serial ,tcpserver 
	const string iodev = "iodev"; //io设备会话 传输设备自定义的通信协议
}

struct ACTIVE_TDS_SESSION {
	string ip;
	int port;
	string type;
};


struct iTDSConf {
	int port;
	bool debugMode;
	string logLevel;
	string projectConfPath;
	bool bConcurrentGateway;
	string dbPath;
	string dataCenterIp;
	string title;
	string homepage;
	string uiMode;
	vector<ACTIVE_TDS_SESSION> vecActiveSession;
};

enum STREAM_TYPE {
	ST_BMP, //bmp流 rgb
	ST_h264_ES, //264 ES流
	ST_RGBA, //原始rgba数据 用于canvas播放视频
};


struct STREAM_INFO {
	int w;
	int h;
	STREAM_TYPE type;
};


//interface of TDS
class iTDS {
public:
	virtual string getVer() = 0;
	virtual bool setEncodeing(string encoding) = 0; // utf8 or gb2312
	virtual bool run(string cmdline = "") = 0;

	// tds 数据服务功能
	virtual string call(string method, string param,string& error) = 0;
	virtual void setRpcHandler(fp_rpcHandler handler) = 0;

	// io 通信服务功能
	virtual bool sendToIoAddr(string ioAddr,char* p,int l) = 0;
	virtual bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback) = 0;

	// 视频功能
	virtual void registerVideoTag(string tag, fp_startStream startStream,void*& mp, STREAM_INFO* si = NULL) = 0;
	//推流到指定的监测点mp
	virtual void pushStream(void* mp, char* pData, int len, STREAM_TYPE st, STREAM_INFO* si=NULL) = 0;

	// 通用服务功能
	virtual void log(char* text) = 0;

	//数据接口
	iTDSConf* conf;
};


#ifndef _TDS

typedef iTDS* (*fp_getTds)();

inline iTDS* getITDS() {
	HMODULE hMod = LoadLibrary("tds.dll");
	if (hMod)
	{
		fp_getTds pGetTds = (fp_getTds)GetProcAddress(hMod, "getTds");
		if (pGetTds)
		{
			return pGetTds();
		}
	}
	return NULL;
}

#endif

extern iTDS* tds;