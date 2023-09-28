#pragma once

#include <mutex>
#include "tdb.h"
#include "json.hpp"
#include "tds.h"
#include "tdsSession.h"

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
	string tag;
	string time;
	string type;
	string id;  //用户自定义的alarmid，当某些报警应用，时空+type都一样时，可以使用id进一步区分
	
	string getKey(){
		return tag + ","+ time + "," + type + id;
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


class ALARM_INFO : public ALARM_KEY{
public:
	string level;
	string strAlarmDesc;
	string strAlarmDetail;
	string strSuggest;
	string typeLabel;
	bool bRecover;
	TIME stRecoverTime;
	bool bAck;
	TIME stConfirmTime;
	string strConfirmInfo;
	string strConfirmUser;
	string pic_url;

	ALARM_INFO() {
		strAlarmDesc = "";
		strAlarmDetail = "";
		strSuggest = "";
		bRecover = 0;
		memset(&stRecoverTime,0,sizeof(TIME));
		bAck = 0;
		memset(&stConfirmTime,0,sizeof(TIME));
		strConfirmInfo = "";
	}

	bool isAlarming() {
		if (level != "" && level != "normal" && level != "正常")
			return true;
		return false;
	}

	string toJsonStr(string rootTag = "");
	ALARM_INFO fromJson(json j);
	json toJson(string rootTag = "");
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
	string tag;
	bool filter_isAck;
	bool isAck;
	bool filter_isRecover;
	bool isRecover;

	ALARM_QUERY() {
		filter_user = false;
		filter_rootTag = false;
		filter_tag = false;
		filter_isAck = false;
		filter_isRecover = false;
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
	string toJsonStr(json filter);

public:
	

	almTable(){
		bOneFilePerMonth = false;
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
};


class almServer
{
public:
////internal interface
//alarm generation
	void Update(ALARM_INFO newStatus);  //update alarm state of a MO. almServer will calc alarm event internally
	void AddEvent(ALARM_INFO ai);//add alarm event of a MO.use for stateless alarm.
	void addAlarm(ALARM_INFO ai);
//报警恢复和报警确认接口
	void recover(ALARM_KEY& key);
	void rpc_acknowledge(json& params, RPC_RESP& resp,RPC_SESSION session);
	void rpc_acknowledgeAll(json& params, RPC_RESP& resp, RPC_SESSION session);
	json rpcReqParams2Querier(json& params, RPC_SESSION session);
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
	string rpc_getCurrent(json filter,RPC_SESSION session);//combined list of active status and unack event
	string rpc_getUnRecover(json filter, RPC_SESSION session);
	string rpc_getUnack(json filter, RPC_SESSION session);
	string rpc_getHistory(json params, RPC_SESSION session);
	string rpc_addAlarm(json j, RPC_RESP& resp);
	void rpc_recoverAlarm(json j, RPC_RESP& resp);
	void rpc_updateStatus(json j, RPC_RESP& resp);

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
	void init();

	bool CompareTime(TIME& time1, TIME& time2);

	static void ClearMap(map<string, ALARM_INFO*>& inMap);
	//almTable tableStatus;
	//almTable tableUnack;
	almTable tableCurrent;
	almTable tableHist;
	std::mutex m_csAlarmData;
	map<string, ALARM_TEMPLATE> m_mapCustomAlarmDesc; //自定义报警信息，在配置文件的alarm.json中定义，一般是某个项目的专用报警
};

extern almServer almSrv;