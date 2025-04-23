#pragma once

#include <mutex>
#include "tdb.h"
#include "tds.h"
#include "json.hpp"
#include <shared_mutex>

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
	string id;  //custom id

	bool multiUnack;  //default disabled. one unack of one tag,so current alarm list will not be too big.

	string getKey(ALM_TABLE_TYPE tableType) {
		string keyWithTime = time + "," + tag + "," + type + id;;
		if(tableType == HISTORY_TABLE)
			return keyWithTime;
		else if (tableType == CURRENT_TABLE) {
			if (multiUnack) {
				return keyWithTime;
			}
			else
				return tag + "," + type + id;
		}
		return keyWithTime;
	}

	string getSortKey(string sortKey)
	{
		if (sortKey == "tag")
			return tag + "," + time + "," + type + id;
		else if (sortKey == "type")
			return   type + "," + time + "," + tag + id;
		else
			return time + "," + tag + "," + type + id;
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
	TIME recoverTime;
	bool isAck;
	bool needAck;
	TIME ackTime;
	string ackInfo;
	string ackUser;
	string pic_url;

	ALARM_INFO() {
		desc = "";
		detail = "";
		suggest = "";
		isRecover = 0;
		needRecover = true;
		needAck = true;
		memset(&recoverTime, 0, sizeof(TIME));
		isAck = 0;
		memset(&ackTime, 0, sizeof(TIME));
		ackInfo = "";
		multiUnack = false;
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

class almTable {
public:
	//bind with disk data file
	void init(string file);

	//table options
	void add(ALARM_INFO ai);
	bool query(json params, ALARM_INFO& ai);
	bool query(string customId, ALARM_INFO& ai, string time = "");
	void update(ALARM_INFO ai);
	void remove(ALARM_KEY& ai);
	ALARM_QUERY parseQuerier(json& querier);
	vector<ALARM_INFO*> query(json filter);
	string toJsonStr(const json& filter);

	void SetAlarmSrv(almServer* pSrv);

	void acknowledge(const ALARM_INFO& ai);
	void acknowledge(const ALARM_INFO& ai, bool remove);
public:

	almTable() {
		bOneFilePerMonth = false;
		m_pAlmSrv = nullptr;
	}
	~almTable() {
		for (auto& i : buff) {
			delete i.second;
		}
	}
	string getFilePath(string time = "");
	string getFilePath(int y, int m);
	void loadFile(string strFile);
	void saveFile(string strFile, map<string, ALARM_INFO*>& memData);
	void freeBuff(map<string, ALARM_INFO*>& mapAlarm);
	ALARM_INFO fromCSV(const string& line);
	string csvColVal(vector<string>& colVals, string colName);

	string toCSV(ALARM_INFO& info);
	string filePath;
	map<string, ALARM_INFO*> buff;
	string buffFilePath;
	bool bOneFilePerMonth;
	shared_mutex m_csTable;
	ALM_TABLE_TYPE m_tableType;


protected:
	almServer* m_pAlmSrv;
	map<string, int> m_colIdx;
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

class almServer
{
public:
	AsInitParam m_initParam;
	//path should be utf8 format if in Chinese
	void init(const string dbPath, AsInitParam& asInitParam);
	string m_dbPath;
	bool m_enable;
	bool m_init;

public:
	////internal interface
	//alarm generation
	void Update(ALARM_INFO newStatus,bool notify = true);  //update alarm state of a MO. almServer will calc alarm event internally
	string Add(ALARM_INFO& ai, bool bNotify = true);//add alarm event of a MO.use for stateless alarm.

	//status check
	bool isRecover(ALARM_INFO& key);
	bool isActive(ALARM_INFO& key);
	bool isRecover(string tag, string type);
	bool isActive(string tag, string type);

	//rpc handler
	void rpc_acknowledge(json& params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_acknowledgeAll(json& params, RPC_RESP& resp, RPC_SESSION session);
	int rpc_approve(json& params, RPC_RESP& resp, RPC_SESSION session);
	json rpcReqParams2Querier(json& params, RPC_SESSION session);
	//query alarm data
	/*
	{
		isAck:false,
		isRecover:false,
		tag:null,
		user:null
	}
	*/
	string rpc_getCurrent(json filter, RPC_SESSION session);//combined list of active status and unack event
	string rpc_getUnRecover(json filter, RPC_SESSION session);
	string rpc_getUnack(json filter, RPC_SESSION session);
	string rpc_getHistory(json params, RPC_SESSION session);
	void rpc_addAlarm(json j, RPC_RESP& resp, bool bUpdate = true);
	void rpc_recoverAlarm(json j, RPC_RESP& resp);
	void rpc_updateStatus(json j, RPC_RESP& resp);
	void rpc_getAlmSrvStatus(json j, RPC_RESP& resp);

	bool canRemoveFromCurrent(ALARM_INFO& ai);

private:
	void addAlarm(ALARM_INFO& ai, bool notify = true);
	//alarm status modify
	void recover(ALARM_INFO& key, string recoverTime, bool notify = true);

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
	void init();


	bool CompareTime(TIME& time1, TIME& time2);
	string uuid();

	static void ClearMap(map<string, ALARM_INFO*>& inMap);
	//almTable tableStatus;
	//almTable tableUnack;
	almTable tableCurrent; 
	almTable tableHist;
	std::mutex m_csAlarmData;
	map<string, ALARM_TEMPLATE> m_mapCustomAlarmDesc; 

	string m_curPath;
	string m_histPath;

	int m_evtAlmRepeCheckTimeLen = 1; //in seconds

	bool m_bTestSrv;
	long long m_iUpdateCallCount;
};


extern almServer almSrv;
extern almServer almSrv_dev;
extern almServer almSrv_fau;
extern almServer almSrv_fauDev;

