#pragma once

#include <mutex>
#include "tdb.h"
#include "json.hpp"
#include <shared_mutex>
/*
要求可直接移植到JHD，把引用其他文件里的那些东西都挪进来  by zgw 20241112
无名空间的各种原有定义放到名空间as(宏改为全局变量), 原有的名空间上加上as_前缀  
*/
namespace as {

	struct Date {
		unsigned short wYear;
		unsigned short wMonth;
		unsigned short wDay;
		unsigned short wDayOfWeek;
		Date() {
			memset(this, 0, sizeof(this));
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
			memset(this, 0, sizeof(this));
		}
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
			memset(this, 0, sizeof(this));
		}

		void initAsInvalid() {
			memset(this, 0, sizeof(this));
		}

		bool isValid() {
			if (wYear > 0)
				return true;
			return false;
		}

		void setDate(as::Date t);
		void setHMS(as::HMS t);

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
		time_t toUnixTimeStamp();
		void fromUnixTimeStamp(time_t t);
	};


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

		bool isDebug; //调试调用不计入session统计

		RPC_SESSION() {
			isNotification = false;
			remotePort = 0;
			localPort = 0;
			isDebug = false;
		}
	};

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

	bool matchTag(string pattern, const string& src);
	bool generalMatch(string pattern, const string& src);
	string getUUID();

	int _vscprintf_cross(const char* format, va_list pargs);
};

namespace as_timeopt {
	as::TIME now();
	void now(as::TIME& t);
	void now(as::TIME* t);
	as::TIME str2st(string str);
	string st2str(as::TIME t, bool enableMS = false);
	as::TIME Unix2SysTime(time_t iUnix, int milli = 0);
	string stTimeToStr(as::TIME time);
	string nowStr(bool enableMS = false);
}

namespace as_str {
	string trimPrefix(string s, string prefix = " ");
	string trimSuffix(string s, string suffix = " ");
	string trim(std::string s, string toTrim = " ");
	std::string format(const char* pszFmt, ...);
	string replace(string str, const string to_replaced, const string newchars);
	bool isDigits(char* pData, int len);
	bool isDigits(string s);
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);
	vector<unsigned char> hexStrToBytes(string hexStr);
	string removeChar(string str, char c);
}

namespace as_fs {
	//不带后缀作为文件夹路径。不要输入无后缀的文件路径
	bool createFolderOfPath(string strFile);
	bool readFile(string path, char*& pData, int& len);
	bool readFile(string path, unsigned char*& pData, int& len);
	bool readFile(string path, string& data);
	bool writeFile(string path, unsigned char* data, size_t len);
	bool writeFile(string path, char* data, size_t len);	//带后缀 .XXX 作为文件路径
	bool writeFile(string path, string& data);

}

namespace as_charCodec {
	//gbk,utf8 <-> unicode
	string utf16_to_utf8(wstring instr);
	string utf16_to_gb(wstring instr);
	wstring utf8_to_utf16(string instr);
	wstring gb_to_utf16(string instr);

	//gbk <-> utf8
	string utf8_to_gb(string instr);
	string gb_to_utf8(string instr);

	string utf16Str_to_utf8(string s);

	//gbk checks   GB2312 value region  A1A1－FEFE  for chinese chars is B0A1-F7FE。
	bool hasGB2312(string s);
	bool isValidGB2312(string s, size_t& errorPos, string& errorChar);
	bool isValidGB2312(string s);

	//tds local codec can  be utf8 or gbk.
	string utf16_to_tds(wstring instr);
	wstring tds_to_utf16(string instr);
	string tds_to_utf8(string instr);
	string tds_to_gb(string instr);
	string gb_to_tds(string instr);
	string utf8_to_tds(string instr);
}

namespace as_common {
	string& getCharCodec();
}

namespace as_sys {
	string getLastError(string szReason = "");

}

/*  Alarm Key
"tag","type","time" 3 attributes are used to identify an alarm status or an alarm event
"Tag" maybe MO or MP
"Type" is alarm type specified by a text string
"time" is when the alarm occured
*/

/* Alarm Level
there are 3 solutions of level
solution 1: "normal|warn|alarm" or "正常|预警|告警"
solution 2: "normal|red|yellow|blue" or "正常|红|黄|蓝"
solution 3: "normal|1|2|3" or "正常|一级|二级|三级"
value of level can by any of the 12 strings above
there aren't 2 record with the same "tag","time","type" attributes and with different "level" attribute
so level is not needed to specify an Alarm Key
*/

namespace ALARM_LEVEL {
	const string normal = "normal";
	const string warn = "warn";
	const string alarm = "alarm";
}

inline string getAlarmLevelLabel(string level)
{
	if (level == "alarm")
		return "告警";
	else if (level == "warn")
		return "预警";
	else if (level == "normal")
		return "正常";
	return "";
}

class ALARM_KEY{
public:
	string uuid;//系统生成的唯一id

	string tag;
	string time;
	string type;
	string id;  //用户自定义的alarmid，当某些报警应用，时空+type都一样时，可以使用id进一步区分
	
	string getKey(){
		return time + ","+ tag + "," + type + id;
	}

	string getSortKey(string sortKey)
	{
		//报警依照时间,位号,类型,id这样的优先级顺序
		//依照某个内容排序时,就将其提至最前端,其他顺延
		if (sortKey == "tag")
			return tag + "," + time + "," + type + id;
		else if (sortKey=="type")
			return   type + "," + time + "," + tag + id;
		else 
			return time + "," + tag + "," + type + id;
	}
};

namespace ALARM_TYPE {
	const string overHighLimit = "超高限";
	const string overLowLimit = "超低限";
}

class ALARM_TEMPLATE {
public:
	string label;
	bool enable;
	string name;
};

class almServer;

class ALARM_INFO : public ALARM_KEY{
public:
	string level;
	string strAlarmDesc;
	string strAlarmDetail;
	string strSuggest;
	string typeLabel;
	bool bRecover;
	as::TIME stRecoverTime;
	bool bAck;
	as::TIME stConfirmTime;
	string strConfirmInfo;
	string strConfirmUser;
	string pic_url;

	ALARM_INFO() {
		strAlarmDesc = "";
		strAlarmDetail = "";
		strSuggest = "";
		bRecover = 0;
		memset(&stRecoverTime,0,sizeof(as::TIME));
		bAck = 0;
		memset(&stConfirmTime,0,sizeof(as::TIME));
		strConfirmInfo = "";
	}

	bool isAlarming() {
		if (level != "" && level != "normal" && level != "正常")
			return true;
		return false;
	}

	string toJsonStr(almServer* almSrv, string rootTag = "");
	ALARM_INFO fromJson(json j);
	json toJson(almServer* almSrv, string rootTag = "");
};

//manage 3 data tables
// status table;  unack table;  history table;
// encapsulate function of data sync with files

struct ALARM_QUERY {
	bool filter_user;
	string user;
	bool filter_rootTag;
	string rootTag;
	bool filter_tag;
	vector<string> vecTag;
	bool filter_time;
	string time;
	bool filter_type;
	vector<string> vecType;
	bool filter_level;
	vector<string> vecLevel;
	bool filter_isAck;
	bool isAck;
	bool filter_isRecover;
	bool isRecover;
	bool ascendingSort;
	string sortKey;

	ALARM_QUERY() {
		filter_user = false;
		filter_rootTag = false;
		filter_tag = false;
		filter_time = false;
		filter_type = false;
		filter_level = false;
		filter_isAck = false;
		filter_isRecover = false;
		ascendingSort = false;
	}
};

class almTable{
public:
	//bind with disk data file
	void init(string file);

	//table options
	void add(ALARM_INFO ai);
	bool query(json params,ALARM_INFO& ai);
	void update(ALARM_INFO ai);
	void remove(ALARM_KEY& ai);
	ALARM_QUERY parseQuerier(json& querier);
	vector<ALARM_INFO*> query(json filter);
	string toJsonStr(const json& filter);

	void SetAlarmSrv(almServer* pSrv);

	void acknowledge(const ALARM_INFO& ai);
	void acknowledge(const ALARM_INFO& ai, bool remove);
public:

	almTable(){
		bOneFilePerMonth = false;
		m_pAlmSrv = nullptr;
	}
	~almTable() {
		for (auto& i : buff) {
			delete i.second;
		}
	}
	string getFilePath(string time = "");
	string getFilePath(int y,int m);
	void loadFile(string strFile);
	void saveFile(string strFile, map<string, ALARM_INFO*>& memData);
	void freeBuff(map<string, ALARM_INFO*>& mapAlarm);
	ALARM_INFO fromCSV(const string& line);

	string toCSV(ALARM_INFO& info);
	string filePath;
	map<string, ALARM_INFO*> buff;
	string buffFilePath;
	bool bOneFilePerMonth;
	shared_mutex m_csTable;

protected:
	almServer* m_pAlmSrv;
};


class almServer
{
public:
	//基础配置
	string m_confpath; 
	bool m_enableGlobalAlarm;

public:
////internal interface
//alarm generation
	void Update(ALARM_INFO newStatus);  //update alarm state of a MO. almServer will calc alarm event internally
	void AddEvent(ALARM_INFO ai);//add alarm event of a MO.use for stateless alarm.
	void addAlarm(ALARM_INFO ai);

	//报警恢复
	void recover(ALARM_INFO& key);

	//报警确认
	void rpc_acknowledge(json& params, as::RPC_RESP& resp, as::RPC_SESSION session);
	void rpc_acknowledgeAll(json& params, as::RPC_RESP& resp, as::RPC_SESSION session);
	int rpc_approve(json& params, as::RPC_RESP& resp, as::RPC_SESSION session);
	json rpcReqParams2Querier(json& params, as::RPC_SESSION session);
	//query alarm data
	//过滤器参数
	/*
	{
		isAck:false,
		isRecover:false,
		tag:null,
		user:null
	}
	*/
	string rpc_getCurrent(json filter, as::RPC_SESSION session);//combined list of active status and unack event
	string rpc_getUnRecover(json filter, as::RPC_SESSION session);
	string rpc_getUnack(json filter, as::RPC_SESSION session);
	string rpc_getHistory(json params, as::RPC_SESSION session);
	string rpc_addAlarm(json j, as::RPC_RESP& resp, bool bUpdate = true);
	void rpc_recoverAlarm(json j, as::RPC_RESP& resp);
	void rpc_updateStatus(json j, as::RPC_RESP& resp);

	json getAlarmStatus(string tag);//获得某一个mo对象的所有报警状态列表
	void initMOAlarmStatus();
	string getAlarmTypeLabel(string type);//内置报警的类型描述
public:
	almServer(void);
	~almServer(void);
	static almServer& Inst() {
		static almServer inst;
		return inst;
	}
	void init(); //tds的conf路径
	void init(const string& aCurPath, const string& aHisPath, string& confPath, bool enableGlobalAlarm);

	bool CompareTime(as::TIME& time1, as::TIME& time2);

	static void ClearMap(map<string, ALARM_INFO*>& inMap);
	//almTable tableStatus;
	//almTable tableUnack;
	almTable tableCurrent; //未确认或未恢复的
	almTable tableHist;
	std::mutex m_csAlarmData;
	map<string, ALARM_TEMPLATE> m_mapCustomAlarmDesc; //自定义报警信息，在配置文件的alarm.json中定义，一般是某个项目的专用报警

	string m_curPath;
	string m_histPath;

	bool m_eventAlarmRepetitiveCheck=false;
	int m_evtAlmRepeCheckTimeLen=1; //秒单位

	bool m_bTestSrv;	//	是否测试报警
};


extern almServer almSrv;
extern almServer almSrv_dev;
extern almServer almSrv_fau;
extern almServer almSrv_fauDev;

