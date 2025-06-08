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
using namespace std;


namespace METHOD_CALLER {
	const string tdspDev = "tdspDev";
	const string script = "script";
	const string httpClient = "httpClient";
}

//stateless rpc session
class RPC_SESSION {
public:
	string req;   //maybe batch call
	string req_single;  //single call

	//authentification
	string name; //name is defined by tds client
	string user;
	string role;
	string pwd;
	string token;
	string method;
	string dbpath;
	string language;

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
	string sLastSendTime;
	string lastMethodNotified;
	string caller;

	bool isDebug; //调试调用不计入session统计

	long long tStartCall;
	long long tStartHandle;
	long long tEndCall;

	RPC_SESSION() {
		isNotification = false;
		remotePort = 0;
		localPort = 0;
		isDebug = false;
		caller = METHOD_CALLER::httpClient;
		tStartCall = 0;
		tStartHandle = 0;
		tEndCall = 0;
	}
};

#define RPC_NULL "null"
#define RPC_OK "\"ok\""
#define RPC_TIMEOUT "\"timeout\""
#define RPC_FAIL "\"fail\""
#define RPC_STR(s) "\""+s+"\""

class RPC_RESP {
public:
	void setResult(string& str) { result = str; }
	RPC_RESP() {
		result = "";
		isNotification = false;
	}
	~RPC_RESP()
	{
	}

	string strResp; 
	string strRespForLog; //ignore some pkt data ,for log only
	string error;
	string result;
	string params; 
	string info;   //rpc excution log
	string dbQueryInfo;
	bool isNotification; //is request a notification.no response will send if request is a notification
};


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
	const string bridgeToTcpServer = "bridgeToTcpServer";

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
	AUTH_userExisted = -40105,
	AUTH_passwordError = -40106,
	AUTH_signatureInvalid = -40107,
	AUTH_signatureMissing = -40108,
	AUTH_noPermission = -40109,
	AUTH_noObjTreePermission = -40110,
	AUTH_noWritePermission = -40111,

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