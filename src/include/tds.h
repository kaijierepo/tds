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
enum std::string			            
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
//#include "tds.h"
#include "json.hpp"
#include "yyjson.h"
#include <thread>
using namespace std;
using json = nlohmann::json;

std::string TDS_LAST_ERROR();


namespace str {
	//format
	std::string format(const char* pszFmt, ...);	

	//process
	std::string replace(std::string str, const std::string to_replaced, const std::string newchars);
	bool isDigits(char* pData, int len);
	bool isDigits(std::string s);
	std::string trimPrefix(std::string s, std::string prefix = " ");
	std::string trimSuffix(std::string s, std::string suffix = " ");
	std::string trim(std::string s, std::string toTrim = " ");
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);

	//char codec
	wstring gb_to_utf16(std::string instr);
	wstring utf8_to_utf16(std::string instr);
	wstring utf8_to_utf16(std::string instr);
	std::string utf16_to_utf8(wstring instr);
	wstring gb_to_utf16(std::string instr);
	std::string utf8_to_gb(std::string instr);
	std::string gb_to_utf8(std::string instr);
}

struct Date {
	unsigned short wYear;
	unsigned short wMonth;
	unsigned short wDay;
	unsigned short wDayOfWeek;
	Date() {
		memset(this, 0, sizeof(*this));
	}
	std::string toStr();
	void fromStr(std::string s);
};

struct HMS {
	unsigned short wHour;
	unsigned short wMinute;
	unsigned short wSecond;
	unsigned short wMilliseconds;
	HMS() {
		memset(this, 0, sizeof(*this));
	}
	bool operator==(HMS& right) {
		return 0 == memcmp(this, &right, sizeof(HMS));
	}

	bool operator>(HMS& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl > sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator>=(HMS& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl >= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<(HMS& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl < sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<=(HMS& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl <= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	void setNow();
	std::string toStr();
	void fromStr(std::string s);
};


struct TIME {
	unsigned short wYear;
	unsigned short wMonth;
	unsigned short wDay;
	unsigned short wHour;
	unsigned short wMinute;
	unsigned short wSecond;
	unsigned short wMilliseconds;
	unsigned short wDayOfWeek;

	TIME() {
		memset(this, 0, sizeof(*this));
	}

	void initAsInvalid() {
		memset(this, 0, sizeof(*this));
	}

	bool isValid() {
		if (wYear > 0)
			return true;
		return false;
	}

	void setDate(Date t);
	void setHMS(HMS t);
	HMS getHMS();
	void setNow();

	bool operator==(TIME& right) {
		return 0 == memcmp(this, &right, sizeof(TIME));
	}

	bool operator>(TIME& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl > sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator>=(TIME& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl >= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<(TIME& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl < sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<=(TIME& right) {
		std::string sl = toStr();
		std::string sr = right.toStr();
		if (sl <= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	std::string toStr(bool enableMilli = true);
	void fromStr(std::string s);
	std::string toDateStr();
	std::string toStampHMS();
	std::string toTimeStr();
	std::string toStampFull();
	time_t toUnixTime();
	void fromUnixTime(time_t t,int milli=0);

	static 	long long calcTimePassMilliSecond(TIME& lastTime)
	{
		TIME nowTime;
		nowTime.setNow();
		time_t now = nowTime.toUnixTime();
		time_t last = lastTime.toUnixTime();
		time_t second = now - last;
		time_t milli = nowTime.wMilliseconds - lastTime.wMilliseconds;
		milli = second * 1000 + milli;
		return milli;
	}

	static 	long long calcTimePassSecond(TIME& lastTime)
	{
		TIME nowTime;
		nowTime.setNow();
		time_t now = nowTime.toUnixTime();
		time_t last = lastTime.toUnixTime();
		time_t second = now - last;
		return second;
	}

	static void sleepMilli(int milliSec)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(milliSec));
	}

	static std::string nowStr(bool enableMS)
	{
		TIME t; 
		t.setNow();
		return t.toStr(enableMS);
	}
};


namespace TAG {
	std::string resolveTag(std::string strTagExp, std::string tagThis);
	std::string trimRoot(std::string tag, std::string root);
	std::string getParentTag(std::string tag); 
	std::string userTag2sysTag(std::string userTag, std::string userOrg);
	std::string sysTag2userTag(std::string sysTag, std::string userOrg);
	std::string addRoot(std::string tag, std::string root);

	size_t getMoLevel(std::string tag);
	std::string trimPrefix(std::string s, std::string prefix);
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);
}

namespace MO_TYPE {
	const std::string mo = "mo";
	const std::string customMo = "customMo";
	const std::string org = "org";
	const std::string customOrg = "customOrg";
	const std::string mp = "mp";
	const std::string mpgroup = "mpGroup";
};

namespace VAL_TYPE {
	const std::string json = "json";	
	const std::string Float = "float"; 
	const std::string integer = "int";
	const std::string boolean = "bool";
	const std::string video = "video";
	const std::string str = "std::string";
	const std::string car_strobe = "car_strobe";
	const std::string man_strobe = "man_strobe";
};
	

namespace CHAN_IO_TYPE {
	const std::string I = "i";
	const std::string O = "o";
	const std::string IO = "io";
};

namespace IO_TYPE {
	const std::string Input = "i";
	const std::string Output = "o";
	const std::string InAndOut = "io";
	const std::string Const = "c";
	const std::string InnerVar = "v";
}

namespace JSON_STR {
	const std::string Null = "null";
	const std::string True = "true";
	const std::string False = "false";
	inline bool is_bool(const std::string& s) {
		if (s == "true" || s == "false") { return true; }
		return false;
	}
	inline bool is_null(const std::string& s) {
		if (s == "null") { return true; }
		return false;
	}
	inline bool is_num(const std::string& s)
	{
		auto doc = yyjson_read(s.c_str(), s.size(), 0);
		if (!doc) return false;
		auto root = yyjson_doc_get_root(doc);
		if (!root) return false;
		bool bResult = yyjson_is_num(root);
		yyjson_doc_free(doc);
		return bResult;
	}
	inline bool is_str(const std::string& s)
	{
		auto doc = yyjson_read(s.c_str(), s.size(), 0);
		if (!doc) return false;
		auto root = yyjson_doc_get_root(doc);
		if (!root) return false;
		bool bResult = yyjson_is_str(root);
		yyjson_doc_free(doc);
		return bResult;
	}
	inline bool is_int(const std::string& s)
	{
		auto doc = yyjson_read(s.c_str(), s.size(), 0);
		if (!doc) return false;
		auto root = yyjson_doc_get_root(doc);
		if (!root) return false;
		bool bResult = yyjson_is_int(root);
		yyjson_doc_free(doc);
		return bResult;
	}
	
	inline bool get_bool(const std::string& s)
	{
		if (s == "true") return true;
		else if (s == "false") return false;
		else if (s == "1") return true;
		else if(s == "0") return false;
		else return false;
	}
	inline std::string getNull()
	{
		return "null";
	}
	inline double get_num(const std::string& s)
	{
		if (is_num(s))
		{
			auto doc = yyjson_read(s.c_str(), s.size(), 0);
			auto root = yyjson_doc_get_root(doc);
			double iResult = yyjson_get_num(root);
			yyjson_doc_free(doc);
			return iResult;
		}
		else
			return 0.0;
	}
	inline std::string get_str(const std::string& s)
	{
		if (is_str(s))
		{
			auto doc = yyjson_read(s.c_str(), s.size(), 0);
			auto root = yyjson_doc_get_root(doc);
			std::string iResult = yyjson_get_str(root);
			yyjson_doc_free(doc);
			return iResult;
		}
		else
			return "";
	}
	inline int get_int(const std::string& s)
	{	
		return atoi(s.c_str());		
	}
}


typedef void (*fp_ioAddrRecv)(void* user, char* pData, size_t iLen);
typedef bool (*fp_rpcHandler)(std::string strReq, RPC_RESP& resp, std::string& error);
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
	std::string mode;
	bool debugMode;
	std::string logLevel;
	bool enableGlobalAlarm;

	//security
	int tokenExpireTime;
	bool enableAccessCtrl;
	std::string testToken;
	
	//path conf
	std::string confPath;   //config data path
	std::string currentPath; //current data of path
	std::string dbPath;     //database folder path
	std::string uiPath;     //ui web files path
	std::string logPath;
	std::string fmsPath;  //file manage service root path
	std::string dbLanguage;

	//tds service conf
	int httpsPort;    //https port of tds;  default value 666; can be upgraded to websocket secure
	int httpPort;  //http port of tds;  default value 667; can be upgraded to websocket
	int httpsPort2;//default 0 not enable; another httpsport
	int httpPort2; //default 0 not enable; another httpport
	int fileUploadPort; // default 0 not enable;
	bool authDownload;
	int tcpKeepAliveDS;
	std::string mediaSrvIP;
	bool showObjOnline;

	//io service conf
	int tdspPort;  //tdsp protocol port of ioServer;  default value 665 
	std::vector<int> mbPort;    //modbus protocol port of ioServer; default value 664
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
	bool enableOfflineAlarm;

	//3rd party services integration
	std::string smsApiUser;
	std::string smsApiKey;
	std::string smsApiUrl;

	//desktop app mode conf
	bool bConcurrentGateway;
	std::string dataCenterIp;
	std::string title;
	std::string homepage;
	std::string uiMode;
	std::string uiTitle;
	bool fullscreen; 
	bool singleGenicamHost;
	std::vector<ACTIVE_TDS_SESSION> vecActiveSession;

	//module enable/disable
	bool enableLog;
	bool enableDB;
	bool enableScript;

	//tds edge conf
	bool edge; //tds edge gateway mode
	std::string cloudIP;
	int cloudPort;
	std::string deviceID;

	//debug
	bool bCreateDumpWhenLogError;
	bool bStopCycleAcq;
	bool bCallAsyn;

	//disk clean  remove the data which is out dataStorageMonths、mediaStorageMonths。
	std::string triggerStratgy = "period"; // LowLimit、 peroid
	int diskSpaceLeft=20; //unit GB
	int judgePeriod = 60;  //unit second
	int dataStorageMonths = 24;
	int mediaStorageMonths = 3;

	//memory clean
	//ini:  CleanMemoryInterval = xxxh  
	std::string strCleanMemoryInterval;

	//rpcSrcipt
	std::string m_apiAdaptorScript = "";
	std::vector<std::string> m_apiAdaptorMethod;

	//large Model
	int largeModelType = -1;

	LOG_ENABLE logEnable;

	virtual int getInt(std::string key, int iDef) = 0;
	virtual std::string getStr(std::string key, std::string sDef) = 0;
	virtual bool setStr(std::string key, std::string val) = 0;
	virtual bool setInt(std::string key, int val) = 0;

	virtual int getCurrentInt(std::string key, int iDef) = 0;
	virtual std::string getCurrentStr(std::string key, std::string sDef) = 0;
	virtual bool setCurrentStr(std::string key, std::string val) = 0;
	virtual bool setCurrentInt(std::string key, int val) = 0;
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


class i_xiaoT : public i_tdsPlugin {
public:
	virtual std::string getReply(std::string msg) = 0;
};

class i_gzhServer : public i_tdsPlugin {
public:
	virtual std::string getReply(std::string msg) = 0;
};

class i_smsServer : public i_tdsPlugin {
public:
	virtual bool send(std::string& msg,std::string& phoneNum) = 0;
	virtual bool sendVerificationCode(std::string phoneNum) = 0;
	virtual bool checkVerificationCode(std::string phoneNum, std::string code) = 0;
};


typedef void (*fp_toolRun)();


struct PLUGIN_INFO {
	std::string name;
};

//interface of TDS
class i_tds {
public:
	virtual std::string getVersion() = 0;
	virtual std::string getServerID() = 0;
	virtual std::string getSvnVersion() = 0;
	virtual bool setEncodeing(std::string encoding) = 0; // utf8 or gb2312
	virtual bool run(std::string cmdline = "") = 0;
	virtual void stop() = 0;
	virtual bool setProcBeforeExit(fp_procBeforeExit callback) = 0;
	fp_procBeforeExit m_fpProcBeforeExit;
	virtual void call(std::string method, json& param, json& err,json& rlt,RPC_SESSION session) = 0;
	virtual bool call(std::string method, std::string param, RPC_RESP& resp) = 0;
	virtual void callAsyn(std::string method, json& param, int delay = 0) = 0;
	virtual void callAsyn(std::string method, std::string& param,int delay = 0) = 0;
	virtual void batchCallAsyn(std::vector<json> calls, int delay = 0) = 0;
	virtual void rpcNotify(std::string method, std::string params = "", std::string sessionId = "") = 0;


	virtual void setRpcHandler(fp_rpcHandler handler) = 0;


	virtual bool enableIoLog(std::string ioAddr, bool bEnable) = 0;
	virtual bool sendToIoAddr(std::string ioAddr, const char* p,int l) = 0;
	virtual bool connectDev(std::string ioAddr) = 0; 
	virtual bool isOnline(std::string ioAddr) = 0;
	virtual bool isConnected(std::string ioAddr) = 0;
	virtual bool isInUse(std::string ioAddr) = 0;
	virtual bool lockIoAddr(std::string ioAddr) = 0;
	virtual bool unlockIoAddr(std::string ioAddr) = 0;
	virtual bool setIoAddrRecvCallback(std::string ioAddr, void* user, fp_ioAddrRecv recvCallback) = 0;

	//video function
#ifdef ENABLE_GENICAM
	virtual void startStream(std::string streamId, STREAM_INFO* si=NULL) = 0;
	//push to sepecified streamId 
	virtual void pushStream(std::string streamId, char* pData, int len, STREAM_INFO* si=NULL) = 0;
	virtual void pullStream(std::string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO*si = NULL) = 0;
#endif

	virtual void log(const char* text) = 0;

	iTDSConf* conf;
	i_xiaoT* xiaoT;
	i_gzhServer* gzhServer;
	i_smsServer* smsServer;
	i_tdsPlugin* shellServer;
	i_ioServer* ioServer;

	map<std::string,i_tdsPlugin*> plugins;

	void* uiWnd;
	std::string uiWndTitle;
	std::string m_sTitle;
	map<std::string, fp_toolRun> tools;
};

#ifdef TDSDLL
typedef i_tds* (*fp_getTds)();
i_tds* getITDS();
#endif

#ifdef TDS
extern i_tds* tds;
#endif
