#pragma once
#include <string>
#include <vector>
#include <map>
using namespace std;


//值类型
namespace TDS {
	namespace VAL_TYPE {
		const string json = "json";	//自定义类型。是一个json对象字符串
		const string Float = "float"; //实型
		const string integer = "int";
		const string boolean = "bool";
		const string video = "video";
		const string str = "string";
		const string car_strobe = "car_strobe";
		const string man_strobe = "man_strobe";
	};
	//IO设备层级
	namespace IO_DEV_LEVEL {
		const string server = "server";
		const string gateway = "gateway";
		const string device = "device";
		const string channel = "channel";
	}
	//IO设备类型
	namespace IO_DEV_TYPE {
		namespace DEV {
			const string modbus_rtu_slave = "modbus-rtu-slave";
			const string iq60_gateway = "iq60-gateway";
			const string genicam = "genicam";
		}
		namespace GW {
			const string local_serial = "local-serial";
			const string can_gateway = "can-gateway";
			const string rs485_gateway = "rs485-gateway";
		}
		namespace CHAN {
			const string io_channel = "io-channel";
		}
		namespace SERVER {
			const string tds = "tds";
		}
	}
};


#define STREAM_TYPE_ENUM string
namespace STREAM_TYPE {
	const string bmp = "bmp"; //bmp流 rgb
	const string h264 = "h264"; //264 ES流
	const string rgba = "rgba"; //原始rgba数据 用于canvas播放视频
	const string mono8 = "mono8";
	const string mono16 = "mono16";
}


struct STREAM_INFO {
	int w;
	int h;
	int pixelSize;
	string pixelFmt;
	float frameRate;
	STREAM_INFO()
	{
		w = 0;
		h = 0;
		pixelSize = 0;
		pixelFmt = "";
		frameRate = 0;
	}
};

//通过 getITDS 获得tds接口总线，访问tds中的各项内容
//总线上有一些固定元素，可以调用，例如
//tds.db 数据库对象

//模块总线消息
//tds中的不同模块，可以通过总线消息沟通
//基于tds的二次开发，可以看做是对总线上模块的扩展，与tds总线上的各个模块沟通
struct MODULE_BUS_MSG {
	string moduleName;
	string eventName;
	string content; //json格式
	char* bin; //消息附带的二进制数据
};

class RPC_RESULT {
public:
	void setResult(string& resp) { textResult = resp; }
	void setResult(char* resp, int len) { binResult = new char[len]; memcpy(binResult, resp, len); iBinLen = len; }
	RPC_RESULT() {
		textResult = "";
		binResult = NULL;
		iBinLen = 0;
	}
	~RPC_RESULT()
	{
		if (binResult)
			delete binResult;
	}

	string textResult;
	char* binResult;
	int iBinLen;
};


typedef void (*fp_ioAddrRecv)(void* user, char* pData, int iLen);
typedef bool (*fp_rpcHandler)(string strReq, RPC_RESULT& resp, string& error);//返回是否处理
typedef void(*fp_msgSinker)(MODULE_BUS_MSG& msg);
typedef void (*fp_onVideoStreamRecv)(char* p, int len, STREAM_INFO si, void* user);
typedef void (*fp_procBeforeExit)();//由tds模块触发的程序退出，主程序退出前需要做的清理工作

namespace TDS_SESSION_TYPE {
	const string none = "none";
	const string rpc = "rpc";
	const string video = "video";
	const string web = "web";
	const string log = "log";
	const string commpkt = "commpkt"; //通信数据包监视
	const string tunnel = "tunnel"; //tunnel to serial ,tcpserver 
	const string websocket2com = "websocket2com";
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
	string uiTitle;
	bool fullscreen; //是否启用标题栏
	bool singleGenicamHost;
	vector<ACTIVE_TDS_SESSION> vecActiveSession;

	bool enableLog;
	bool enableDB;
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
	virtual bool Update(string tag, SYSTEMTIME stTime, string& sData) = 0;

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

	// tds客户端访问接口
	virtual bool call(string method, string param,string& result) = 0;//返回true，result为结果;返回false,result为错误信息
	virtual void rpcNotify(string method, string params = "", string sessionId = "") = 0;

	// tds服务功能扩展
	virtual void setRpcHandler(fp_rpcHandler handler) = 0;

	// io 通信服务功能
	virtual bool enableIoLog(string ioAddr, bool bEnable) = 0;
	virtual bool sendToIoAddr(string ioAddr, const char* p,int l) = 0;
	virtual bool connectDev(string ioAddr) = 0; 
	virtual bool isOnline(string ioAddr) = 0;
	virtual bool lockIoAddr(string ioAddr) = 0;
	virtual bool unlockIoAddr(string ioAddr) = 0;
	virtual bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback) = 0;

	//视频功能
	virtual void startStream(string streamId, STREAM_INFO* si=NULL) = 0;
	//推流到指定的streamId  streamId可以是tag,ioAddr,或者其他自定义名称
	virtual void pushStream(string streamId, char* pData, int len, STREAM_INFO* si=NULL) = 0;
	//从指定通道拉流（必须是支持视频功能的io地址）
	virtual void pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO*si = NULL) = 0;

	// 通用服务功能
	virtual void log(const char* text) = 0;

	// 消息总线。注册消息接收器
	virtual void registerMsgSinker(fp_msgSinker sinker) = 0;
	virtual void publishMsg(MODULE_BUS_MSG& msg) = 0;

	//数据接口
	iTDSConf* conf;
	i_database* db;

	//ui窗口
	HWND uiWnd;
	string uiWndTitle;
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