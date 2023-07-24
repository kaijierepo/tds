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

/*TDS Coding Standard
> name pattern
functions variable
use camel mode, like  getIODevices
enum string			            
short dash,like gw-local-serial

>use double instead of float anywhere, 
cause json.hpp uses double,if float is used,it will cause loss of precision when format to json

>use unsigned char* instead of char* when pointed to device protocol buffer
most of time you want to get value 0-255 when you use expression int a = p[i]
it will be easier to deal with a Hex packet in debuging or coding when you think a 0-255 value instead of a -127 or 127value
in most protocol specificatin,0-255 will be used to define a value of one byte
*/


#pragma once
#include <string>
#include <vector>
#include <map>
#include "json.hpp"
using namespace std;
using json = nlohmann::json;	

namespace TAG {
	string resolveTag(string strTagExp, string tagThis);
	string trimRoot(string tag, string root);
	string getParentTag(string tag); //获取当前位号的父节点的位号
	string userTag2sysTag(string userTag, string userOrg);
	string sysTag2userTag(string sysTag, string userOrg);
	string addRoot(string tag, string root);

	bool hasTag(json& tree, string tag); 
	size_t getMoLevel(string tag);
	json mapTree2List(json mapTree);
}

struct TIME;


namespace MO_TYPE {
	const string mo = "mo";
	const string customMo = "customMo";
	const string org = "org";
	const string customOrg = "customOrg";
	const string mp = "mp";
	const string mpgroup = "mpGroup";
};

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
	
inline string getValTypeLabel(string valType)
{
	if (valType == "json") return "JSON";
	else if (valType == "float") return "浮点型";
	else if (valType == "int") return "整型";
	else if (valType == "bool") return "布尔型";
	else if (valType == "video") return "视频";
	else if (valType == "string") return "字符串型";
	else return "未知值类型";
};

namespace CHAN_IO_TYPE {
	const string I = "i";
	const string O = "o";
	const string IO = "io";
};

inline string getIOTypeLabel(string valType)
{
	if (valType == "i") return "输入";
	else if (valType == "o") return "输出";
	else if (valType == "io") return "输入/输出";
	else return "未知IO类型";
};



//应用层协议类型
namespace APP_LAYER_PROTO {
	const string UNKNOWN = "unknown";
	const string HTTP = "http";
	const string PROTOCOL_WEBSOCKET = "websocket";
	const string PROTOCOL_FRAMING_PROTOCOL = "alp_framing_protocol";
	const string tdsHMR = "tdsHMR";  //tds web hot module replacement
	const string terminalPrompt = "->";  //以 -> 结尾的字符串 
	const string textEnd1LF = "textEnd1LF";
	const string textEnd2LF = "textEnd2LF";
};

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


//设备管理状态
namespace DEV_DISPOSITION_MODE {
	const string managed = "managed"; //后续重构为 inService 表示启用
	const string spare = "spare";
};


struct MODULE_BUS_MSG {
	string moduleName;
	string eventName;
	string content; //json
	char* bin; 
};

//stateless rpc session
class RPC_SESSION {
public:
	string req;

	//authentification
	string name; //name is defined by tds client
	string user;
	string token;
	string method;

	//tag expression in current user; multi-tenant
	//rootTag = org + queryRootTag
	//sysTag = org + queryRootTag + tag used in this session   rootTag = org + queryRootTag;
	string org; //user's org


	bool isNotification; 

	//session params for rpc route
	string route_ioAddr;  //route to io device
	string route_tag;     //route to io device or childTds
	string route_childTds;

	//ip params
	string remoteAddr;
	string remoteIP;
	int remotePort;
	string localIP;
	int localPort;
	bool isHttps;

	string sLastRecvTime;
	string lastMethodCalled;

	bool isDebug; //调试调用不计入session统计

	RPC_SESSION() {
		isNotification = false;
		remotePort = 0;
		localPort = 0;
		isDebug = false;
	}
};

#define RPC_OK "\"ok\""
#define RPC_TIMEOUT "\"timeout\""
#define RPC_FAIL "\"fail\""

class RPC_RESP {
public:
	void setResult(string& str) { result = str; }
	void setResult(char* bin, int len) { binResult = new char[len]; memcpy(binResult, bin, len); iBinLen = len; }
	RPC_RESP() {
		result = "";
		binResult = NULL;
		iBinLen = 0;
		isNotification = false;
	}
	~RPC_RESP()
	{
		if (binResult)
			delete binResult;
	}

	string strResp; 
	string strRespForLog; //ignore some pkt data ,for log only
	string error;
	string result;
	string params; 
	string info;   //rpc excution log
	string dbQueryInfo;
	char* binResult;
	int iBinLen;
	bool isNotification; //is request a notification.no response will send if request is a notification
};


typedef void (*fp_ioAddrRecv)(void* user, char* pData, size_t iLen);
typedef void (*fp_createLicence)();
typedef bool (*fp_rpcHandler)(string strReq, RPC_RESP& resp, string& error);
typedef void(*fp_msgSinker)(MODULE_BUS_MSG& msg);
#ifdef ENABLE_GENICAM
typedef void (*fp_onVideoStreamRecv)(char* p, int len, STREAM_INFO si, void* user);
#endif
typedef void (*fp_procBeforeExit)();

namespace TDS_SESSION_TYPE {
	const string none = "none";

	//client connections
	const string tdsClient = "tdsClient";  
	const string video = "video";
	const string dataStream = "dataStream";
	const string iodev = "ioDev"; 
	const string webHMR = "webHMR"; //web hot module replacement

	//bridge data interfaces
	const string bridgeToLocalCom = "bridgeToLocalCom";
	const string bridgeToiodev = "bridgeToiodev";
	const string bridgeToTcpClient = "bridgeToTcpClient";

	//debug tools
	const string terminal = "terminal";
	const string log = "log";
	const string apipkt = "apipkt"; 
	const string iopkt = "sessionPkt";
}

struct ACTIVE_TDS_SESSION {
	string ip;
	int port;
	string type;
};

struct iTDSConf {
	virtual void loadConf() = 0;
	//software conf
	string mode;
	bool debugMode;
	string logLevel;
	bool enableGlobalAlarm;

	//security
	int tokenExpireTime;
	bool enableAccessCtrl;
	string testToken;
	
	//path conf
	string confPath;   //config data path
	string dbPath;     //database folder path
	string uiPath;     //ui web files path

	//tds service conf
	int httpsPort;    //https port of tds;  default value 666; can be upgraded to websocket secure
	int httpPort;  //http port of tds;  default value 667; can be upgraded to websocket
	int httpsPort2;//default 0 not enable; another httpsport
	int httpPort2; //default 0 not enable; another httpport
	bool authDownload;
	int tcpKeepAliveDS;

	//io service conf
	int tdspPort;  //tdsp protocol port of ioServer;  default value 665 
	int mbPort;    //modbus protocol port of ioServer; default value 664
	int iq60Port;  //iq60 protocol port of ioServer; default value 663
	int tcpKeepAliveIO;
	int iotimeoutTdsp; //tdsp comm timeout in milliseconds
	int iotimeoutModbusRtu;
	int iotimeoutIQ60;
	int iotimeoutDLT645;
	bool enableDevCommReboot;
	bool enableDevReboot;
	int devRebootTime; //seconds
	int devCommRebootTime;
	bool tdspSingleTransaction;

	//3rd party services integration
	string smsApiUser;
	string smsApiKey;
	string smsApiUrl;

	//desktop app mode conf
	bool bConcurrentGateway;
	string dataCenterIp;
	string title;
	string homepage;
	string uiMode;
	string uiTitle;
	bool fullscreen; 
	bool singleGenicamHost;
	vector<ACTIVE_TDS_SESSION> vecActiveSession;

	//module enable/disable
	bool enableLog;
	bool enableDB;
	bool enableScript;

	//tds edge conf
	bool edge; //tds edge gateway mode
	string cloudIP;
	int cloudPort;
	string deviceID;

	//debug
	bool bCreateDumpWhenLogError;
	bool bStopCycleAcq;

	virtual int getInt(string key, int iDef) = 0;
	virtual string getStr(string key, string sDef) = 0;
	virtual bool setStr(string key, string val) = 0;
	virtual bool setInt(string key, string val) = 0;
};


enum RPC_ERROR_CODE {
	//json rpc standard
	TDS_ERROR_CODE = -32603,

	//common
	TEC_FAIL = -40000,
	TEC_InvalidReqFmt = -40001,
	TEC_WrongParamFmt = -40002,
	TEC_TIME_SELECTOR_FMT_ERROR = -40003,
	TEC_TAG_SELECTOR_FMT_ERROR = -40004,
	TEC_paramMissing = -40021,

	//user
	AUTH_tokenError = -40101,
	AUTH_tokenMissing = -40102,
	AUTH_userNotFound = -40103,
	AUTH_userMissing = -40104,
	AUTH_passwordError = -40105,
	AUTH_signatureInvalid = -40106,
	AUTH_signatureMissing = -40107,
	AUTH_noPermission = -40108,
	AUTH_noWritePermission = -40109,

	//mo
	MO_specifiedTagNotFound = -40201,
	MO_outputFail = -40202,
	MO_outputValNotSpecified = -40203,
	MO_outputValShouldBeBool = -40204,
	MO_outputValShouldBeNumber = -40205,
	MO_currentValIsNull = -40206,
	MO_outputTimeout = -40207,
	TEC_VAL_TYPE_ERROR = -40208,
	OBJ_templateNotFound = -40209,
	OBJ_enumValNotFound = -40210,
	OBJ_specifiedObjIDNotFound = -40211,

	//io
	IO_devNotFound = -40301,
	IO_devOffline = -40302,
	IO_reqTimeout = -40303,
	IO_devTypeError = -40304,
	IO_ioAddrNotSpecified = -40305,
	IO_chanTemplateNotFound = -40306,
	IO_devBusy = -40307,
	IO_devStopped = -40308,
	IO_devAddrFmtError = -40309,

	//video
	TEC_VIDEO_PARAM_NOT_VALID = -40401,
	TEC_NO_STREAM_SRC = -40402,
	TEC_STREAM_ID_NOT_FOUND = -40403,

	//tdsp
	DEV_confNameNotFound = -42001,
	DEV_confCategoryNotFound = -42002,
	DEV_chanNotFound = -42003,

	//os
	OS_fileNotExist = -43001,

	//alarm
	ALM_alarmEventNotFound = -44001
};

//code ,msg is specified by JSON RPC stardard. desc is for detail description by TDS.can be Chinese Charactors
inline string makeRPCError(int code, string msg,string desc = "")
{
	string error = "{\"code\":" + std::to_string(code) + ",\"message\":\"" + msg + "\"";
	if (desc != "")
	{
		string data = ",\"data\":{\"desc\":\"" + desc + "\"}";
		error += data;
	}
	error += "}";

	return error;
}

class i_tdsPlugin {
public:
	virtual bool init() = 0;
	virtual bool run() = 0;
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
	virtual bool Update(string tag, TIME stTime, string& sData) = 0;


	
	//deFileUrl  1.localfile 2.localfolder 3.http url
	virtual void saveDEFile(string strTag, TIME stTime, string deFileUrl) = 0;

	//get db.json path
	virtual string getPath_dbFile(string strTag, TIME date,string deType="") = 0;
	//de folder path
	virtual string getPath_dataFolder(string strTag, TIME date) = 0;
	virtual string getPath_deFile(string strTag, TIME stTime) = 0;
	virtual string getPath_dbRoot() = 0;
};

class i_ioServer {
public:
	virtual size_t getBindedChanCount() = 0;
};

class i_rpcServer {
public:
	virtual void setLicenceStatus(json j) = 0;
};

class i_xiaoT : public i_tdsPlugin {
public:
	virtual std::string getReply(string msg) = 0;
};

class i_gzhServer : public i_tdsPlugin {
public:
	virtual std::string getReply(string msg) = 0;
};

class i_smsServer : public i_tdsPlugin {
public:
	virtual bool send(string& msg,string& phoneNum) = 0;
	virtual bool send(json& params, string& phoneNum) = 0;
	virtual bool sendVerificationCode(string phoneNum) = 0;
	virtual bool checkVerificationCode(string phoneNum, string code) = 0;
};


typedef void (*fp_toolRun)();


struct PLUGIN_INFO {
	string name;
};

//interface of TDS
class i_tds {
public:
	virtual string getVersion() = 0;
	virtual bool setEncodeing(string encoding) = 0; // utf8 or gb2312
	virtual bool run(string cmdline = "") = 0;
	virtual void stop() = 0;
	virtual bool setProcBeforeExit(fp_procBeforeExit callback) = 0;
	fp_procBeforeExit m_fpProcBeforeExit;
	fp_createLicence createLicence;
	virtual void call(string method, json& param, json& err,json& rlt,RPC_SESSION session) = 0;
	virtual bool call(string method, string param, RPC_RESP& resp) = 0;
	virtual void callAsyn(string method, json& param, int delay = 0) = 0;
	virtual void callAsyn(string method, string& param,int delay = 0) = 0;
	virtual void batchCallAsyn(vector<json> calls, int delay = 0) = 0;
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
#ifdef ENABLE_GENICAM
	virtual void startStream(string streamId, STREAM_INFO* si=NULL) = 0;
	//push to sepecified streamId 
	virtual void pushStream(string streamId, char* pData, int len, STREAM_INFO* si=NULL) = 0;
	virtual void pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO*si = NULL) = 0;
#endif

	virtual void log(const char* text) = 0;

	//event bus
	virtual void registerMsgSinker(fp_msgSinker sinker) = 0;
	virtual void publishMsg(MODULE_BUS_MSG& msg) = 0;

	
	iTDSConf* conf;
	i_database* db;
	i_xiaoT* xiaoT;
	i_gzhServer* gzhServer;
	i_smsServer* smsServer;
	i_tdsPlugin* shellServer;
	i_ioServer* ioServer;
	i_rpcServer* rpcServer;

	map<string,i_tdsPlugin*> plugins;

	void* uiWnd;
	string uiWndTitle;
	string m_sTitle;
	map<string, fp_toolRun> tools;
};

#ifdef TDSDLL
typedef i_tds* (*fp_getTds)();
i_tds* getITDS();
#endif

#ifdef TDS
extern i_tds* tds;
#endif
