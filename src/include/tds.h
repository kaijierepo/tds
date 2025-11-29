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
#include "tds.h"
#include "json.hpp"
#include "yyjson.h"
#include <thread>
using namespace std;
using json = nlohmann::json;

string TDS_LAST_ERROR();


namespace str {
	//format
	std::string format(const char* pszFmt, ...);	

	//process
	string replace(string str, const string to_replaced, const string newchars);
	bool isDigits(char* pData, int len);
	bool isDigits(string s);
	string trimPrefix(string s, string prefix = " ");
	string trimSuffix(string s, string suffix = " ");
	string trim(std::string s, string toTrim = " ");
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);

	//char codec
	wstring gb_to_utf16(string instr);
	wstring utf8_to_utf16(string instr);
	wstring utf8_to_utf16(string instr);
	string utf16_to_utf8(wstring instr);
	wstring gb_to_utf16(string instr);
	string utf8_to_gb(string instr);
	string gb_to_utf8(string instr);
}

struct Date {
	unsigned short wYear;
	unsigned short wMonth;
	unsigned short wDay;
	unsigned short wDayOfWeek;
	Date() {
		memset(this, 0, sizeof(*this));
	}
	string toStr();
	void fromStr(string s);
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
		string sl = toStr();
		string sr = right.toStr();
		if (sl > sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator>=(HMS& right) {
		string sl = toStr();
		string sr = right.toStr();
		if (sl >= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<(HMS& right) {
		string sl = toStr();
		string sr = right.toStr();
		if (sl < sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<=(HMS& right) {
		string sl = toStr();
		string sr = right.toStr();
		if (sl <= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	void setNow();
	string toStr();
	void fromStr(string s);
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
		string sl = toStr();
		string sr = right.toStr();
		if (sl > sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator>=(TIME& right) {
		string sl = toStr();
		string sr = right.toStr();
		if (sl >= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<(TIME& right) {
		string sl = toStr();
		string sr = right.toStr();
		if (sl < sr) {
			return true;
		}
		else {
			return false;
		}
	}
	bool operator<=(TIME& right) {
		string sl = toStr();
		string sr = right.toStr();
		if (sl <= sr) {
			return true;
		}
		else {
			return false;
		}
	}
	string toStr(bool enableMilli = true);
	void fromStr(string s);
	string toDateStr();
	string toStampHMS();
	string toTimeStr();
	string toStampFull();
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

	static string nowStr(bool enableMS)
	{
		TIME t; 
		t.setNow();
		return t.toStr(enableMS);
	}
};


namespace TAG {
	string resolveTag(string strTagExp, string tagThis);
	string trimRoot(string tag, string root);
	string getParentTag(string tag); 
	string userTag2sysTag(string userTag, string userOrg);
	string sysTag2userTag(string sysTag, string userOrg);
	string addRoot(string tag, string root);

	size_t getMoLevel(string tag);
	string trimPrefix(string s, string prefix);
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);
}

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
	

namespace CHAN_IO_TYPE {
	const string I = "i";
	const string O = "o";
	const string IO = "io";
};

namespace IO_TYPE {
	const string Input = "i";
	const string Output = "o";
	const string InAndOut = "io";
	const string Const = "c";
	const string InnerVar = "v";
}

namespace JSON_STR {
	const string Null = "null";
	const string True = "true";
	const string False = "false";
	inline bool is_bool(const string& s) {
		if (s == "true" || s == "false") { return true; }
		return false;
	}
	inline bool is_null(const string& s) {
		if (s == "null") { return true; }
		return false;
	}
	inline bool is_num(const string& s)
	{
		auto doc = yyjson_read(s.c_str(), s.size(), 0);
		if (!doc) return false;
		auto root = yyjson_doc_get_root(doc);
		if (!root) return false;
		bool bResult = yyjson_is_num(root);
		yyjson_doc_free(doc);
		return bResult;
	}
	inline bool is_str(const string& s)
	{
		auto doc = yyjson_read(s.c_str(), s.size(), 0);
		if (!doc) return false;
		auto root = yyjson_doc_get_root(doc);
		if (!root) return false;
		bool bResult = yyjson_is_str(root);
		yyjson_doc_free(doc);
		return bResult;
	}
	inline bool is_int(const string& s)
	{
		auto doc = yyjson_read(s.c_str(), s.size(), 0);
		if (!doc) return false;
		auto root = yyjson_doc_get_root(doc);
		if (!root) return false;
		bool bResult = yyjson_is_int(root);
		yyjson_doc_free(doc);
		return bResult;
	}
	
	inline bool get_bool(const string& s)
	{
		if (s == "true") return true;
		else if (s == "false") return false;
		else if (s == "1") return true;
		else if(s == "0") return false;
		else return false;
	}
	inline string getNull()
	{
		return "null";
	}
	inline double get_num(const string& s)
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
	inline string get_str(const string& s)
	{
		if (is_str(s))
		{
			auto doc = yyjson_read(s.c_str(), s.size(), 0);
			auto root = yyjson_doc_get_root(doc);
			string iResult = yyjson_get_str(root);
			yyjson_doc_free(doc);
			return iResult;
		}
		else
			return "";
	}
	inline int get_int(const string& s)
	{	
		return atoi(s.c_str());		
	}
}


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
	bool enableOfflineAlarm;

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

	//rpcSrcipt
	string m_apiAdaptorScript = "";
	vector<string> m_apiAdaptorMethod;

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
