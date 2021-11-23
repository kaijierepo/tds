#include "pch.h"
#include "as.h"
#include "common.hpp"
#include "prj.h"
#include <regex>
#include "rpcHandler.h"
#include "db.h"
#include "tds.h"
#include "users/userMng.h"

almServer almSrv;

almServer::almServer(void)
{
	
}


almServer::~almServer(void)
{
}

void almServer::run()
{
	string s;
	if (fs::readFile(tds->conf->projectConfPath + "/alarm.json", s) && s!="")
	{
		json jAlms = json::parse(s);
		for(int i=0;i< jAlms.size();i++)
		{
			json& jAlmDesc = jAlms[i];
			m_mapCustomAlarmDesc[jAlmDesc["type"].get<string>()] = jAlmDesc["typeLabel"].get<string>();
		}
	}
	
	tableStatus.init("\\alarms\\status");
	tableUnack.init("\\alarms\\unack");
	tableHist.init("\\alarms\\history");
	tableHist.bOneFilePerMonth = true;
}

void almServer::ClearAlarm(ALARM_KEY& key)
{
	tableStatus.remove(key);

	ALARM_INFO ai;
	if(tableUnack.query(key,ai))
	{
		ai.bRecover = 1;
		tableUnack.update(ai);
	}
	
	if(tableHist.query(key,ai))
	{
		ai.bRecover = 1;
		tableHist.update(ai);
	}
}


void almServer::OccurAlarm(ALARM_INFO ai)
{
	tableStatus.add(ai);
	tableUnack.add(ai);
	tableHist.add(ai);
}


void almServer::Update(ALARM_INFO newStatus)
{
	std::lock_guard<mutex> g(m_csAlarmData);

	//the time attr of a status record is always the newest occuring event
	//time attr is not needed to specify a status record 
	ALARM_KEY filter;
	filter.tag = newStatus.tag;
	filter.type = newStatus.type;
	filter.time = "*";
	ALARM_INFO lastStatus;
	if (tableStatus.query(filter,lastStatus))
	{
		//check if status has changed
		if (lastStatus.level != newStatus.level)
		{
			ClearAlarm(lastStatus);
			if (newStatus.level != "" &&  newStatus.level != "normal" && newStatus.level != "正常")
			{
				OccurAlarm(newStatus);
			}
		}
		else
		{
			//maintain last status
		}
	}
	else
	{
		if (newStatus.level != "" &&  newStatus.level != "normal" && newStatus.level != "正常")
		{
			OccurAlarm(newStatus);
		}
	}
}

void almTable::FreeAlarmList(map<string, ALARM_INFO*>& mapAlarm)
{
	map<string, ALARM_INFO*>::iterator i = mapAlarm.begin();
	for (; i != mapAlarm.end(); i++)
	{
		delete i->second;
	}
}

string almServer::rpc_addEvent(json j)
{
	ALARM_INFO ai;
	ai.fromJson(j);
	ai.time = timeopt::nowStr();
	AddEvent(ai);
	return "\"success\"";
}

void almServer::rpc_updateStatus(json j,RPC_RESP& resp)
{
	if (j["tag"] == nullptr && j["ioAddr"] == nullptr)
	{
		resp.error = "必须指定 tag 或者 ioAddr 字段";
		return;
	}
	if (j["type"] == nullptr)
	{
		resp.error = "必须指定 type 字段";
		return;
	}

	try
	{
		ALARM_INFO ai;
		ai.fromJson(j);
		ai.time = timeopt::nowStr();
		Update(ai);
		resp.result = "ok";
	}
	catch (std::exception& e)
	{
		resp.error = e.what();
	}
	
}

void almServer::AddEvent(ALARM_INFO ai)
{
	std::lock_guard<mutex>  g(m_csAlarmData);
	tableUnack.add(ai);
	tableHist.add(ai);
}

void almServer::acknowledge(ALARM_KEY& key,string ackInfo,string ackUser) {
	ALARM_INFO ai;
	if(tableStatus.query(key,ai))
	{
		ai.bConfirm = 1;
		tableStatus.update(ai);
	}

	tableUnack.remove(key);

	if(tableHist.query(key,ai))
	{
		ai.strConfirmUser = ackUser;
		ai.strConfirmInfo = ackInfo;
		GetLocalTime(&ai.stConfirmTime);
		tableHist.update(ai);
	}
}

/*
ALARM_LEVEL almServer::StringToAlarmLevel(string level)
{
	if (level.find("预")!= string::npos)
	{
		return AL_PRE_ALARM;
	}
	else if (level.find("告")!=string::npos)
	{
		return AL_ALARM;
	}
	else if (level.find("报") != string::npos)
	{
		return AL_ALARM;
	}
	else if (level.find("一级") != string::npos)
	{
		return AL_ALARM_L1;
	}
	else if (level.find("二级") != string::npos)
	{
		return AL_ALARM_L2;
	}
	else if (level.find("三级") != string::npos)
	{
		return AL_ALARM_L3;
	}
	return AL_NORMAL;
}

string almServer::AlarmLevelToString(ALARM_LEVEL level) {
	string strLevel;
	if (level == AL_PRE_ALARM)
	{
		strLevel = "预警";
	}
	else if (level == AL_ALARM)
	{
		strLevel = "告警";
	}
	else if (level == AL_ALARM_L3)
	{
		strLevel = "三级告警";
	}
	else if (level == AL_ALARM_L2)
	{
		strLevel = "二级告警";
	}
	else if (level == AL_ALARM_L1)
	{
		strLevel = "一级告警";
	}
	return strLevel;
}*/

bool almServer::CompareTime(SYSTEMTIME& time1, SYSTEMTIME& time2) {
	if (time1.wYear == time2.wYear && time1.wMonth == time2.wMonth && time1.wDay == time2.wDay && time1.wHour == time2.wHour && time1.wMinute == time2.wMinute && time1.wSecond == time2.wSecond)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void almTable::saveFile(string strFile, map<string, ALARM_INFO*>& memData)
{
	string data = "位号,报警时间,报警类型,报警等级,报警信息,报警详情,恢复状态,恢复时间,确认状态,确认时间,确认信息,确认用户\r\n";
	map<string, ALARM_INFO*>::iterator i;
	for (i = memData.begin(); i != memData.end(); i++)
	{
		ALARM_INFO& ai = *i->second;
		string str = toCSV(ai);
		data += str;
	}
	fs::createFolderOfPath(strFile);
	fs::writeFile(strFile,data);
}

string almTable::getFilePath(int y,int m){
	string p ;
	if(bOneFilePerMonth)
	{
		string strYM = str::format("%04d%02d", y, m);
		p = db.m_path + filePath + "_" + strYM + ".csv";
	}
	else
	{
		p = db.m_path + filePath  + ".csv";
	}
	return p;
}

string almTable::getFilePath(string time){
	if(time == "")
		return db.m_path + filePath  + ".csv";

	SYSTEMTIME st = timeopt::str2st(time);
	int y,m;
	y = st.wYear;
	m = st.wMonth;
	return getFilePath(y,m);
}

void almTable::loadFile(string strFile, map<string, ALARM_INFO*>& memData)
{
	string strDBData;
	fs::readFile(strFile, strDBData);
	vector<string> recLines;
	str::split(recLines, strDBData, "\r\n");
	for (int i = 1; i < recLines.size(); i++)
	{
		string str = recLines.at(i);
		if(str::trim(str) == "")
			continue;
		ALARM_INFO* pAi = new ALARM_INFO();
		*pAi = fromCSV(str);
		memData[pAi->getKey()]=pAi;
	}
}

string almServer::getCurrent(string user)
{
	return "";
}

string almServer::getStatus(string user)
{
	return tableStatus.toJson(user);
}

string almServer::getUnack(string user)
{
	return tableUnack.toJson(user);
}

string almServer::getHistory(json params, string user)
{
	TIME_SELECTOR timeSelector;
	TAG_SELECTOR tagSelector;
	string tag = params["tag"].get<string>();
	params["tag"] = TAG::trimRoot(tag);
	if (user != "")
	{
		json jUser = userMng.getUser(user);
		if (jUser != nullptr)//指定用户模式下，tag是相对位号，必须有根的位号。 报警的数据当中，存储的都是完整位号
		{
			params["root"] = jUser["org"];
		}
	}
	string error = tdsSrv.parseDataSelector(params,timeSelector,tagSelector);
	if(error != "") return error;
	
	string dataSet = "[";
	std::lock_guard<mutex> g(m_csAlarmData);
	int startYear = timeSelector.stStart.wYear;
	int startMonth = timeSelector.stStart.wMonth;
	int endYear = timeSelector.stEnd.wYear;
	int endMonth = timeSelector.stEnd.wMonth;
	int iMonth = 0;
	int iEndMonth = 0;
	for(int iYear = startYear;iYear<=endYear;iYear++)
	{
		if(iYear == startYear) iMonth = startMonth;
		else iMonth=1;
		if(iYear == endYear) iEndMonth = endMonth;
		else iEndMonth = 12;
		for(;iMonth<=iEndMonth;iMonth++)
		{
			map<string, ALARM_INFO*> almHistory;
			tableHist.loadFile(tableHist.getFilePath(iYear,iMonth),almHistory);
			for (map<string, ALARM_INFO*>::iterator it = almHistory.begin(); it != almHistory.end(); it++)
			{
				if (user != "")
				{
					if (!userMng.checkTagPermission(user, it->second->tag))
						continue;
				}
				if(!tagSelector.match(it->second->tag))
				{
					continue;
				}
				if (!timeSelector.Match(it->second->time))
				{
					continue;
				}
				

				if(dataSet !="[")
					dataSet += "," + it->second->toJson();
				else
					dataSet += it->second->toJson();
			}
			tableHist.FreeAlarmList(almHistory);
		}
	}
	dataSet += "]";
	return dataSet;
}

ALARM_INFO ALARM_INFO::fromJson(json j)
{
	ALARM_INFO& ai = *this;
	//必填字段
	ai.tag = j["tag"];
	ai.type = j["type"];

	if (j["desc"] != nullptr)
		ai.level = j["level"];
	else
		ai.level = ALARM_LEVEL::alarm;

	//可选字段
	if(j["desc"] != nullptr)
		ai.strAlarmDesc = j["desc"];
	
	return ai;
}

ALARM_INFO almTable::fromCSV(const string& line)
{
	vector<string> cols;
	string el;
	bool bInQuotation = false;
	for(int i=0;i<line.length();i++)
	{
		char* p = (char*)line.c_str() + i;
		if(!bInQuotation && *p == ',')
		{
			cols.push_back(el);
			el = "";
		}
		else if(*p =='\"')
		{
			bInQuotation = !bInQuotation;
		}
		else
		{
			el += *p;
		}
	}
	cols.push_back(el);

	ALARM_INFO ai;
	if(cols.size()!=13)return ai;
	ai.tag = cols[0];
	ai.time = cols[1].c_str();
	ai.type = cols[2].c_str();
	ai.level = cols[3].c_str();
	ai.strAlarmDesc = cols[4].c_str();
	ai.strAlarmDetail = cols[5].c_str();
	ai.bRecover = atoi(cols[6].c_str());
	ai.stRecoverTime = timeopt::str2st(cols[7].c_str());
	ai.bConfirm = atoi(cols[8].c_str());
	ai.stConfirmTime = timeopt::str2st(cols[9].c_str());
	ai.strConfirmInfo = cols[10].c_str();
	ai.strConfirmUser = cols[11].c_str();
	ai.pic_url = cols[12].c_str();
	return ai;
}

string almTable::toCSV(ALARM_INFO& info)
{
	string str;
	/*0*/str += info.tag; str += ",";
	/*1*/str += info.time; str += ",";
	/*2*/str += info.type; str += ",";
	/*3*/str += info.level; str += ",";
	/*4*/str += "\"" + info.strAlarmDesc + "\""; str += ",";
	/*5*/str += "\"" + info.strAlarmDetail + "\""; str += ",";
	/*6*/str += info.bRecover ? "1" : "0"; str += ",";
	/*7*/str += timeopt::st2str(info.stRecoverTime); str += ",";
	/*8*/str += info.bConfirm ? "1" : "0"; str += ",";
	/*9*/str += timeopt::st2str(info.stConfirmTime); str += ",";
	/*10*/str +="\"" + info.strConfirmInfo + "\""; str += ",";
	/*11*/str += info.strConfirmUser;str += ",";
	/*12*/str+= info.pic_url;
	str += "\r\n";
	return str;
}

string ALARM_INFO::toJson()
{
	ALARM_INFO* info = this;
	json j;
	j["tag"]=info->tag;
	j["type"]=info->type;

	if (almSrv.m_mapCustomAlarmDesc.find(info->type) != almSrv.m_mapCustomAlarmDesc.end())
	{
		j["typeLabel"] = almSrv.m_mapCustomAlarmDesc[info->type];
	}
	else
	{
		j["typeLabel"] = j["type"];
	}

	j["level"]=info->level;
	string levelLabel = getAlarmLevelLabel(level);
	if (levelLabel != "")
	{
		j["levelLabel"] = levelLabel;
	}
	else
	{
		j["levelLabel"] = info->level;
	}

	j["desc"]=info->strAlarmDesc;
	j["detail"]=info->strAlarmDetail;
	j["time"] = info->time;
	j["suggest"]=info->strSuggest;
	j["is_recover"]=info->bRecover;
	j["recover_time"]=timeopt::st2str(info->stRecoverTime);
	j["is_ack"]=info->bConfirm;
	j["ack_time"]=timeopt::st2str(info->stConfirmTime);
	j["ack_info"]=info->strConfirmInfo;
	j["ack_user"]=info->strConfirmUser;
	j["pic_url"]=info->pic_url;
	return j.dump(2);
}

string almServer::FormatSystemTime(SYSTEMTIME time)
{
	string strInfo;
	strInfo=str::format("%d,%d,%d,%d,%d,%d,%d,%d", time.wYear, time.wMonth, time.wDayOfWeek,
		time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	return strInfo;
}

void almServer::ClearMap(map<string, ALARM_INFO*>& inMap)
{
	for (map<string, ALARM_INFO*>::iterator it = inMap.begin(); it != inMap.end(); it++) {
		if (it->second) delete it->second;
	}
	inMap.clear();
}

void almTable::init(string file)
{
	filePath = file;
}

void almTable::add(ALARM_INFO ai)
{
	map<string, ALARM_INFO*> temp;
	loadFile(getFilePath(ai.time),temp);
	ALARM_INFO* pNew = new ALARM_INFO();
	*pNew = ai;
	temp[ai.getKey()] = pNew;
	saveFile(getFilePath(ai.time),temp);
	FreeAlarmList(temp);
}
bool almTable::query(ALARM_KEY key,ALARM_INFO& ai)
{
	ALARM_INFO* p = NULL;
	map<string, ALARM_INFO*> temp;
	loadFile(getFilePath(key.time),temp);
	for(auto& i:temp)
	{
		ALARM_KEY& it = *i.second;
		if((it.tag == key.tag || key.tag == "*")&&
		(it.time == key.time || key.time == "*")&&
		(it.type == key.type || key.type == "*"))
		{
			p= i.second;
		}
	}
	FreeAlarmList(temp);
	if(p)
	{
		ai=*p;
		return true;
	}
		
	return false;
}
void almTable::update(ALARM_INFO ai)
{
	map<string, ALARM_INFO*> temp;
	loadFile(getFilePath(ai.time),temp);
	ALARM_INFO* p = temp.at(ai.getKey());
	if(p)
	{
		*p = ai;
		saveFile(getFilePath(ai.time),temp);
	}
	FreeAlarmList(temp);
}
void almTable::remove(ALARM_KEY ai)
{
	map<string, ALARM_INFO*> temp;
	loadFile(getFilePath(ai.time),temp);
	temp.erase(ai.getKey());
	saveFile(getFilePath(ai.time),temp);
	FreeAlarmList(temp);
}

string almTable::toJson(string user){
	map<string, ALARM_INFO*> temp;
	loadFile(getFilePath(),temp);
	json jUser = userMng.getUser(user);
	string dataSet = "[";
	for (map<string, ALARM_INFO*>::iterator it = temp.begin(); it != temp.end(); it++) {
		if (jUser != nullptr)
		{
			if (!userMng.checkTagPermission(user, it->second->tag))
				continue;
		}
		
		if(dataSet !="[")
			dataSet += "," + it->second->toJson();
		else
			dataSet +=  it->second->toJson();
	}
	dataSet += "]";
	FreeAlarmList(temp);
	return dataSet;
}


