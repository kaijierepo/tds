#pragma once
#include <string>
#include <vector>
using namespace std;



typedef void (*fp_ioAddrRecv)(void* user, char* pData, int iLen);
typedef string (*fp_rpcHandler)(string strReq, string& strResp, string& error);
typedef bool (*fp_startStream)(bool start,void* puller); //启动码流，并传入拉流者id
typedef void (*fp_procBeforeExit)();//由tds模块触发的程序退出，主程序退出前需要做的清理工作

namespace TDS_SESSION_TYPE {
	const string none = "none";
	const string rpc = "rpc";
	const string video = "video";
	const string web = "web";
	const string log = "log";
	const string commpkt = "commpkt"; //通信数据包监视
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


//interface of tds.db
//key is timestamp as 2020-01-01 11:11:11,value is a json string of one data element
#define DB_DATA_SET std::map<string,string>
class i_database {
public:
	//增删改查操作 crud options
	//virtual void INSERT(string strTag, SYSTEMTIME stTime, json& jData, json dataFile = nullptr) = 0;
	//time: "2020-02-14~2020-02-15" or "1d1h1m30s"
	//filter: "humidiy==55 && temperature>30"
	//dataSet是一个json数组，数组成员为1个数据元。 meta是元数据，描述数据的一些信息
	//virtual bool SELECT(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result) = 0;
	virtual bool UPDATE(string tag, SYSTEMTIME stTime, string& sData) = 0;

	//底层基础操作
	
	//保存一个数据元文件。deFileUrl可以是 1.本机文件路径 2.文件夹路径 3.http文件或文件夹路径
	virtual void saveDEFile(string strTag, SYSTEMTIME stTime, string deFileUrl) = 0;

	//获得数据库文件db.json的路径
	virtual string getPath_dbFile(string strTag, SYSTEMTIME date) = 0;
	//获得数据元文件或者数据库文件的存储文件夹目录
	virtual string getPath_dataFolder(string strTag, SYSTEMTIME date) = 0;
	//获得数据元文件或者数据元文件夹的路径
	virtual string getPath_deFile(string strTag, SYSTEMTIME stTime) = 0;
	//获得数据库根路径
	virtual string getPath_dbRoot() = 0;
};



//interface of TDS
class iTDS {
public:
	virtual string getVer() = 0;
	virtual bool setEncodeing(string encoding) = 0; // utf8 or gb2312
	virtual bool run(string cmdline = "") = 0;
	virtual bool setProcBeforeExit(fp_procBeforeExit callback) = 0;

	// tds 数据服务功能
	virtual string call(string method, string param,string& error) = 0;
	virtual void setRpcHandler(fp_rpcHandler handler) = 0;
	virtual void rpcNotify(string method, string params = "", string sessionId = "") = 0;

	// io 通信服务功能
	virtual bool enableIoLog(string ioAddr, bool bEnable) = 0;
	virtual bool sendToIoAddr(string ioAddr, const char* p,int l) = 0;
	virtual bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback) = 0;

	// 视频功能
	virtual void registerVideoTag(string tag, fp_startStream startStream,void*& mp, STREAM_INFO* si = NULL) = 0;
	//推流到指定的监测点mp
	virtual void pushStream(void* mp, char* pData, int len, STREAM_TYPE st, STREAM_INFO* si=NULL) = 0;

	// 通用服务功能
	virtual void log(const char* text) = 0;

	//数据接口
	iTDSConf* conf;
	i_database* db;
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