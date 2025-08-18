#pragma once

#include <mutex>
#include "tdb.h"
#include "tds.h"
#include "json.hpp"
#include "yyjson.h"
//#include <shared_mutex>
#include <chrono>

/* Performance-critical design
* append to file tail when insert to history 
* seperate history to one file per day,reduce overhead when update history data
* update,add default in async mode,will not block calling thread
* unrecover alarm is saved in a seperated memory table,because this table is most frequently queried
*/

namespace as_fs {
	string GetDir(string strIn);
	void CreateDirectoryPlus_old(string str);

	void createFolderOfPath(string strFile);
	bool readFile(string path, char*& pData, int& len);
	bool readFile(string path, unsigned char*& pData, int& len);
	bool readFile(string path, string& data);
	bool writeFile(string path, unsigned char* data, size_t len);
	bool writeFile(string path, char* data, size_t len);	
	bool writeFile(string path, string& data);
}

/*  Alarm Key
"tag","type","time" 3 attributes are used to identify an alarm status or an alarm event
"Tag" maybe MO or MP
"Type" is alarm type specified by a text string
"time" is when the alarm occured
*/

/* Alarm Level
there are 3 solutions of level
solution 1: "normal|warn|alarm" 
solution 2: "normal|red|yellow|blue" 
solution 3: "normal|1|2|3" 
value of level can by any of the 12 strings above
there aren't 2 record with the same "tag","time","type" attributes and with different "level" attribute
so level is not needed to specify an Alarm Key
*/

namespace ALARM_LEVEL {
	const string normal = "normal";
	const string warn = "warn";
	const string alarm = "alarm";
	const string L0 = "L0";
	const string L1 = "L1";
	const string L2 = "L2";
	const string L3 = "L3";
	const string L4 = "L4";
}

enum ALM_TABLE_TYPE {
	CURRENT_TABLE,
	UNRECOVER_TABLE,
	UNACK_TABLE,
	HISTORY_TABLE
};

class ALARM_KEY {
public:
	string uuid;

	string tag;
	string time;
	string type;
	string acqType;
	string objStatus;
	string id;  //custom id

	bool multiUnack;  //default disabled. one unack of one tag,so current alarm list will not be too big.

	ALARM_KEY() {
		multiUnack = false;
	}

	string getKeyUnrecover() {
		string k;
		k.reserve(tag.size() + type.size() + id.size());
		k.append(tag).append(type).append(id);
		k += acqType + objStatus;
		return k;
	}

	string getKey() { //diff tag with the same time is allowed
		string k;
		k.reserve(time.size() + tag.size() + type.size() + id.size());
		k.append(time).append(tag).append(type).append(id);
		k += acqType + objStatus;
		return k;
	}

	void getSortKey(const string& sortKey,string& completeSortKey)
	{
		if (sortKey == "tag") {
			completeSortKey.reserve(tag.size() + time.size() + type.size() + id.size());
			completeSortKey.append(tag).append(time).append(type).append(id);
		}
		else if (sortKey == "type") {
			completeSortKey.reserve(type.size() + time.size() + tag.size() + id.size());
			completeSortKey.append(type).append(time).append(tag).append(id);
		}
		else {
			completeSortKey.reserve(time.size() + tag.size() + type.size() + id.size());
			completeSortKey.append(time).append(tag).append(type).append(id);
		}
		completeSortKey += acqType + objStatus;
	}

	string getYearMonth() {
		string year = time.substr(0, 4);
		string month = time.substr(5, 2);
		return year + month;
	}
};

namespace ALARM_TYPE {
	const string overHighLimit = "over high limit";
	const string overLowLimit = "over low limit";
}

class ALARM_TEMPLATE {
public:
	string label;
	bool enable;
	string name;
};

class almServer;

class ALARM_INFO : public ALARM_KEY {
public:
	string level;
	string desc;
	string detail;
	string suggest;
	string typeLabel;
	bool isRecover;
	bool needRecover;
	string recoverTime;
	bool isAck;
	bool needAck;
	string ackTime;
	string ackInfo;
	string ackUser;
	string pic_url;

	ALARM_INFO() {
		isRecover = 0;
		needRecover = true;
		needAck = true;
		isAck = 0;
		multiUnack = false;
	}

	bool isAlarming() {
		if (level != ALARM_LEVEL::normal)
			return true;
		return false;
	}

	string toCSVLine();
	string toJsonStr(almServer* almSrv, string rootTag = "");
	void fromJsonStr(const string& s);
	void fromJson(yyjson_val* j);
	json toJson(almServer* almSrv, string rootTag = "");
	void toJson(almServer* almSrv, string rootTag, yyjson_mut_val*& jVal, yyjson_mut_doc* doc);
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

struct CELL_VAL {
	const char* p;
	int len;

	CELL_VAL() {
		p = nullptr;
		len = 0;
	}
	CELL_VAL(char* cp,int clen) {
		p = cp;
		len = clen;
	}
};

struct LINE_VAL {
	const char* p;
	int len;
};

enum DB_FILE_MODE {
	ONE_FILE_PER_DAY,
	ONE_FILE_PER_MONTH
};

struct LINE_PARSER {
	map<string, int> m_colNameToColIdx;
	int m_loadIdxToColIdx[50];
	bool valLoadIdxInit;
	int getColIdxByColName(string colName);
	void parse(const char* line, int lineLen, ALARM_INFO& ai);

	LINE_PARSER() {
		valLoadIdxInit = false;
	}
};

class almTable {
public:
	//disk io options
	void loadFile(string strFile);
	void saveFile();

	//table options
	void add(ALARM_INFO ai);
	ALARM_QUERY parseQuerier(json& querier);
	void SetAlarmSrv(almServer* pSrv);
	void acknowledge(ALARM_INFO& ai);
	void acknowledge(ALARM_INFO& ai, bool remove);

	void initUnAckUnRecover();
public:

	almTable() {
		dbFileMode = ONE_FILE_PER_DAY;
		m_pAlmSrv = nullptr;
	}
	~almTable() {
		for (auto& i : buff) {
			delete i.second;
		}
	}


	void saveFile(string strFile, map<string, ALARM_INFO*>& memData);
	void appendFile(string strFile, ALARM_INFO* pNew);
	void freeBuff(map<string, ALARM_INFO*>& mapAlarm);

	string toCSV(ALARM_INFO& info);
	map<string, ALARM_INFO*> buff;
	map<string, ALARM_INFO*> unRecoverList;
	map<string, ALARM_INFO*> unAckList;
	string buffFilePath;
	DB_FILE_MODE dbFileMode;
	mutex m_csTable;
	ALM_TABLE_TYPE m_tableType;
	int unAckListSizeLimit = 10;


protected:
	almServer* m_pAlmSrv;
	LINE_PARSER m_lineParser;
};


typedef bool (*tfunc_obj_isEnableAlarm)(std::string, std::string);
typedef bool (*tfunc_obj_setJAlmStatus)(std::string, std::string, json& js);
typedef json(*tfunc_obj_getTypeTagByTag)(std::string);
typedef void (*tfunc_log)(const char*, ...);
typedef bool (*tfunc_rpcHand_notify)(std::string, json& js);
typedef bool (*tfunc_sms_notify)(std::string, std::string&);
typedef bool (*tfunc_usrMng_checkTagPermission)(std::string, std::string);

struct AsInitParam
{
	string confPath; 
	bool enableGlobalAlarm = true;

	tfunc_obj_isEnableAlarm func_obj_isEnableAlarm = NULL;
	tfunc_obj_setJAlmStatus func_obj_setJAlmStatus = NULL;
	tfunc_obj_getTypeTagByTag func_obj_getTypeTagByTag = NULL;
	tfunc_log func_log = NULL;
	tfunc_rpcHand_notify func_rpcHand_notify = NULL;
	tfunc_sms_notify func_sms_notify = NULL;
	tfunc_usrMng_checkTagPermission func_usrMng_checkTagPermission = NULL;
};


struct BLOCKING_PLAN {
	string start;
	string end;
	bool enable;
	string name;

	BLOCKING_PLAN() {
		enable = false;
	}

	bool isBlocking() {
		TIME tNow; tNow.setNow();
		string sNow = tNow.toStr(false);
		if (enable && sNow > start && sNow < end) {
			return true;
		}
		return false;
	}

	void fromJson(json& j) {
		start = j["start"];
		end = j["end"];
		enable = j["enable"].get<bool>();
		name = j["name"].get<string>();
	}

	json toJson() {
		json j;
		j["start"] = start;
		j["end"] = end;
		j["enable"] = enable;
		j["name"] = name;
		return j;
	}
};

struct ALM_SELECTOR : public DE_SELECTOR {
	vector<string> level;
	bool filter_isRecover;
	bool filter_isAck;
	bool isRecover;
	bool isAck;
	vector<string> type;
	vector<string> keywords;
	string org;
	string parseError;
	ALM_SELECTOR() {
		isRecover = false;
		isAck = false;
		filter_isRecover = false;
		filter_isAck = false;
	}
};

class almServer
{
public:
	AsInitParam m_initParam;
	//path should be utf8 format if in Chinese
	void init(const string dbPath, AsInitParam& asInitParam);
	string m_dbPath;
	bool m_enable;
	bool m_init;
	DB_FILE_MODE m_dbFileMode;

public:
	////internal interface
	//alarm generation
	void UpdateSync(ALARM_INFO newStatus, bool notify = true);
	void AddSync(ALARM_INFO ai,string& err, bool bNotify = true);
	void Update(ALARM_INFO newStatus,bool notify = true);  //update alarm state of a MO. almServer will calc alarm event internally
	void Add(ALARM_INFO ai, bool bNotify = true);//add alarm event of a MO.use for stateless alarm.

	//status check
	bool isRecover(ALARM_INFO& key);
	bool isActive(ALARM_INFO& key);
	bool isRecover(const string &tag, const string& type, const string& acqType, const string& objStatus);
	bool isActive(const string& tag, const string& type, const string& acqType, const string& objStatus);

	//rpc handler
	void rpc_acknowledge(json& params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_acknowledgeAll(json& params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getAlarmBlockingPlan(json& params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_setAlarmBlockingPlan(json& params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_convertDBMode(json& params, RPC_RESP& resp, RPC_SESSION session);

	int rpc_approve(json& params, RPC_RESP& resp, RPC_SESSION session);
	//query alarm data
	/*
	{
		isAck:false,
		isRecover:false,
		tag:null,
		user:null
	}
	*/
	void rpc_getCurrent(json params, RPC_RESP& resp, RPC_SESSION session);//combined list of active status and unack event
	void rpc_getUnRecover(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getUnack(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getHistory(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_addAlarm(yyjson_val* yyv_params, RPC_RESP& resp, bool bUpdate = true);
	void rpc_recoverAlarm(json j, RPC_RESP& resp);
	void rpc_updateStatus(json j, RPC_RESP& resp,bool bSync = true);
	void rpc_getAlmSrvStatus(json j, RPC_RESP& resp);

	bool canRemoveFromCurrent(ALARM_INFO& ai);

private:
	void addAlarm(ALARM_INFO& ai, bool notify = true);

	bool parseAlmSelector(json& params, RPC_SESSION& session, ALM_SELECTOR& almSel);
	void getDBFileTimeKey_monthly(ALM_SELECTOR& almSel, vector<string>& timeKey);
	void getDBFileTimeKey_daily(ALM_SELECTOR& almSel, vector<string>& timeKey);
	void getDBFileTimeKey(ALM_SELECTOR& almSel, vector<string>& timeKey);
	bool isSelected(ALARM_INFO* ai, ALM_SELECTOR& almSel);
	void loadHistAlarm(vector<ALARM_INFO*>& almList, ALM_SELECTOR& almSel, RPC_SESSION session);
	void getPagedDateSet(vector<ALARM_INFO*> almList,ALM_SELECTOR& almSel, string& dataSet);
	json getAlarmStatus(string tag);
	void initMOAlarmStatus();
	string getAlarmTypeLabel(string type);


public:
	almServer(void);
	~almServer(void);
	static almServer& Inst() {
		static almServer inst;
		return inst;
	}
	string getFilePath(string time, ALM_TABLE_TYPE tableType, DB_FILE_MODE fileMode);
	string getFilePath(int y, int m, int d, ALM_TABLE_TYPE tableType, DB_FILE_MODE fileMode);

	bool CompareTime(TIME& time1, TIME& time2);

	static void ClearMap(map<string, ALARM_INFO*>& inMap);
	//almTable tableStatus;
	//almTable tableUnack;
	almTable tableCurrent; 
	map<string,almTable*> tableHist;  //key是202004 年月相加格式
	std::mutex m_csTableHistList;
	almTable* getHistTable(string time);
	bool handleRpc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	map<string, ALARM_TEMPLATE> m_mapCustomAlarmDesc; 

	BLOCKING_PLAN m_blockingPlan;

	string m_curPath;
	string m_histPath;


	int getCallCount(std::chrono::steady_clock::duration duration, vector<std::chrono::steady_clock::time_point>& latestCall);
	int getLastMinuteCalls(vector<std::chrono::steady_clock::time_point>& latestCall);
	int getLastHourCalls(vector<std::chrono::steady_clock::time_point>& latestCall);
	vector<std::chrono::steady_clock::time_point> m_latestUpdateCall;
	vector<float> m_latestUpdateCallTimeCost;
	float m_lastUpdateCallTimeCostAvg;
	vector<std::chrono::steady_clock::time_point> m_latestAddCall;
	vector<float> m_latestAddCallTimeCost;
	float m_lastAddCallTimeCostAvg;
	int m_evtAlmRepeCheckTimeLen = 1; //in seconds
	long long m_iUpdateCallCount;


#ifdef ENABLE_ALM_SRV_HOOK_SCRIPT
	string m_scriptBeforeUpdateAlarm;
#endif
};
bool generalMatch(string pattern, const string& src);
extern almServer almSrv;

