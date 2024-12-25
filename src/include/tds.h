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
#include "tdsRPC.h"
#include <string>
#include <vector>
#include <map>
#include "json.hpp"
#include "tds.h"
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
	string trimPrefix(string s, string prefix);
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);
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
	else if (valType == "v") return "内部变量";
	else if (valType == "c") return "常量";
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

typedef void (*fp_ioAddrRecv)(void* user, char* pData, size_t iLen);
typedef bool (*fp_rpcHandler)(string strReq, RPC_RESP& resp, string& error);
typedef void (*fp_procBeforeExit)();


//custom define which kind of message to log
struct LOG_ENABLE {
	bool innerRPCCall;
	bool scriptEngine;
	LOG_ENABLE() {
		innerRPCCall = false;
		scriptEngine = false;
	}
};

struct iTDSConf {
	virtual void loadConf() = 0;
	virtual void loadCurrentData() = 0;
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
	string currentPath; //current data of path
	string dbPath;     //database folder path
	string uiPath;     //ui web files path
	string logPath;
	string fmsPath;  //file manage service root path
	string dbLanguage;

	//tds service conf
	int httpsPort;    //https port of tds;  default value 666; can be upgraded to websocket secure
	int httpPort;  //http port of tds;  default value 667; can be upgraded to websocket
	int httpsPort2;//default 0 not enable; another httpsport
	int httpPort2; //default 0 not enable; another httpport
	int fileUploadPort; // default 0 not enable;
	bool authDownload;
	int tcpKeepAliveDS;
	string mediaSrvIP;
	bool showObjOnline;

	//io service conf
	int tdspPort;  //tdsp protocol port of ioServer;  default value 665 
	vector<int> mbPort;    //modbus protocol port of ioServer; default value 664
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
	bool bCallAsyn;

	//disk clean  remove the data which is out dataStorageMonths、mediaStorageMonths。
	string triggerStratgy = "period"; // LowLimit、 peroid
	int diskSpaceLeft=20; //unit GB
	int judgePeriod = 60;  //unit second
	int dataStorageMonths = 24;
	int mediaStorageMonths = 3;

	//memory clean
	//ini:  CleanMemoryInterval = xxxh  
	string strCleanMemoryInterval;

	LOG_ENABLE logEnable;

	virtual int getInt(string key, int iDef) = 0;
	virtual string getStr(string key, string sDef) = 0;
	virtual bool setStr(string key, string val) = 0;
	virtual bool setInt(string key, int val) = 0;

	virtual int getCurrentInt(string key, int iDef) = 0;
	virtual string getCurrentStr(string key, string sDef) = 0;
	virtual bool setCurrentStr(string key, string val) = 0;
	virtual bool setCurrentInt(string key, int val) = 0;
};

class i_tdsPlugin {
public:
	virtual bool init() = 0;
	virtual bool run() = 0;
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
	virtual string getSvnVersion() = 0;
	virtual bool setEncodeing(string encoding) = 0; // utf8 or gb2312
	virtual bool run(string cmdline = "") = 0;
	virtual void stop() = 0;
	virtual bool setProcBeforeExit(fp_procBeforeExit callback) = 0;
	fp_procBeforeExit m_fpProcBeforeExit;
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

	iTDSConf* conf;
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
