#ifndef TDS_DATA_SERVER_TALMSRV_H
#define TDS_DATA_SERVER_TALMSRV_H


#include <mutex>
#ifdef TDS
#include "database/tDatabase.h"
#else
#include "tdb.h"
#endif
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
	std::string GetDir(std::string strIn);
	void CreateDirectoryPlus_old(std::string str);

	void createFolderOfPath(std::string strFile);
	bool readFile(std::string path, char*& pData, int& len);
	bool readFile(std::string path, unsigned char*& pData, int& len);
	bool readFile(std::string path, std::string& data);
	bool writeFile(std::string path, unsigned char* data, size_t len);
	bool writeFile(std::string path, char* data, size_t len);	
	bool writeFile(std::string path, std::string& data);
}

/*  Alarm Key
"tag","type","time" 3 attributes are used to identify an alarm status or an alarm event
"Tag" maybe MO or MP
"Type" is alarm type specified by a text std::string
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
	const std::string normal = "normal";
	const std::string warn = "warn";
	const std::string alarm = "alarm";
	const std::string L0 = "L0";
	const std::string L1 = "L1";
	const std::string L2 = "L2";
	const std::string L3 = "L3";
	const std::string L4 = "L4";
}

//	南京恩瑞特用
namespace AS_ALARM_LEVEL {
	const std::string normal = "normal";
	const std::string event = "event";
	const std::string warn = "warn";
	const std::string warn1 = "warn1";
	const std::string warn2 = "warn2";
	const std::string warn3 = "warn3";
	const std::string warn4 = "warn4";
	const std::string alarm = "alarm";
}

enum ALM_TABLE_TYPE {
	CURRENT_TABLE,
	UNRECOVER_TABLE,
	UNACK_TABLE,
	HISTORY_TABLE
};

class ALARM_KEY {
public:
	std::string uuid;

	std::string tag;
	std::string time;
	std::string type;
	std::string acqType;
	std::string objStatus;
	std::string id;  //custom id

	bool multiUnack;  //default disabled. one unack of one tag,so current alarm list will not be too big.

	ALARM_KEY() {
		multiUnack = false;
	}

	std::string getKeyUnrecover() {
		std::string k;
		k.reserve(tag.size() + type.size() + id.size());
		k.append(tag).append(type).append(id);
		k += acqType + objStatus;
		return k;
	}

	std::string getKey() { //diff tag with the same time is allowed
		std::string k;
		k.reserve(time.size() + tag.size() + type.size() + id.size());
		k.append(time).append(tag).append(type).append(id);
		k += acqType + objStatus;
		return k;
	}

	void getSortKey(const std::string& sortKey,std::string& completeSortKey)
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

	std::string getYearMonth() {
		std::string year = time.substr(0, 4);
		std::string month = time.substr(5, 2);
		return year + month;
	}
};

namespace ALARM_TYPE {
	const std::string overHighLimit = "over high limit";
	const std::string overLowLimit = "over low limit";
}

class ALARM_TEMPLATE {
public:
	std::string label;
	bool enable;
	std::string name;
};

class almServer;

class ALARM_INFO : public ALARM_KEY {
public:
	std::string level;
	std::string desc;
	std::string detail;
	std::string suggest;
	std::string typeLabel;
	bool isRecover;
	bool needRecover;
	std::string recoverTime;
	bool isAck;
	bool needAck;
	std::string ackTime;
	std::string ackType;
	std::string ackInfo;
	std::string ackUser;
	std::string pic_url;

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

	std::string toCSVLine();
	std::string toJsonStr(almServer* almSrv, std::string rootTag = "");
	void fromJsonStr(const std::string& s);
	void fromJson(yyjson_val* j);
	json toJson(almServer* almSrv, std::string rootTag = "");
	void toJson(almServer* almSrv, std::string rootTag, yyjson_mut_val*& jVal, yyjson_mut_doc* doc);
};

//manage 3 data tables
// status table;  unack table;  history table;
// encapsulate function of data sync with files

struct ALARM_QUERY {
	bool filter_user;
	std::string user;
	bool filter_rootTag;
	std::string rootTag;
	bool filter_tag;
	std::vector<std::string> vecTag;
	bool filter_time;
	std::string time;
	bool filter_type;
	std::vector<std::string> vecType;
	bool filter_level;
	std::vector<std::string> vecLevel;
	bool filter_isAck;
	bool isAck;
	bool filter_isRecover;
	bool isRecover;
	bool ascendingSort;
	std::string sortKey;

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
	map<std::string, int> m_colNameToColIdx;
	int m_loadIdxToColIdx[50];
	bool valLoadIdxInit;
	int getColIdxByColName(std::string colName);
	void parse(const char* line, int lineLen, ALARM_INFO& ai);

	LINE_PARSER() {
		valLoadIdxInit = false;
	}
};

class almTable {
public:
	//disk io options
	void loadFile(std::string strFile);
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


	void saveFile(std::string strFile, map<std::string, ALARM_INFO*>& memData);
	void appendFile(std::string strFile, ALARM_INFO* pNew);
	void freeBuff(map<std::string, ALARM_INFO*>& mapAlarm);

	std::string toCSV(ALARM_INFO& info);
	map<std::string, ALARM_INFO*> buff;
	map<std::string, ALARM_INFO*> unRecoverList;
	map<std::string, ALARM_INFO*> unAckList;
	std::string buffFilePath;
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
	std::string confPath; 
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
	std::string start;
	std::string end;
	bool enable;
	std::string name;

	BLOCKING_PLAN() {
		enable = false;
	}

	bool isBlocking() {
		TIME tNow; tNow.setNow();
		std::string sNow = tNow.toStr(false);
		if (enable && sNow > start && sNow < end) {
			return true;
		}
		return false;
	}

	void fromJson(json& j) {
		start = j["start"];
		end = j["end"];
		enable = j["enable"].get<bool>();
		name = j["name"].get<std::string>();
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
	std::vector<std::string> level;
	bool filter_isRecover;
	bool filter_isAck;
	bool isRecover;
	bool isAck;
	std::vector<std::string> type;
	std::vector<std::string> keywords;
	std::string org;
	std::string parseError;
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
	void init(const std::string dbPath, AsInitParam& asInitParam);
	std::string m_dbPath;
	bool m_enable;
	bool m_init;
	DB_FILE_MODE m_dbFileMode;

public:
	////internal interface
	//alarm generation
	void UpdateSync(ALARM_INFO newStatus, bool notify = true);
	void AddSync(ALARM_INFO ai,std::string& err, bool bNotify = true);
	void Update(ALARM_INFO newStatus,bool notify = true);  //update alarm state of a MO. almServer will calc alarm event internally
	void Add(ALARM_INFO ai, bool bNotify = true);//add alarm event of a MO.use for stateless alarm.

	//status check
	bool isRecover(ALARM_INFO& key);
	bool isActive(ALARM_INFO& key);
	bool isRecover(const std::string &tag, const std::string& type, const std::string& acqType, const std::string& objStatus);
	bool isActive(const std::string& tag, const std::string& type, const std::string& acqType, const std::string& objStatus);

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

	bool queryCurentAlarm(ALARM_INFO newStatus, ALARM_INFO& lastStatus);

private:
	void addAlarm(ALARM_INFO& ai, bool notify = true);

	bool parseAlmSelector(json& params, RPC_SESSION& session, ALM_SELECTOR& almSel);
	void getDBFileTimeKey_monthly(ALM_SELECTOR& almSel, std::vector<std::string>& timeKey);
	void getDBFileTimeKey_daily(ALM_SELECTOR& almSel, std::vector<std::string>& timeKey);
	void getDBFileTimeKey(ALM_SELECTOR& almSel, std::vector<std::string>& timeKey);
	bool isSelected(ALARM_INFO* ai, ALM_SELECTOR& almSel);
	void loadHistAlarm(std::vector<ALARM_INFO*>& almList, ALM_SELECTOR& almSel, RPC_SESSION session);
	void getPagedDateSet(std::vector<ALARM_INFO*> almList,ALM_SELECTOR& almSel, std::string& dataSet);
	json getAlarmStatus(std::string tag);
	void initMOAlarmStatus();
	std::string getAlarmTypeLabel(std::string type);


public:
	almServer(void);
	~almServer(void);
	static almServer& Inst() {
		static almServer inst;
		return inst;
	}
	std::string getFilePath(std::string time, ALM_TABLE_TYPE tableType, DB_FILE_MODE fileMode);
	std::string getFilePath(int y, int m, int d, ALM_TABLE_TYPE tableType, DB_FILE_MODE fileMode);

	bool CompareTime(TIME& time1, TIME& time2);

	static void ClearMap(map<std::string, ALARM_INFO*>& inMap);
	//almTable tableStatus;
	//almTable tableUnack;
	almTable tableCurrent; 
	map<std::string,almTable*> tableHist;  //key是202004 年月相加格式
	std::mutex m_csTableHistList;
	almTable* getHistTable(std::string time);
	bool handleRpc(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	map<std::string, ALARM_TEMPLATE> m_mapCustomAlarmDesc; 

	BLOCKING_PLAN m_blockingPlan;

	std::string m_curPath;
	std::string m_histPath;


	int getCallCount(std::chrono::steady_clock::duration duration, std::vector<std::chrono::steady_clock::time_point>& latestCall);
	int getLastMinuteCalls(std::vector<std::chrono::steady_clock::time_point>& latestCall);
	int getLastHourCalls(std::vector<std::chrono::steady_clock::time_point>& latestCall);
	std::vector<std::chrono::steady_clock::time_point> m_latestUpdateCall;
	std::vector<float> m_latestUpdateCallTimeCost;
	float m_lastUpdateCallTimeCostAvg;
	std::vector<std::chrono::steady_clock::time_point> m_latestAddCall;
	std::vector<float> m_latestAddCallTimeCost;
	float m_lastAddCallTimeCostAvg;
	int m_evtAlmRepeCheckTimeLen = 1; //in seconds
	long long m_iUpdateCallCount;


#ifdef ENABLE_ALM_SRV_HOOK_SCRIPT
	std::string m_scriptBeforeUpdateAlarm;
#endif
};
bool generalMatch(std::string pattern, const std::string& src);
extern almServer almSrv;


#endif /* TDS_DATA_SERVER_TALMSRV_H */
