#pragma once
#include "tdscore.h"
#include <mutex>
#include "db.h"
#include "json.hpp"

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

class ALARM_KEY{
public:
	string tag;
	string time;
	string type;
	
	string getKey(){
		return tag + ","+ time + "," + type;
	}
};


class ALARM_INFO : public ALARM_KEY{
public:
	string level;
	string strAlarmDesc;
	string strAlarmDetail;
	string strSuggest;
	bool bRecover;
	SYSTEMTIME stRecoverTime;
	bool bConfirm;
	SYSTEMTIME stConfirmTime;
	string strConfirmInfo;
	string strConfirmUser;
	string pic_url;

	ALARM_INFO() {
		strAlarmDesc = "";
		strAlarmDetail = "";
		strSuggest = "";
		bRecover = 0;
		memset(&stRecoverTime,0,sizeof(SYSTEMTIME));
		bConfirm = 0;
		memset(&stConfirmTime,0,sizeof(SYSTEMTIME));
		strConfirmInfo = "";
	}

	string toJson();
	ALARM_INFO fromJson(json j);
};

//manage 3 data tables
// status table;  unack table;  history table;
// encapsulate function of data sync with files
class almTable{
public:
	//bind with disk data file
	void init(string file);

	//table options
	void add(ALARM_INFO ai);
	bool query(ALARM_KEY key,ALARM_INFO& ai);
	void update(ALARM_INFO ai);
	void remove(ALARM_KEY ai);

	string toJson();

public:
	almTable(){
		bOneFilePerMonth = false;
	}
	string getFilePath(string time = "");
	string getFilePath(int y,int m);
	void loadFile(string strFile, map<string, ALARM_INFO*>& memData);
	void saveFile(string strFile, map<string, ALARM_INFO*>& memData);
	void FreeAlarmList(map<string, ALARM_INFO*>& mapAlarm);
	ALARM_INFO fromCSV(const string& line);
	string toCSV(ALARM_INFO& info);
	string filePath;
	bool bOneFilePerMonth;
};


class almServer
{
public:
////internal interface
//alarm generation
	void Update(ALARM_INFO newStatus);  //update alarm state of a MO. almServer will calc alarm event internally
	void AddEvent(ALARM_INFO ai);//add alarm event of a MO.use for stateless alarm.
//acknow alarm
	void acknowledge(ALARM_KEY& key,string ackInfo,string ackUser);
//query alarm data
	string getCurrent();//combined list of active status and unack event
	string getStatus();
	string getUnack();
	string getHistory(json params);

////TDS RPC
	string rpc_addEvent(json j);
	string rpc_updateStatus(json j);
public:
	almServer(void);
	~almServer(void);
	static almServer& Inst() {
		static almServer inst;
		return inst;
	}
	void Init();

	//在Update接口中，AlarmService自动计算 报警消除 和 报警产生事件
	void ClearAlarm(ALARM_KEY& key);
	void OccurAlarm(ALARM_INFO ai);
	bool CompareTime(SYSTEMTIME& time1, SYSTEMTIME& time2);

	string FormatSystemTime(SYSTEMTIME time);

	static void ClearMap(map<string, ALARM_INFO*>& inMap);
	almTable tableStatus;
	almTable tableUnack;
	almTable tableHist;
	std::mutex m_csAlarmData;
};

extern almServer almSrv;