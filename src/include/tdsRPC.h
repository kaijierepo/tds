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
#include <string>
#include <unordered_set>
#include <vector>
using namespace std;


namespace METHOD_CALLER {
	const std::string tdspDev = "tdspDev";
	const std::string script = "script";
	const std::string httpClient = "httpClient";
}

//stateless rpc session
class RPC_SESSION {
public:
	std::string req;   //maybe batch call
	std::string req_single;  //single call

	//authentification
	std::string name; //name is defined by tds client
	std::string user;
	std::string role;
	std::string pwd;
	std::string token;
	std::string method;
	std::string dbpath;
	std::string language;

	//tag expression in current user; multi-tenant
	//rootTag = org + queryRootTag
	//sysTag = org + queryRootTag + tag used in this session   rootTag = org + queryRootTag;
	std::string org; //user's org


	static bool defaultSubAll;


	bool isNotification; 
	bool subAllMethod;
	bool subAllTag;
	std::vector<std::string> subMethod;
	std::vector<std::string> subRootTag;
	std::vector<std::string> subTag;

	//session params for rpc route
	std::string route_ioAddr;  //route to io device
	std::string route_tag;     //route to io device or childTds
	std::string route_childTds;

	//ip params
	std::string remoteAddr;
	std::string remoteIP;
	int remotePort;
	std::string localIP;
	int localPort;
	bool isHttps;

	std::string sLastRecvTime;
	std::string lastMethodCalled;
	std::string sLastSendTime;
	std::string lastMethodNotified;
	std::string caller;

	bool isDebug; //调试调用不计入session统计

	long long tStartCall;
	long long tStartHandle;
	long long tEndCall;

	bool isSubscribed(const std::string& method, const std::string& tag) {
		bool subByMethod = false;
		if (subAllMethod) {
			subByMethod = true;
		}
		else {
			for (auto& i : subMethod) {
				if (i == method) {
					subByMethod = true;
					break;
				}
			}
		}

		if (!subByMethod) {
			return false;
		}

		if (tag == "") { //无位号属性
			return true;
		}

		bool subByTag = false;
		if (subAllTag) {
			subByTag = true;
		}
		else {
			for (const std::string& rootTag : subRootTag) {
				if (tag.find(rootTag) == 0) {
					subByTag = true;
				}
			}
		}
				
		if (subByMethod && subByTag) {
			return true;
		}

		return false;
	}

	RPC_SESSION() {
		isNotification = false;
		remotePort = 0;
		localPort = 0;
		isDebug = false;
		caller = METHOD_CALLER::httpClient;
		tStartCall = 0;
		tStartHandle = 0;
		tEndCall = 0;
		if (RPC_SESSION::defaultSubAll) {
			subAllMethod = true;
			subAllTag = true;
		}
		else {
			subAllMethod = false;
			subAllTag = false;
		}
	}
};

#define RPC_NULL "null"
#define RPC_OK "\"ok\""
#define RPC_TIMEOUT "\"timeout\""
#define RPC_FAIL "\"fail\""
#define RPC_PARAM_MISSING "\"param missing\""
#define RPC_STR(s) "\""+s+"\""

class RPC_RESP {
public:
	void setResult(std::string& str) { result = str; }
	RPC_RESP() {
		result = "";
		isNotification = false;
		timeCost = 0;
	}
	~RPC_RESP()
	{
	}

	std::string strResp; 
	std::string strRespForLog; //ignore some pkt data ,for log only
	std::string error;
	std::string result;
	std::string params; 
	std::string info;   //rpc excution log
	std::string dbQueryInfo;
	int timeCost;
	bool isNotification; //is request a notification.no response will send if request is a notification
};


namespace TDS_SESSION_TYPE {
	const std::string none = "none";

	//client connections
	const std::string tdsClient = "tdsClient";  
	const std::string video = "video";
	const std::string dataStream = "dataStream";
	const std::string iodev = "ioDev"; 
	const std::string webHMR = "webHMR"; //web hot module replacement

	//bridge data interfaces
	const std::string bridgeToLocalCom = "bridgeToLocalCom";
	const std::string bridgeToiodev = "bridgeToiodev";
	const std::string bridgeToTcpClient = "bridgeToTcpClient";
	const std::string bridgeToTcpServer = "bridgeToTcpServer";

	//debug tools
	const std::string terminal = "terminal";
	const std::string log = "log";
	const std::string apipkt = "apipkt"; 
	const std::string iopkt = "sessionPkt";
}

struct ACTIVE_TDS_SESSION {
	std::string ip;
	int port;
	std::string type;
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
inline std::string makeRPCError(int code, std::string msg,std::string desc = "")
{
	std::string error = "{\"code\":" + std::to_string(code) + ",\"message\":\"" + msg + "\"";
	if (desc != "")
	{
		std::string data = ",\"data\":{\"desc\":\"" + desc + "\"}";
		error += data;
	}
	error += "}";

	return error;
}