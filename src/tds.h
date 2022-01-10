/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu  

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/




#pragma once
#include <string>
#include <vector>
#include <map>
#include <Windows.h>
using namespace std;



namespace TDS {
	namespace VAL_TYPE {
		const string json = "json";	
		const string Float = "float"; 
		const string integer = "int";
		const string boolean = "bool";
		const string video = "video";
		const string str = "string";
		const string car_strobe = "car_strobe";
		const string man_strobe = "man_strobe";
	};
	
	namespace IO_DEV_LEVEL {
		const string server = "server";
		const string gateway = "gateway";
		const string device = "device";
		const string channel = "channel";
	}
	
	namespace IO_DEV_TYPE {
		namespace DEV {
			const string tdsp_device = "tdsp-device";
			const string modbus_rtu_slave = "modbus-rtu-slave";
			const string iq60_gateway = "iq60-gateway";
			const string genicam = "genicam";
			const string mqttBroker = "mqtt-broker";
		}
		namespace GW {
			const string local_serial = "local-serial";
			const string can_gateway = "can-gateway";
			const string rs485_gateway = "rs485-gateway";
			const string tuya_iot_project = "tuya-iot-project";
		}
		namespace CHAN {
			const string io_channel = "io-channel";
		}
		namespace SERVER {
			const string tds = "tds";
		}
	}
};


namespace STORAGE_FMT {
	const string Int16 = "Int16";
	const string UInt16 = "UInt16";
	const string Int32 = "Int32";
	const string UInt32 = "UInt32";
	const string Int64 = "Int64";
	const string Uint64 = "UInt64";
	const string Float = "Float";
	const string Double = "Double";
	const string BCD16 = "BCD16";
	const string BCD32 = "BCD32";
}

inline int storageSize(string fmt) {
	if (fmt.find("16") != string::npos)return 2;
	else if (fmt.find("32") != string::npos)return 4;
	else if (fmt.find("64") != string::npos)return 8;
	else if (fmt == "Float")return 4;
	else if (fmt == "Double")return 8;
}

#define STREAM_TYPE_ENUM string
namespace STREAM_TYPE {
	const string bmp = "bmp"; 
	const string h264 = "h264"; 
	const string rgba = "rgba"; 
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


struct MODULE_BUS_MSG {
	string moduleName;
	string eventName;
	string content; //json
	char* bin; 
};


class RPC_RESP {
public:
	void setResult(string& str) { result = str; }
	void setResult(char* bin, int len) { binResult = new char[len]; memcpy(binResult, bin, len); iBinLen = len; }
	RPC_RESP() {
		result = "";
		binResult = NULL;
		iBinLen = 0;
	}
	~RPC_RESP()
	{
		if (binResult)
			delete binResult;
	}

	string strResp; 
	string error;
	string result;
	string params; 
	char* binResult;
	int iBinLen;
};


typedef void (*fp_ioAddrRecv)(void* user, char* pData, int iLen);
typedef bool (*fp_rpcHandler)(string strReq, RPC_RESP& resp, string& error);
typedef void(*fp_msgSinker)(MODULE_BUS_MSG& msg);
typedef void (*fp_onVideoStreamRecv)(char* p, int len, STREAM_INFO si, void* user);
typedef void (*fp_procBeforeExit)();

namespace TDS_SESSION_TYPE {
	const string none = "none";
	const string tdsClient = "tdsClient";  
	const string video = "video";
	const string iodev = "ioDev"; 

	const string tunnel = "tunnel"; 
	const string websocket2com = "websocket2com";
	const string bridgeToiodev = "bridgeToiodev";
	const string bridgeToTcpClient = "bridgeToTcpClient";
	const string terminal = "terminal";

	const string log = "log";
	const string commpkt = "commpkt"; 
	const string sessionPkt = "sessionPkt";
}

struct ACTIVE_TDS_SESSION {
	string ip;
	int port;
	string type;
};


struct iTDSConf {
	int port;
	int httpPort;
	int ioServerPort;
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
	bool fullscreen; 
	bool singleGenicamHost;
	vector<ACTIVE_TDS_SESSION> vecActiveSession;

	bool enableLog;
	bool enableDB;
	bool enableAccessCtrl;
	bool enableScript;
	bool authDownload; 
};




//interface of tds.db
//key is timestamp as 2020-01-01 11:11:11,value is a json string of one data element
#define DB_DATA_SET std::map<string,string>
class i_database {
public:
	//crud options
	//virtual void INSERT(string strTag, SYSTEMTIME stTime, json& jData, json dataFile = nullptr) = 0;
	//time: "2020-02-14~2020-02-15" or "1d1h1m30s"
	//filter: "humidiy==55 && temperature>30"
	//dataSet is json de array
	//virtual bool SELECT(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result) = 0;
	virtual bool Update(string tag, SYSTEMTIME stTime, string& sData) = 0;


	
	//deFileUrl  1.localfile 2.localfolder 3.http url
	virtual void saveDEFile(string strTag, SYSTEMTIME stTime, string deFileUrl) = 0;

	//get db.json path
	virtual string getPath_dbFile(string strTag, SYSTEMTIME date) = 0;
	//de folder path
	virtual string getPath_dataFolder(string strTag, SYSTEMTIME date) = 0;
	virtual string getPath_deFile(string strTag, SYSTEMTIME stTime) = 0;
	virtual string getPath_dbRoot() = 0;
};



//interface of TDS
class iTDS {
public:
	virtual string getVer() = 0;
	virtual bool setEncodeing(string encoding) = 0; // utf8 or gb2312
	virtual bool run(string cmdline = "") = 0;
	virtual bool setProcBeforeExit(fp_procBeforeExit callback) = 0;
	fp_procBeforeExit m_fpProcBeforeExit;

	virtual bool call(string method, string param, RPC_RESP& resp) = 0;
	virtual void rpcNotify(string method, string params = "", string sessionId = "") = 0;


	virtual void setRpcHandler(fp_rpcHandler handler) = 0;


	virtual bool enableIoLog(string ioAddr, bool bEnable) = 0;
	virtual bool sendToIoAddr(string ioAddr, const char* p,int l) = 0;
	virtual bool connectDev(string ioAddr) = 0; 
	virtual bool isOnline(string ioAddr) = 0;
	virtual bool isConnected(string ioAddr) = 0;
	virtual bool isInUse(string ioAddr) = 0;
	virtual bool lockIoAddr(string ioAddr) = 0;
	virtual bool unlockIoAddr(string ioAddr) = 0;
	virtual bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback) = 0;

	//video function
	virtual void startStream(string streamId, STREAM_INFO* si=NULL) = 0;
	//push to sepecified streamId 
	virtual void pushStream(string streamId, char* pData, int len, STREAM_INFO* si=NULL) = 0;
	virtual void pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO*si = NULL) = 0;


	virtual void log(const char* text) = 0;

	//event bus
	virtual void registerMsgSinker(fp_msgSinker sinker) = 0;
	virtual void publishMsg(MODULE_BUS_MSG& msg) = 0;

	
	iTDSConf* conf;
	i_database* db;

	SYSTEMTIME stStartupTime;

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