#include "pch.h"
#include "as.h"
#include "common.h"
#include "prj.h"
#include <regex>
#include "rpcHandler.h"
#include "tdb.h"
#include "tds.h"
#include "userMng.h"
#include "logger.h"
#include "tds.h"

almServer almSrv;
almServer almSrv2;

almServer::almServer(void)
{
	m_bTestSrv = false;
}


almServer::~almServer(void)
{
}

void almServer::init()
{
	string s;
	if (fs::readFile(tds->conf->confPath + "/alarm.json", s) && s!="")
	{
		json jAlms = json::parse(s);
		for(int i=0;i< jAlms.size();i++)
		{
			json& jAlmDesc = jAlms[i];
			ALARM_TEMPLATE at;
			at.name = jAlmDesc["type"].get<string>();
			at.label = jAlmDesc["typeLabel"].get<string>();
			at.enable = true;
			if (jAlmDesc["enable"] != nullptr && jAlmDesc["enable"].get<bool>() == false) //报警屏蔽
			{
				at.enable = false;
			}
			m_mapCustomAlarmDesc[jAlmDesc["type"].get<string>()] = at;
		}
	}
	
	//tableStatus.init("\\alarms\\status");
	//tableUnack.init("\\alarms\\unack");

}

void almServer::init(const string& aCurPath, const string& aHisPath)
{
	init();

	tableCurrent.init(aCurPath);
	tableHist.init(aHisPath);
	tableHist.bOneFilePerMonth = true;
	tableCurrent.SetAlarmSrv(this);
	tableHist.SetAlarmSrv(this);

	initMOAlarmStatus();
}

void almServer::recover(ALARM_KEY& key)
{
	/*tableStatus.remove(key);

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
	}*/

	ALARM_INFO ai;
	json params;
	params["time"] = key.time;
	params["type"] = key.type;
	params["tag"] = key.tag;
	if (tableCurrent.query(params, ai))
	{
		ai.bAck = 1;
		ai.bRecover = 1;
		if (ai.bAck && ai.bRecover)//删除已消除已确认报警
		{
			tableCurrent.remove(key);
		}
		else
			tableCurrent.update(ai);
	}
	if (tableHist.query(params, ai))
	{
		ai.bRecover = 1;
		tableHist.update(ai);
	}

	json j = ai.toJson(this);
	rpcSrv.notify("alarmRecover", j);
}


void almServer::addAlarm(ALARM_INFO ai, BOOL bUpdate)
{
	tableCurrent.add(ai);
	tableHist.add(ai);

	//报警短信通知
	string msg = "报警类型:" + ai.typeLabel + "; ";
	msg += "报警对象:" + ai.tag + "; ";
	msg += "报警时间:" + ai.time + "; ";

	vector<USER_INFO> relateUsers = userMng.getRelateUsers(ai.tag);
	string pl, pnl;

	for (int i = 0; i < relateUsers.size(); i++)
	{
		USER_INFO& ui = relateUsers[i];
		if (ui.phone != "")
		{
			if (pl != "") pl += ",";
			pl += ui.phone;

			if (pnl != "") pnl += ";";
			pnl += ui.name + "," + ui.phone;
		}
	}

	if (pl != "" && tds->smsServer)
	{
		if (tds->smsServer->send(msg, pl))
		{
			LOG("[报警短信通知]报警:" + msg + ",通知人:" + pnl);
		}
		
	}

	//通知给TDS客户端
	if (bUpdate)
	{
		json j = ai.toJson(this);
		rpcSrv.notify("alarmAdd", j);
	}

}


void almServer::Update(ALARM_INFO newStatus)
{
	//忽略屏蔽报警
	if (newStatus.typeLabel == "")
	{
		//内置类型查找
		newStatus.typeLabel = getAlarmTypeLabel(newStatus.type);
		//自定义类型查找
		if (newStatus.typeLabel == "") {
			if (m_mapCustomAlarmDesc.find(newStatus.type) != m_mapCustomAlarmDesc.end())
			{
				ALARM_TEMPLATE at = m_mapCustomAlarmDesc[newStatus.type];
				newStatus.typeLabel = at.label;
				if (at.enable == false)
					return;
			}
		}


		if (newStatus.typeLabel == "")
		{
			LOG("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息");
		}
	}
	
	std::lock_guard<mutex> g(m_csAlarmData);

	if (newStatus.time == "")
	{
		TIME st;
		timeopt::now(&st);
		newStatus.time = timeopt::stTimeToStr(st);
	}

	//the time attr of a status record is always the newest occuring event
	//time attr is not needed to specify a status record 
	json filter;
	filter["tag"] = newStatus.tag;
	filter["type"] = newStatus.type;
	if (newStatus.level != "normal")
	{
		filter["isAck"] = false;
	}
	
	filter["isRecover"] = false;
	ALARM_INFO lastStatus;
	bool bTagAlarmStatusChanged = false; //该位号的报警状态是否发生改变
	if (tableCurrent.query(filter,lastStatus))
	{
		//check if status has changed
		//如果当前报警等级和之前发生改变。
		if (lastStatus.level != newStatus.level)
		{
			//先进行报警恢复。例如从报警到预警的变化。先恢复报警。
			recover(lastStatus);
			if (newStatus.level != "" &&  newStatus.level != "normal" && newStatus.level != "正常")
			{
				//再产生新的报警
				addAlarm(newStatus);
			}
			bTagAlarmStatusChanged = true;
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
			addAlarm(newStatus, FALSE);
			bTagAlarmStatusChanged = true;
		}
	}


	//更新mo对象中的缓存
	//if (bTagAlarmStatusChanged)
	//{
		OBJ* pmo = prj.queryObj(newStatus.tag);
		if (pmo)
		{
			pmo->m_jAlarmStatus = getAlarmStatus(newStatus.tag);
		}
	//}

	json j = newStatus.toJson(this);
	rpcSrv.notify("onUpdateAlarmStatus", j);
}

void almTable::freeBuff(map<string, ALARM_INFO*>& mapAlarm)
{
	map<string, ALARM_INFO*>::iterator i = mapAlarm.begin();
	for (; i != mapAlarm.end(); i++)
	{
		delete i->second;
	}
	mapAlarm.clear();
}

string almServer::rpc_addAlarm(json j, RPC_RESP& resp, BOOL bUpdate)
{
	ALARM_INFO ai;
	ai.fromJson(j);
	ai.time = timeopt::nowStr();

	if (m_bTestSrv == false)
	{
		OBJ* pObj = prj.queryObj(ai.tag);
		if (pObj && !pObj->m_bEnableAlarm)
		{
			return "\"success\"";
		}
	}

	addAlarm(ai, bUpdate);
	return "\"success\"";
}

void almServer::rpc_recoverAlarm(json j, RPC_RESP& resp)
{
	j["level"] = "normal";
	rpc_updateStatus(j, resp);
	return;
}

void almServer::rpc_updateStatus(json j,RPC_RESP& resp)
{
	if (j["tag"] == nullptr && j["ioAddr"] == nullptr)
	{
		json jErr = "必须指定 tag 或者 ioAddr 字段";
		resp.error = jErr.dump();
		return;
	}
	if (j["type"] == nullptr)
	{
		json jErr = "必须指定 type 字段";
		resp.error = jErr.dump();
		return;
	}

	try
	{
		ALARM_INFO ai;
		ai.fromJson(j);
		ai.time = timeopt::nowStr();
		
		if (m_bTestSrv == false)
		{
			OBJ* pObj = prj.queryObj(ai.tag);
			if (pObj && !pObj->m_bEnableAlarm)
			{
				resp.result = "ok";
				return;
			}
		}
	
		Update(ai);
		resp.result = RPC_OK;
	}
	catch (std::exception& e)
	{
		json jErr = e.what();
		resp.error = jErr.dump();
	}
	
}

json almServer::getAlarmStatus(string tag)
{
	json querier;
	querier["tag"] = tag;
	querier["isRecover"] = false;
	vector<ALARM_INFO*> statusList = tableCurrent.query(querier);
	json list = json::array();

	for (int i = 0; i < statusList.size(); i++)
	{
		ALARM_INFO* p = statusList[i];
		json j = p->toJson(this);
		list.push_back(j);
	}
	return list;
}

void almServer::initMOAlarmStatus()
{

}

string almServer::getAlarmTypeLabel(string type)
{
	if (type == ALARM_TYPE::overHighLimit) {
		return "超高限";
	}
	else if (type == ALARM_TYPE::overLowLimit) {
		return "超低限";
	}
	return "";
}

void almServer::AddEvent(ALARM_INFO ai)
{
	std::lock_guard<mutex>  g(m_csAlarmData);
	tableCurrent.add(ai);
	tableHist.add(ai);
}

void almServer::rpc_acknowledge(json& params, RPC_RESP& resp, RPC_SESSION session) {
	string user = session.user;
	string info = params["ackInfo"];
	ALARM_INFO ai;
	//用户位号转系统位号
	string tag = params["tag"].get<string>();
	tag = TAG::addRoot(tag, session.org);
	params["tag"] = tag;
	if(tableCurrent.query(params,ai))
	{
		ai.bAck = 1;
		if (ai.bAck && ai.bRecover)//删除已消除已确认报警
		{
			tableCurrent.remove(ai);
		}
		else
			tableCurrent.update(ai);
	}
	else
	{
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未找到报警事件");
		resp.error = error;
		return;
	}
	if(tableHist.query(params,ai))
	{
		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		timeopt::now(&ai.stConfirmTime);
		tableHist.update(ai);
	}

	json j = ai.toJson(this);
	rpcSrv.notify("alarmAck", j);

	resp.result = "\"ok\"";
}

void almServer::rpc_acknowledgeAll(json& params, RPC_RESP& resp, RPC_SESSION session)
{
	
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

bool almServer::CompareTime(TIME& time1, TIME& time2) {
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

	TIME st = timeopt::str2st(time);
	int y,m;
	y = st.wYear;
	m = st.wMonth;
	return getFilePath(y,m);
}

void almTable::loadFile(string strFile)
{
	//如果当前缓存对应的数据文件和要加载的相同，直接使用内存即可，返回
	if (buffFilePath == strFile)
		return;

	//加载新的路径到缓存
	freeBuff(buff);
	buffFilePath = strFile;

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
		buff[pAi->getKey()]=pAi;
	}
}

json almServer::rpcReqParams2Querier(json& params, RPC_SESSION session)
{
	json querier;
	string rootTag = "";
	//把用户rootTag转成系统rootTag
	if (params["rootTag"] != nullptr)
	{
		rootTag = params["rootTag"].get<string>();
		rootTag = TAG::addRoot(rootTag, session.org);
	}
	//用户没有设置rootTag.将用户的org直接作为rootTag
	else
	{
		rootTag = TAG::addRoot(rootTag, session.org);
	}
	querier["rootTag"] = rootTag;
	querier["user"] = session.user;
	return querier;
}

string almServer::rpc_getCurrent(json params, RPC_SESSION session)
{
	//全局报警禁用功能
	if (!tds->conf->enableGlobalAlarm)
	{
		return "[]";
	}


	json querier = rpcReqParams2Querier(params, session);
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getUnRecover(json params, RPC_SESSION session)
{
	//全局报警禁用功能
	if (!tds->conf->enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	querier["isRecover"] = false;
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getUnack(json params, RPC_SESSION session)
{
	//全局报警禁用功能
	if (!tds->conf->enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	querier["isAck"] = false;
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getHistory(json params, RPC_SESSION session)
{
	DE_SELECTOR deSel;
	string rootTag = params["rootTag"].get<string>();
	rootTag = TAG::addRoot(rootTag, session.org);
	params["tag"] = rootTag + "*"; //此处采用历史数据的搜索语法

	string error;
	string sParams = params.dump();
	db.parseDESelector(sParams, deSel,error );
	if(error != "") return error;
	TIME_SELECTOR& timeSelector = deSel.timeSel;
	TAG_SELECTOR& tagSelector = deSel.tagSel;

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
			tableHist.loadFile(tableHist.getFilePath(iYear,iMonth));
			for (map<string, ALARM_INFO*>::iterator it = tableHist.buff.begin(); it != tableHist.buff.end(); it++)
			{
				if (session.user != "")
				{
					if (!userMng.checkTagPermission(session.user, it->second->tag))
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
					dataSet += "," + it->second->toJsonStr(this, rootTag);
				else
					dataSet += it->second->toJsonStr(this, rootTag);
			}
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

	if (j["level"] != nullptr)
		ai.level = j["level"];
	else
		ai.level = ALARM_LEVEL::alarm;

	//可选字段
	if(j["desc"] != nullptr)
		ai.strAlarmDesc = j["desc"];
	
	return ai;
}

json ALARM_INFO::toJson(almServer* almSrv, string rootTag)
{
	ALARM_INFO* info = this;
	json j;
	if (rootTag == "")
	{
		j["tag"] = info->tag;
	}
	else
	{
		string tag = info->tag;
		tag = str::trimPrefix(tag, rootTag + ".");
		j["tag"] = tag;
	}

	j["type"] = info->type;


	if (almSrv->m_mapCustomAlarmDesc.find(info->type) != almSrv->m_mapCustomAlarmDesc.end())
	{
		ALARM_TEMPLATE at = almSrv->m_mapCustomAlarmDesc[info->type];
		j["typeLabel"] = at.label;
	}
	else
	{
		j["typeLabel"] = j["type"];
	}

	j["level"] = info->level;
	string levelLabel = getAlarmLevelLabel(level);
	if (levelLabel != "")
	{
		j["levelLabel"] = levelLabel;
	}
	else
	{
		j["levelLabel"] = info->level;
	}

	j["desc"] = info->strAlarmDesc;
	j["detail"] = info->strAlarmDetail;
	j["time"] = info->time;
	j["suggest"] = info->strSuggest;
	j["isRecover"] = info->bRecover;
	j["recoverTime"] = timeopt::st2str(info->stRecoverTime);
	j["isAck"] = info->bAck;
	j["ackTime"] = timeopt::st2str(info->stConfirmTime);
	j["ackInfo"] = info->strConfirmInfo;
	j["ackUser"] = info->strConfirmUser;
	j["picUrl"] = info->pic_url;
	j["dbPath"] = almSrv->tableCurrent.filePath;
	return j;
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
	ai.bAck = atoi(cols[8].c_str());
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
	/*8*/str += info.bAck ? "1" : "0"; str += ",";
	/*9*/str += timeopt::st2str(info.stConfirmTime); str += ",";
	/*10*/str +="\"" + info.strConfirmInfo + "\""; str += ",";
	/*11*/str += info.strConfirmUser;str += ",";
	/*12*/str+= info.pic_url;
	str += "\r\n";
	return str;
}

string ALARM_INFO::toJsonStr(almServer* almSrv, string rootTag)
{
	json j = toJson(almSrv, rootTag);
	return j.dump(2);
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
	std::unique_lock<shared_mutex> lock(m_csTable);
	loadFile(getFilePath(ai.time));
	ALARM_INFO* pNew = new ALARM_INFO();
	*pNew = ai;
	buff[ai.getKey()] = pNew;
	saveFile(getFilePath(ai.time),buff);
}

bool almTable::query(json params, ALARM_INFO& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	bool bFind = false;
	ALARM_INFO* p = NULL;
	string time;
	if (params["time"] != nullptr)
		time = params["time"].get<string>();
	loadFile(getFilePath(time));
	for(auto& i:buff)
	{
		ALARM_INFO& it = *i.second;
		if (params["tag"] != nullptr && it.tag != params["tag"].get<string>())
			continue;
		if (params["time"] != nullptr && it.time != params["time"].get<string>())
			continue;
		if (params["type"] != nullptr && it.type != params["type"].get<string>())
			continue;
		if (params["isAck"] != nullptr && it.bAck != params["isAck"].get<bool>())
			continue;
		if (params["isRecover"] != nullptr && it.bRecover != params["isRecover"].get<bool>())
			continue;

		ai = it;
		bFind = true;
	}
	if(bFind)
	{
		return true;
	}
	return false;
}
void almTable::update(ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	loadFile(getFilePath(ai.time)); //获取报警对应的数据文件
	ALARM_INFO* p = buff.at(ai.getKey());
	if(p)
	{
		*p = ai;
		saveFile(getFilePath(ai.time),buff);
	}
}
void almTable::remove(ALARM_KEY& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	loadFile(getFilePath(ai.time));
	buff.erase(ai.getKey());
	saveFile(getFilePath(ai.time),buff);
}

ALARM_QUERY almTable::parseQuerier(json& querier)
{
	ALARM_QUERY aq;
	if (querier.contains("user"))
	{
		aq.filter_user = true;
		aq.user = querier["user"].get<string>();
	}
	if (querier.contains("rootTag"))
	{
		aq.filter_rootTag = true;
		aq.rootTag = querier["rootTag"].get<string>();
	}
	if (querier.contains("tag"))
	{
		aq.filter_tag = true;
		aq.tag = querier["tag"].get<string>();
	}
	if (querier.contains("isAck"))
	{
		aq.filter_isAck = true;
		aq.isAck = querier["isAck"].get<bool>();
	}
	if (querier.contains("isRecover"))
	{
		aq.filter_isRecover = true;
		aq.isRecover = querier["isRecover"].get<bool>();
	}

	return aq;
}

vector<ALARM_INFO*> almTable::query(json querier)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	vector<ALARM_INFO*> dataSet;
	loadFile(getFilePath());
	ALARM_QUERY aq = parseQuerier(querier);

	
	for (map<string, ALARM_INFO*>::iterator it = buff.begin(); it != buff.end(); it++) {
		if (aq.filter_user && !userMng.checkTagPermission(aq.user, it->second->tag))
			continue;
		
		ALARM_INFO* pAi = it->second;
		if (aq.filter_isAck)
		{
			if (aq.isAck != pAi->bAck)
				continue;
		}

		if (aq.filter_isRecover)
		{
			if (aq.isRecover != pAi->bRecover)
				continue;
		}

		if (aq.filter_tag)
		{
			if (aq.tag != pAi->tag)
				continue;
		}

		if (aq.filter_rootTag && pAi->tag.find(aq.rootTag) == string::npos)
			continue;

		dataSet.push_back(it->second);
	}
	return dataSet;
}

string almTable::toJsonStr(json querier) {
	string rootTag = "";
	if(querier.contains("rootTag"))
		rootTag = querier["rootTag"].get<string>();
	vector<ALARM_INFO*> vec = query(querier);
	string dataSet = "[";
	for (auto& it :vec) {
		if(dataSet !="[")
			dataSet += "," + it->toJsonStr(m_pAlmSrv, rootTag);
		else
			dataSet +=  it->toJsonStr(m_pAlmSrv, rootTag);
	}
	dataSet += "]";
	return dataSet;
}

void almTable::SetAlarmSrv(almServer* pSrv)
{
	m_pAlmSrv = pSrv;
}


