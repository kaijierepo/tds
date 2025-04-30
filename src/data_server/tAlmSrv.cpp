#include "tAlmSrv.h"
#include <regex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <random>
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#include <Commdlg.h>
#include <ShlObj_core.h>
#include <SetupAPI.h>
#include <devguid.h>
#pragma comment (lib, "Setupapi.lib")
#else
#include <unistd.h>
//#include <iconv.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#endif
#ifdef ENABLE_JERRY_SCRIPT
#include "ScriptEngine.h"
#include "scriptFunc.h"
#include "scriptManager.h"
#endif

almServer almSrv;
almServer almSrv_dev;
almServer almSrv_fau;
almServer almSrv_fauDev;

namespace as_fs {
	string GetDir(string strIn)
	{
#ifdef _WIN32
		std::string& str = strIn;
		std::string::size_type pos = str.find_last_of("\\/");
		std::string strDir = str;
		if (pos != std::string::npos) {
			strDir = str.substr(0, pos);
		}
		return strDir;
#else
		return "";
#endif
	}

	void CreateDirectoryPlus_old(string str)
	{
#ifdef _WIN32
		if (str.empty()) return;
		::CreateDirectory(str.c_str(), NULL);

		DWORD dwAttrib = GetFileAttributes(str.c_str());
		bool bDirExist = INVALID_FILE_ATTRIBUTES != dwAttrib && 0 != (dwAttrib & FILE_ATTRIBUTE_DIRECTORY);

		if (!bDirExist) {
			CreateDirectoryPlus_old(GetDir(str));
			::CreateDirectory(str.c_str(), NULL);
		}
#else
		return;
#endif
	}

	string fixPath(string path) {
		path = str::replace(path, "\\", "/");
		path = str::replace(path, "////", "/");
		path = str::replace(path, "///", "/");
		path = str::replace(path, "//", "/");
		return path;
	}


	void createFolderOfPath(string strFile)
	{
		strFile = str::replace(strFile, "\\", "/");
		strFile = str::replace(strFile, "////", "/");
		strFile = str::replace(strFile, "///", "/");
		strFile = str::replace(strFile, "//", "/");

		size_t iDotPos = strFile.rfind('.');
		size_t iSlashPos = strFile.rfind('/');
		if (iDotPos != string::npos && iDotPos > iSlashPos)//is a file
		{
			strFile = strFile.substr(0, iSlashPos);
		}
#ifdef _WIN32
		//filesystem::create_directories(utf8_to_utf16(strFile));
		int iStartPos = 0;
		while (1)
		{
			int iSlash = strFile.find('/', iStartPos);
			if (iSlash == string::npos) { break; }

			string strFolder = strFile.substr(0, iSlash);
			CreateDirectoryW(DB_STR::utf8_to_utf16(strFolder).c_str(), NULL);

			if (iSlash + 1 == strFile.length())//last char is /
				break;
			iStartPos = iSlash + 1;
		}
		CreateDirectoryW(DB_STR::utf8_to_utf16(strFile).c_str(), NULL);
#else
		std::filesystem::create_directories(strFile);
#endif
	}


	bool readFile(string path, char*& pData, int& len)
	{
		FILE* fp = nullptr;
		#ifdef _WIN32
		_wfopen_s(&fp, str::utf8_to_utf16(path).c_str(), L"rb");
		#else
		fp = fopen(path.c_str(), "rb");
		#endif
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			len = ftell(fp);
			pData = new char[len];
			fseek(fp, 0, SEEK_SET);
			fread(pData, 1, len, fp);
			fclose(fp);
			return true;
		}
		return false;
	}
	bool readFile(string path, unsigned char*& pData, int& len)
	{
		char* p = nullptr;
		bool bRet = readFile(path, p, len);
		pData = (unsigned char*)p;
		return bRet;
	}
	bool readFile(string path, string& data)
	{
		FILE* fp = nullptr;
		#ifdef _WIN32
		_wfopen_s(&fp, str::utf8_to_utf16(path).c_str(), L"rb");
		#else
		fp = fopen(path.c_str(), "rb");
		#endif
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			long len = ftell(fp);
			char* pdata = new char[len + 2];
			memset(pdata, 0, len + 2);
			fseek(fp, 0, SEEK_SET);
			fread(pdata, 1, len, fp);
			data = pdata;
			fclose(fp);
			delete[] pdata;
			return true;
		}
		return false;
	}
	bool writeFile(string path, unsigned char* data, size_t len)
	{
		return writeFile(path, (char*)data, len);
	}
	bool writeFile(string path, char* data, size_t len)
	{
		createFolderOfPath(path);

		FILE* fp = nullptr;
		#ifdef _WIN32
		wstring wpath = str::utf8_to_utf16(path);
		_wfopen_s(&fp, wpath.c_str(), L"wb");
		#else
		fp = fopen(path.c_str(), "wb");
		#endif
		if (fp)
		{
			fwrite(data, 1, len, fp);
			fclose(fp);
			return true;
		}
		else
		{
			string err = TDS_LAST_ERROR();
			err = str::utf8_to_gb(err);
			printf("[error]%s", err.c_str());
		}
		return false;
	}

	bool writeFile(string path, string& data)
	{
		return writeFile(path, (char*)data.c_str(), data.length());
	}
}

almServer::almServer(void)
{
	m_enable = true;
	m_bTestSrv = false;
	m_iUpdateCallCount = 0;
	tableCurrent.m_tableType = CURRENT_TABLE;
	m_init = false;
}


almServer::~almServer(void)
{
}

void almServer::init()
{
	string s;
	if (as_fs::readFile(m_initParam.confPath + "/alarm.json", s) && s != "")
	{
		json jAlms = json::parse(s);
		for (int i = 0; i < jAlms.size(); i++)
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

void almServer::init(const string dbPath, AsInitParam& asInitParam)
{
	m_initParam = asInitParam;
	m_dbPath = dbPath;
	m_dbPath = as_fs::fixPath(m_dbPath);

	init();

	tableCurrent.init("current");
	tableCurrent.SetAlarmSrv(this);

	initMOAlarmStatus();

	string abpConf = m_dbPath + "/alarmBlockingPlan.json";
	string s;
	as_fs::readFile(abpConf, s);
	if (s != "") {
		json j = json::parse(s);
		m_blockingPlan.fromJson(j);
	}

	m_init = true;
}

void almServer::recover(ALARM_INFO& key,string recoverTime, bool notify)
{
	if (!m_init)return;
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
		ai.isRecover = 1;
		ai.recoverTime = recoverTime;
		if (ai.isAck && ai.isRecover)
		{
			tableCurrent.remove(key);
		}
		else
			tableCurrent.update(ai);
	}
	almTable* pTableHist = getHistTable(ai.time);
	if (pTableHist->query(params, ai))
	{
		ai.isRecover = 1;
		ai.recoverTime = key.recoverTime;
		pTableHist->update(ai);
	}

	json j = ai.toJson(this);

	//rpcSrv.notify("onAlarmRecover", j);  
	if (m_initParam.func_rpcHand_notify && notify)
		m_initParam.func_rpcHand_notify("onAlarmRecover", j);
}

bool almServer::isRecover(ALARM_INFO& key) {
	std::lock_guard<mutex> g(m_csAlarmData);

	json filter;
	filter["tag"] = key.tag;
	filter["type"] = key.type;
	filter["isRecover"] = false;
	ALARM_INFO lastStatus;
	if (tableCurrent.query(filter, lastStatus))
	{
		return false;
	}
	return true;
}

bool almServer::isRecover(string tag, string type) {
	ALARM_INFO ai;
	ai.tag = tag;
	ai.type = type;
	return isRecover(ai);
}

bool almServer::isActive(ALARM_INFO& key) {
	return !isRecover(key);
}

bool almServer::isActive(string tag, string type)
{	
	return !isRecover(tag,type);
}

/*
need add alarm after status check
must and only refered in almServer::Update , almServer::Add
*/
void almServer::addAlarm(ALARM_INFO& ai, bool notify)
{
	if (!m_init)
		return;
	if (!m_enable)
		return;

	if (ai.time == "") {
		TIME t;
		t.setNow();
		ai.time = t.toStr();
	}
	if (ai.level == "") {
		ai.level = ALARM_LEVEL::alarm;
	}

	if (m_bTestSrv == false)
	{
		string sTag = ai.tag;
		if ((ai.tag.find("(") != string::npos || ai.tag.find(")") != string::npos)
			&& (ai.tag.find("[") != string::npos || ai.tag.find("]") != string::npos)) {
			//LOG("[报警服务]新报警,tag非法，小括号中括号不能同时存在,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log("[报警服务]新报警,tag非法，小括号中括号不能同时存在,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			return;
		}
		string::size_type pos_s = ai.tag.find("(");
		if (pos_s != string::npos) {
			string::size_type pos_e = ai.tag.find(")");
			if (pos_e != string::npos) {
				sTag = ai.tag.substr(pos_s + 1, pos_e - (pos_s + 1));
			}
		}
		else {
			string::size_type pos_s = ai.tag.find("[");
			if (pos_s != string::npos) {
				string::size_type pos_e = ai.tag.find("]");
				if (pos_e != string::npos) {

					sTag = ai.tag.substr(0, pos_s) + ai.tag.substr(pos_e + 1);
				}
			}
		}

		/*OBJ* pObj = prj.queryObj(sTag, "zh");
		if (pObj) {
			if (!pObj->m_bEnableAlarm)
			{
				LOG("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
				return;
			}
		}*/
		//auto func_obj_isEnableAlarm = m_initParam.func_obj_isEnableAlarm;
		//if (func_obj_isEnableAlarm != NULL && func_obj_isEnableAlarm(sTag, "zh") == false) {
		//	auto func_log = m_initParam.func_log;
		//	if (func_log)
		//		func_log("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
		//	//LOG("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
		//	return;
		//}

		//如果没有位号，报警默认不禁用
	}

	//LOG("[报警服务]新报警,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());
	auto func_log = m_initParam.func_log;
	if (func_log)
		func_log("[报警服务]新报警,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());

	//ai.uuid = uuid();
	ai.uuid = ai.time + ai.tag + ai.type + ai.level;

	tableCurrent.add(ai);
	almTable* pHistTable = getHistTable(ai.time);
	pHistTable->add(ai);

	//报警短信通知
	string msg = "报警类型:" + ai.typeLabel + "; ";
	msg += "报警对象:" + ai.tag + "; ";
	msg += "报警时间:" + ai.time + "; ";

	/*
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
			//LOG("[报警短信通知]报警:" + msg + ",通知人:" + pnl);
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log(("[报警短信通知]报警:" + msg + ",通知人:" + pnl).c_str());
		}

	}*/
	if (m_initParam.func_sms_notify) {
		m_initParam.func_sms_notify(ai.tag, msg);
	}

	//通知给TDS客户端
	if (!m_bTestSrv) {
		json j = ai.toJson(this);

		//rpcSrv.notify("onAlarmAdd", j); 
		if (m_initParam.func_rpcHand_notify && notify)
			m_initParam.func_rpcHand_notify("onAlarmAdd", j);
	}
}


string almServer::uuid() {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, 15);
	std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
	int pos = 0;
	for (char& c : uuid) {
		if (c == 'x' || c == 'y') {
			int n = dis(gen);
			int r = (n & 0x3) | 0x8;
			c = (c == 'x') ? "0123456789abcdef"[n] : "89ab"[r];
		}
		++pos;
	}
	return uuid;
}

void almServer::Update(ALARM_INFO newStatus, bool notify)
{
	if (!m_init)
		return;
	if (!m_enable)
		return;
	if (m_blockingPlan.isBlocking())
		return;


	m_iUpdateCallCount++;
	//忽略屏蔽报警
	//if (newStatus.typeLabel == "")
	//{
	//	//内置类型查找
	//	newStatus.typeLabel = getAlarmTypeLabel(newStatus.type);
	//	//自定义类型查找
	//	if (newStatus.typeLabel == "") {
	//		if (m_mapCustomAlarmDesc.find(newStatus.type) != m_mapCustomAlarmDesc.end())
	//		{
	//			ALARM_TEMPLATE at = m_mapCustomAlarmDesc[newStatus.type];
	//			newStatus.typeLabel = at.label;
	//			if (at.enable == false)
	//				return;
	//		}
	//	}


	//	if (newStatus.typeLabel == "")
	//	{
	//		//LOG("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息");
	//		auto func_log = m_initParam.func_log;
	//		if (func_log)
	//			func_log(("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息").c_str());
	//	}
	//}

	std::lock_guard<mutex> g(m_csAlarmData);

	if (newStatus.time == "" || newStatus.time == "0000-00-00 00:00:00")
	{
		TIME st;
		st.setNow();
		newStatus.time = st.toStr();
	}

	//the time attr of a status record is always the newest occuring event
	//time attr is not needed to specify a status record 
	json filter;
	filter["tag"] = newStatus.tag;
	filter["type"] = newStatus.type;
	filter["isRecover"] = false;
	ALARM_INFO lastStatus;
	bool bTagAlarmStatusChanged = false; //该位号的报警状态是否发生改变
	bool bNeedAdd = false;
	if (tableCurrent.query(filter, lastStatus))
	{
		//check if status has changed
		//如果当前报警等级和之前发生改变。
		if (lastStatus.level != newStatus.level)
		{
			//先进行报警恢复。例如从报警到预警的变化。先恢复报警。
			recover(lastStatus, newStatus.time, notify);
			if (newStatus.level != "" && newStatus.level != "normal" && newStatus.level != "正常")
			{
				bNeedAdd = true;
			}
			bTagAlarmStatusChanged = true;
		}
		else
		{
			//maintain last status
			//lastStatus.stRecoverTime = timeopt::str2st(newStatus.time);
			//lastStatus.bRecover = true;
			//recover(lastStatus);
		}
	}
	else
	{
		if (newStatus.level != "" && newStatus.level != "normal" && newStatus.level != "正常")
		{
			bNeedAdd = true;
			bTagAlarmStatusChanged = true;
		}
	}

	if (bNeedAdd) {
		addAlarm(newStatus, notify);
	}

	if (bTagAlarmStatusChanged)
	{
		//update alarm status buffered in MO
		string sTag = newStatus.tag;
		string::size_type pos_s = newStatus.tag.find("(");
		if (pos_s != string::npos)
		{
			string::size_type pos_e = newStatus.tag.find(")");

			if (pos_e != string::npos)
			{
				sTag = newStatus.tag.substr(pos_s + 1, pos_e - (pos_s + 1));
			}
		}

		/*OBJ* pmo = prj.queryObj(sTag, "zh");
		if (pmo)
		{
			pmo->m_jAlarmStatus = getAlarmStatus(newStatus.tag);
		}*/
		auto func_obj_setJAlmStatus = m_initParam.func_obj_setJAlmStatus;
		if (func_obj_setJAlmStatus != NULL) {
			json  js = getAlarmStatus(newStatus.tag);
			func_obj_setJAlmStatus(sTag, "zh", js);
		}


		//notify client
		//json j = newStatus.toJson(this);
		//rpcSrv.notify("onUpdateAlarmStatus", j);
		if (notify) {
			if (!m_bTestSrv) {
				json j = newStatus.toJson(this);

				if (m_initParam.func_rpcHand_notify && notify)
					m_initParam.func_rpcHand_notify("onAlarmUpdate", j);
			}
		}
	}
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

string almServer::Add(ALARM_INFO& ai, bool bNotify)
{
	if (m_blockingPlan.isBlocking())
		return "blocked by blocking plan";

	std::lock_guard<mutex>  g(m_csAlarmData);


	if (!ai.needRecover) {
		ai.isRecover = true;
	}

	json filter;
	filter["tag"] = ai.tag;
	filter["type"] = ai.type;
	filter["isRecover"] = false;
	ALARM_INFO lastStatus;
	
	bool bNeedAdd = false;

	if (ai.needRecover) {
		if (!tableCurrent.query(filter, lastStatus))
		{
			bNeedAdd = true;
		}
	}
	else { // state less alarm
		bNeedAdd = true;
	}

	if (bNeedAdd) {
		addAlarm(ai, bNotify);
		return "ok";
	}
	else {
		return "\"unrecover alarm with the same alarm key already existed,add fail\"";
	}	
}

#if 1
void almServer::rpc_addAlarm(json j, RPC_RESP& resp, bool bUpdate)
{
	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = TAG::addRoot(tag, rootTag);
	}

	ALARM_INFO ai;
	ai.fromJson(j);

	if (j["time"].is_string()) {
		ai.time = j["time"];
	}
	else {
		TIME t;
		t.setNow();
		ai.time = t.toStr();
	}

	string sRet = Add(ai, true);
	if (sRet == "ok") {
		resp.result = ai.toJsonStr(this);
	}
	else {
		resp.error = sRet;
	}
}

void almServer::rpc_recoverAlarm(json j, RPC_RESP& resp)
{
	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = TAG::addRoot(tag, rootTag);
	}

	j["level"] = "normal";
	rpc_updateStatus(j, resp);
	return;
}

void almServer::rpc_updateStatus(json j, RPC_RESP& resp)
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

	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = TAG::addRoot(tag, rootTag);
	}

	try
	{
		ALARM_INFO ai;
		ai.fromJson(j);
		//ai.time = timeopt::nowStr();
		Update(ai);
		resp.result = RPC_OK;
	}
	catch (std::exception& e)
	{
		json jErr = e.what();
		resp.error = jErr.dump();
	}

}


void almServer::rpc_getAlmSrvStatus(json j, RPC_RESP& resp) {
	json js;
	js["updateCallCount"] = m_iUpdateCallCount;
	resp.result = js.dump();
}

bool almServer::canRemoveFromCurrent(ALARM_INFO& ai) {
	if (ai.needRecover && ai.isRecover && ai.needAck && ai.isAck) {
		return true;
	}

	if (ai.needRecover && ai.isRecover && ai.needAck == false) {
		return true;
	}

	if (ai.needAck && ai.isAck && ai.needRecover == false) {
		return true;
	}

	return false;
}

//基于 uuid,或 tag+ time+ type 匹配记录 
void almServer::rpc_acknowledge(json& params, RPC_RESP& resp, RPC_SESSION session) {
	if (!params.contains("time")) {
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定time字段");
		resp.error = error;
		return;
	}
	
	if (!params.contains("uuid") && !params.contains("tag")) {
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定uuid或tag字段");
		resp.error = error;
		return;
	}

	if (params.contains("tag")) {
		string rootTag;
		if (params.contains("rootTag")) {
			rootTag = params["rootTag"];
		}
		string tag = params["tag"];
		tag = TAG::addRoot(tag, rootTag);

		//用户位号转系统位号
		tag = TAG::addRoot(tag, session.org);
		params["tag"] = tag;
	}

	ALARM_INFO ai;
	if (tableCurrent.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.isAck = 1;
		ai.ackUser = session.user;
		ai.ackInfo = info;
		TIME t; t.setNow();
		ai.ackTime = t.toStr();
		if (canRemoveFromCurrent(ai))//删除已消除已确认报警
		{
			tableCurrent.remove(ai);
		}
		else
			tableCurrent.update(ai);
	}
	
	almTable* pTableHist = getHistTable(ai.time);
	if (pTableHist->query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.isAck = 1;
		ai.ackUser = session.user;
		ai.ackInfo = info;
		TIME t; t.setNow();
		ai.ackTime = t.toStr(true);
		pTableHist->update(ai);
	}
	else
	{
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未找到报警事件");
		resp.error = error;
		return;
	}

	json j = ai.toJson(this);

	//rpcSrv.notify("onAlarmAck", j);  
	if (m_initParam.func_rpcHand_notify)
		m_initParam.func_rpcHand_notify("onAlarmAck", j);

	resp.result = "\"ok\"";
}

void almServer::rpc_acknowledgeAll(json& params, RPC_RESP& resp, RPC_SESSION session)
{

}

void almServer::rpc_getAlarmBlockingPlan(json& params, RPC_RESP& resp, RPC_SESSION session)
{
	resp.result = m_blockingPlan.toJson().dump();
}

void almServer::rpc_setAlarmBlockingPlan(json& params, RPC_RESP& resp, RPC_SESSION session)
{
	m_blockingPlan.fromJson(params);
	string s = params.dump();

	string abpConf = m_dbPath + "/alarmBlockingPlan.json";
	as_fs::createFolderOfPath(abpConf);
	as_fs::writeFile(abpConf, s);

	resp.result = RPC_OK;
}

//params ：对应ai那个结构
//返回负数  失败, 非负数 成功：0 未通过，1通过
//按原理，会马上恢复掉，仅恢复掉的允许审核 前端保证
int almServer::rpc_approve(json& params, RPC_RESP& resp, RPC_SESSION session) {
	int nRet = -1;
	if (params.contains("uuid") == false && (params.contains("tag") == false)) {
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定uuid或tag字段");
		resp.error = error;
		return nRet;
	}
	if (false == params["isRecover"].get<bool>()) {
		resp.error = "尚未恢复";
		nRet = -2;
		return nRet;
	}
	if (params.contains("tag")) {
		string rootTag;
		if (params.contains("rootTag")) {
			rootTag = params["rootTag"];
		}
		string tag = params["tag"];
		tag = TAG::addRoot(tag, rootTag);

		//用户位号转系统位号
		tag = TAG::addRoot(tag, session.org);
		params["tag"] = tag;
	}

	ALARM_INFO ai;
	if (tableCurrent.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];
		if (info == "通过") {
			nRet = 1;
		}
		ai.isAck = 1;
		ai.ackUser = session.user;
		ai.ackInfo = info;
		TIME t; t.setNow();
		ai.ackTime = t.toStr();
		if (ai.isAck && ai.isRecover)//删除已消除已确认报警
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
		return nRet;
	}

	if (params.contains("time") == false)//用时间对应历史表文件  时间来自未确定文件.
		params["time"] = ai.time;
	almTable* pTableHist = getHistTable(ai.time);
	if (pTableHist->query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.isAck = 1;
		ai.ackUser = session.user;
		ai.ackInfo = info;
		TIME t; t.setNow();
		ai.ackTime = t.toStr();
		pTableHist->update(ai);
	}

	json j = ai.toJson(this);

	//rpcSrv.notify("onAlarmAck", j);  
	if (m_initParam.func_rpcHand_notify)
		m_initParam.func_rpcHand_notify("onAlarmAck", j);

	resp.result = "\"ok\"";
	return nRet;
}

//构造 querier {K1:V1,...} 基础key： rootTag、user，记录key：记录任意字段 如tag、time、type等  字符串型的val可模糊匹配
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

	if (params.contains("tag")) querier["tag"] = params["tag"];//string or array
	if (params.contains("type")) querier["type"] = params["type"];//string or array
	if (params.contains("time")) querier["time"] = params["time"].get<string>();
	if (params.contains("level")) querier["level"] = params["level"];//string or array
	if (params.contains("isRecover")) querier["isRecover"] = params["isRecover"].get<bool>();
	if (params.contains("isAck")) querier["isAck"] = params["isAck"].get<bool>();
	if (params.contains("pageNo")) querier["pageNo"] = params["pageNo"].get<int>();
	if (params.contains("pageSize")) querier["pageSize"] = params["pageSize"].get<int>();
	if (params.contains("a-sort")) querier["a-sort"] = params["a-sort"];
	if (params.contains("d-sort")) querier["d-sort"] = params["d-sort"];
	//....
	return querier;
}

string almServer::rpc_getCurrent(json params, RPC_SESSION session)
{
	if (!m_enable){
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getUnRecover(json params, RPC_SESSION session)
{
	if (!m_enable) 
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
	if (!m_initParam.enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	querier["isAck"] = false;
	return tableCurrent.toJsonStr(querier);
}


bool matchTag(string pattern, const string& src)
{
	if (pattern.find("*") == string::npos) {
		if (pattern == src)
			return true;
	}
	else {
		string& strReg = pattern;
		strReg = str::replace(strReg, ".", "\\.");
		strReg = str::replace(strReg, "*", ".*");
		std::regex reg(strReg);
		if (std::regex_match(src, reg) == true) {
			return true;
		}
	}
	return false;
}

//src和pattern相等 或 *匹配
bool generalMatch(string pattern, const string& src)
{
	if (pattern.find("*") == string::npos) {
		if (pattern == src)
			return true;
	}
	else {
		string& strReg = pattern;
		strReg = str::replace(strReg, "*", ".*");
		std::regex reg(strReg);
		if (std::regex_match(src, reg) == true) {
			return true;
		}
	}
	return false;
}

string almServer::rpc_getHistory(json params, RPC_SESSION session)
{
	DE_SELECTOR deSel;

	//root tag 转 系统位号
	string rootTag = "";
	if (params["rootTag"].is_string()) {
		rootTag = params["rootTag"].get<string>();
	}
	rootTag = TAG::addRoot(rootTag, session.org);
	params["rootTag"] = rootTag;

	if (!params.contains("tag")) {
		params["tag"] = "*";
	}

	//keyword search
	vector<string> keywords;
	if (params["keyword"].is_string()) {
		vector<string> kwtemp;
		string kw = params["keyword"];
		kw = str::trim(kw);
		if (kw != "") {
			str::split(kwtemp, kw, " ");
			for (auto& i : kwtemp) {
				string s = str::trim(i);
				if (s != "") {
					keywords.push_back(s);
				}
			}
		}
	}

	bool getTypeTag = false;
	if (params.contains("getTypeTag") && params["getTypeTag"].is_boolean()) {
		getTypeTag = true;
	}

	vector<string> vecType;
	if (params.contains("type")) {
		if (params["type"].is_array()) {
			for (int i = 0; i < params["type"].size(); i++) {
				if (params["type"][i].is_string()) {
					vecType.push_back(params["type"][i].get<string>());
				}
			}
		}
		else if (params["type"].is_string()) {
			str::split(vecType, params["type"].get<string>(), ",");
		}
	}

	vector<string> vecLevel;
	if (params.contains("level")) {
		if (params["level"].is_array()) {
			for (int i = 0; i < params["level"].size(); i++) {
				if (params["level"][i].is_string()) {
					vecLevel.push_back(params["level"][i].get<string>());
				}
			}
		}
		else if (params["level"].is_string()) {
			str::split(vecLevel, params["level"].get<string>(), ",");
		}
	}

	bool filter_isRecover = false;
	bool isRecover = false;
	if (params.contains("isRecover")) {
		filter_isRecover = true;
		isRecover = params["isRecover"].get<bool>();
	}

	bool filter_isAck = false;
	bool isAck = false;
	if (params.contains("isAck")) {
		filter_isAck = true;
		isAck = params["isAck"].get<bool>();
	}

	string error;
	string sParams = params.dump();
	db.parseDESelector(sParams, deSel, error);
	if (error != "")
		return error;
	TIME_SELECTOR& timeSelector = deSel.timeSel;
	TAG_SELECTOR& tagSelector = deSel.tagSel;
	int maxSelCount = -1; //-1表示不限制
	if (deSel.pageSize > 0)
	{

	}

	int startYear = timeSelector.atomSelList[0].stStart.wYear;
	int startYearStartMonth = timeSelector.atomSelList[0].stStart.wMonth;
	int endYear = timeSelector.atomSelList[0].stEnd.wYear;
	int endYearEndMonth = timeSelector.atomSelList[0].stEnd.wMonth;

	if (deSel.sortKey == "") deSel.ascendingSort = false;
	map<SORT_FLAG, ALARM_INFO*> deList_Sort;
	vector<ALARM_INFO*> afterSortList;
	//历史数据查询一般都是查询最近的，因此从最新的数据开始往前查
	//只加载选择时间范围内的文件
	for (int iYear = endYear; iYear >= startYear; iYear--) {
		int startMonth = 1;
		if (iYear == startYear)
			startMonth = startYearStartMonth;
		else
			startMonth = 1;
		for (int iMonth = 12; iMonth >= startMonth; iMonth--) {
			string time = str::format("%04d-%02d-00 00:00:00", iYear, iMonth);
			almTable* pTableHist = getHistTable(time);
			std::shared_lock<shared_mutex> lock(pTableHist->m_csTable);
			for (auto it = pTableHist->buff.rbegin(); it != pTableHist->buff.rend(); it++) {
				//未来拟删除按照单个对象控制权限的机制，过于复杂，也用不太上
				if (session.user != "") {
					//if (!userMng.checkTagPermission(session.user, it->second->tag))
						//continue;
					//if (m_initParam.func_usrMng_checkTagPermission) {
					//	if (!m_initParam.func_usrMng_checkTagPermission(session.user, it->second->tag)) {
					//		continue;
					//	}
					//}
					if (it->second->tag.find(session.org) != 0) {
						continue;
					}
				}

				if (keywords.size() > 0) {
					vector<bool> matchRlt;
					for (int i = 0; i < keywords.size(); i++) {
						string& kw = keywords[i];
						bool match = false;
						if (it->second->tag.find(kw) != string::npos) {
							match = true;
						}
						else if (it->second->time.find(kw) != string::npos) {
							match = true;
						}
						else if (it->second->type.find(kw) != string::npos) {
							match = true;
						}
						else if (it->second->level.find(kw) != string::npos) {
							match = true;
						}
						else if (it->second->desc.find(kw) != string::npos) {
							match = true;
						}
						else if (it->second->detail.find(kw) != string::npos) {
							match = true;
						}
						matchRlt.push_back(match);
					}

					bool bMatchRlt = true;
					for (int i = 0; i < matchRlt.size();i++) {
						if (matchRlt[i] == false) {
							bMatchRlt = false;
						}
					}

					if (bMatchRlt == false) {
						continue;
					}
				}

				if (!tagSelector.match(it->second->tag)) {
					continue;
				}
				if (!timeSelector.Match(it->second->time)) {
					continue;
				}

				bool bTypeMatch = false;
				if (vecType.size() == 0)
					bTypeMatch = true;
				else {
					for (auto& one : vecType) {
						if (generalMatch(one, it->second->type)) {
							bTypeMatch = true;
							break;
						}
					}
				}
				if (!bTypeMatch)
					continue;

				bool bLevelMatch = false;
				if (vecLevel.size() == 0)
					bLevelMatch = true;
				else {
					for (auto& one : vecLevel) {
						if (generalMatch(one, it->second->level)) {
							bLevelMatch = true;
							break;
						}
					}
				}
				if (!bLevelMatch)
					continue;

				if (filter_isRecover) {
					if (isRecover != it->second->isRecover) {
						continue;
					}
				}

				if (filter_isAck) {
					if (isAck != it->second->isAck) {
						continue;
					}
				}

				//SORT_FLAG sf;
				//it->second->getSortKey(deSel.sortKey, sf.sFlag);
				//deList_Sort[sf] = it->second;
				afterSortList.push_back(it->second); //时间降序排列
			}
		}
	}

	//因为有升降序之后还有分页需求,所以要再把map转为Vector;
	//也可以直接根据map直接生成最后的json,但是逻辑稍微复杂,所以转换成Vector
	//报警这块没有配置的话,按时间降序排列
	//if (deSel.ascendingSort)
	//{
	//	for (auto it = deList_Sort.begin(); it != deList_Sort.end(); ++it) {
	//		afterSortList.push_back(it->second);
	//	}
	//}
	//else
	//{
	//	for (auto it = deList_Sort.rbegin(); it != deList_Sort.rend(); ++it) {
	//		afterSortList.push_back(it->second);
	//	}
	//}

	string dataSet;
	//pageNo缺省时默认返回第一页
	if (deSel.pageSize > 0)
	{
		yyjson_mut_doc* yyDoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* resultObj = yyjson_mut_obj(yyDoc);
		yyjson_mut_val* jDataSet = yyjson_mut_arr(yyDoc);
		yyjson_mut_obj_add_int(yyDoc, resultObj, "pageNo", deSel.pageNo);
		yyjson_mut_obj_add_int(yyDoc, resultObj, "pageSize", deSel.pageSize);
		yyjson_mut_obj_add_int(yyDoc, resultObj, "pageCount", afterSortList.size() / deSel.pageSize + (afterSortList.size() % deSel.pageSize == 0 ? 0 : 1));
		yyjson_mut_obj_add_int(yyDoc, resultObj, "deCount", afterSortList.size());
		if (afterSortList.size() > (deSel.pageNo - 1) * deSel.pageSize)
		{
			int curPageSize = deSel.pageSize;
			int leftPageCount = afterSortList.size() - (deSel.pageNo - 1) * deSel.pageSize;
			if (leftPageCount < curPageSize) {
				curPageSize = leftPageCount;
			}

			for (int i = 0; i < curPageSize; i++)
			{
				int almIdx = i + (deSel.pageNo - 1) * deSel.pageSize;
				auto it = afterSortList[almIdx];
				yyjson_mut_val* j = nullptr;
				it->toJson(this, rootTag,j,yyDoc);
				//if (getTypeTag) {
				//	/*json jTypeTag = prj.getTypeTagByTag(it->tag);
				//	if (jTypeTag != nullptr) {
				//		j["typeTag"] = jTypeTag;
				//	}*/
				//	auto func_obj_getTypeTagByTag = m_initParam.func_obj_getTypeTagByTag;
				//	if (func_obj_getTypeTagByTag != NULL) {
				//		json jTypeTag = func_obj_getTypeTagByTag(it->tag);
				//		if (jTypeTag != nullptr) {
				//			j["typeTag"] = jTypeTag;
				//		}
				//	}

				//}
				yyjson_mut_arr_append(jDataSet, j);
			}
		}
		yyjson_mut_obj_add_val(yyDoc, resultObj, "pageData", jDataSet);
		size_t len;
		dataSet = yyjson_mut_val_write(resultObj, 0, &len);
		yyjson_mut_doc_free(yyDoc);
	}
	else
	{
		json jDataSet = json::array();
		for (int i = 0; i < afterSortList.size(); i++)
		{
			auto it = afterSortList[i];
			json j = it->toJson(this, rootTag);
			if (getTypeTag) {
				/*json jTypeTag = prj.getTypeTagByTag(it->tag);
				if (jTypeTag != nullptr) {
					j["typeTag"] = jTypeTag;
				}*/
				auto func_obj_getTypeTagByTag = m_initParam.func_obj_getTypeTagByTag;
				if (func_obj_getTypeTagByTag != NULL) {
					json jTypeTag = func_obj_getTypeTagByTag(it->tag);
					if (jTypeTag != nullptr) {
						j["typeTag"] = jTypeTag;
					}
				}
			}

			jDataSet.push_back(j);
		}
		dataSet = jDataSet.dump(2);
	}

	return dataSet;
}
#endif

/*
AS_ALARM_LEVEL almServer::StringToAlarmLevel(string level)
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

string almServer::AlarmLevelToString(AS_ALARM_LEVEL level) {
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
	string data = "uuid,tag,time,type,level,info,detail,isRecover,needRecover,recoverTime,isAck,needAck,multiUnack,ackTime,ackInfo,ackUser\r\n";
	map<string, ALARM_INFO*>::iterator i;
	for (i = memData.begin(); i != memData.end(); i++)
	{
		ALARM_INFO& ai = *i->second;
		string str = toCSV(ai);
		data += str;
	}
	as_fs::createFolderOfPath(strFile);
	as_fs::writeFile(strFile, data);
}

string almTable::getFilePath(int y, int m) {
	string p;
	if (bOneFilePerMonth)
	{
		string strYM = str::format("%04d%02d", y, m);
		p = m_pAlmSrv->m_dbPath + "/" + filePath + "_" + strYM + ".csv";
	}
	else
	{
		p = m_pAlmSrv->m_dbPath + "/" + filePath + ".csv";
	}
	return p;
}

string almTable::getFilePath(string time) {
	if (m_tableType == CURRENT_TABLE || time == "")
		return m_pAlmSrv->m_dbPath + "/" + filePath + ".csv";
	else if (time != "")
	{
		TIME st;
		st.fromStr(time);
		int y, m;
		y = st.wYear;
		m = st.wMonth;
		return getFilePath(y, m);
	}
	else
	{
		return m_pAlmSrv->m_dbPath + "/" + filePath + ".csv";
	}
}


struct LINE_VAL {
	const char* p;  
	int len;
};

void parse_csv_lines(const char* s, std::vector<LINE_VAL>& lines) {
	lines.clear();  // 清空现有内容

	if (!s || !*s) return;  // 空输入处理

	const char* start = s;
	const char* p = s;

	while (*p) {
		if (*p == '\r' && *(p + 1) == '\n') {
			lines.push_back({ start, static_cast<int>(p - start) });
			p += 2;
			start = p;
		}
		else {
			++p;
		}
	}

	// 处理最后一行（如果没有以\r\n结尾）
	if (p > start) {
		lines.push_back({ start, static_cast<int>(p - start) });
	}
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
	as_fs::readFile(strFile, strDBData);
	//strDBData = as_charCodec::gb_to_utf8(strDBData);//默认使用utf8,出现乱码的GB2312只有健康管理系统,自己手动改数据库
	std::vector<LINE_VAL> recLines;
	parse_csv_lines(strDBData.data(), recLines);

	if (recLines.size() >= 1) {
		string tableHeader(recLines[0].p, recLines[0].len);
		vector<string> colNames;
		str::split(colNames, tableHeader, ",");
		for (int i = 0; i < colNames.size(); i++) {
			string name = colNames[i];
			m_colIdx[name] = i;
		}
	}

	for (int i = 1; i < recLines.size(); i++)
	{
		LINE_VAL& lv = recLines.at(i);
		//if (str::trim(str) == "") //性能考虑
		//	continue;
		ALARM_INFO* pAi = new ALARM_INFO();
		fromCSV(lv.p,lv.len,*pAi);
		buff[pAi->getKey(m_tableType)] = pAi;
	}
}


ALARM_INFO ALARM_INFO::fromJson(json j)
{
	ALARM_INFO& ai = *this;

	//必填字段
	ai.tag = j["tag"];
	ai.type = j["type"];

	if (j["time"] != nullptr)
		ai.time = j["time"];

	if (j["level"] != nullptr)
		ai.level = j["level"];
	else
		ai.level = ALARM_LEVEL::alarm;

	//可选字段
	if (j["desc"] != nullptr)
		ai.desc = j["desc"];
	if (j["isRecover"] != nullptr)
		ai.isRecover = j["isRecover"].get<bool>();
	if (j["isAck"].is_boolean())
		ai.isAck = j["isAck"].get<bool>();
	if (j["recoverTime"] != nullptr)
		ai.recoverTime = j["recoverTime"];
	if (j["needRecover"].is_boolean())
		ai.needRecover = j["needRecover"].get<bool>();
	if (j["needAck"].is_boolean())
		ai.needAck = j["needAck"].get<bool>();
	if (j["multiUnack"].is_boolean())
		ai.multiUnack = j["multiUnack"].get<bool>();
	return ai;
}

json ALARM_INFO::toJson(almServer* almSrv, string rootTag)
{
	ALARM_INFO* info = this;
	json j;
	j["uuid"] = info->uuid;

	if (rootTag == "") {
		j["tag"] = info->tag;
	}
	else {
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
	j["desc"] = info->desc;
	j["detail"] = info->detail;
	j["time"] = info->time;
	j["suggest"] = info->suggest;
	j["isRecover"] = info->isRecover;
	j["needRecover"] = info->needRecover;
	j["recoverTime"] = info->recoverTime;
	j["isAck"] = info->isAck;
	j["needAck"] = info->needAck;
	j["ackTime"] = info->ackTime;
	j["ackInfo"] = info->ackInfo;
	j["ackUser"] = info->ackUser;
	j["picUrl"] = info->pic_url;
	j["dbPath"] = almSrv->tableCurrent.filePath;
	j["multiUnack"] = info->multiUnack;
	return j;
}

void ALARM_INFO::toJson(almServer* almSrv, string rootTag,yyjson_mut_val*& jVal,yyjson_mut_doc* doc)
{
	ALARM_INFO* info = this;
	jVal = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_strcpy(doc, jVal, "uuid", info->uuid.c_str());

	if (rootTag == "") {
		yyjson_mut_obj_add_strcpy(doc, jVal, "tag", info->tag.c_str());
	}
	else {
		string tag = info->tag;
		tag = str::trimPrefix(tag, rootTag + ".");
		yyjson_mut_obj_add_strcpy(doc, jVal, "tag", tag.c_str());
	}

	yyjson_mut_obj_add_strcpy(doc, jVal, "type", info->type.c_str());

	//if (almSrv->m_mapCustomAlarmDesc.find(info->type) != almSrv->m_mapCustomAlarmDesc.end())
	//{
	//	ALARM_TEMPLATE at = almSrv->m_mapCustomAlarmDesc[info->type];
	//	j["typeLabel"] = at.label;
	//}
	//else
	//{
	//	j["typeLabel"] = j["type"];
	//}

	yyjson_mut_obj_add_strcpy(doc, jVal, "level", info->level.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "desc", info->desc.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "detail", info->detail.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "time", info->time.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "suggest", info->suggest.c_str());
	yyjson_mut_obj_add_bool(doc, jVal, "isRecover", info->isRecover);
	yyjson_mut_obj_add_bool(doc, jVal, "needRecover", info->needRecover);
	yyjson_mut_obj_add_strcpy(doc, jVal, "recoverTime", info->recoverTime.c_str());
	yyjson_mut_obj_add_bool(doc, jVal, "isAck", info->isAck);
	yyjson_mut_obj_add_bool(doc, jVal, "needAck", info->needAck);
	yyjson_mut_obj_add_strcpy(doc, jVal, "ackTime", info->ackTime.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "ackInfo", info->ackInfo.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "ackUser", info->ackUser.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "picUrl", info->pic_url.c_str());
	yyjson_mut_obj_add_strcpy(doc, jVal, "dbPath", almSrv->tableCurrent.filePath.c_str());
	yyjson_mut_obj_add_bool(doc, jVal, "multiUnack", info->multiUnack);
}

CELL_VAL* almTable::csvColVal(CELL_VAL* colVals, string colName) {
	map<string, int>::iterator iter = m_colIdx.find(colName);
	if (iter != m_colIdx.end()) {
		int idx = m_colIdx[colName];
		return colVals + idx;
	}
	else {
		return nullptr;
	}
}

int almTable::getCsvColIdx(string colName) {
	map<string, int>::iterator iter = m_colIdx.find(colName);
	if (iter != m_colIdx.end()) {
		int idx = m_colIdx[colName];
		return idx;
	}
	else {
		return -1;
	}
}


int atoi_n(const char* p, size_t len) {
	int val = 0;
	for (size_t i = 0; i < len; ++i) {
		//if (p[i] < '0' || p[i] > '9') break; // 可选：检查非法字符
		val = val * 10 + (p[i] - '0');
	}
	return val;
}

void almTable::fromCSV(const char* line,int lineLen, ALARM_INFO& ai)
{
	CELL_VAL cols[50];
	int colNum = 0;

	bool bInQuotation = false;
	int elSize = 0;
	const char* pElStart = line;
	for (int i = 0; i < lineLen; i++)
	{
		const char* p = line + i;
		if (!bInQuotation && *p == ',')
		{
			cols[colNum].p = pElStart;
			cols[colNum].len = elSize;
			colNum++;
			pElStart = p + 1;
			elSize = 0;
		}
		else if (*p == '\"')
		{
			//双引号不算在列的数值字符串之内
			bInQuotation = !bInQuotation;
			if (bInQuotation) {
				pElStart++;
			}
		}
		else
		{
			elSize++;
		}
	}


	int loadIdx = 0;
	if (!valLoadIdxInit) {
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("uuid"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("tag"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("time"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("type"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("level"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("info"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("detail"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("isRecover"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("needRecover"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("recoverTime"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("isAck"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("needAck"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("multiUnack"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("ackTime"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("ackInfo"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("ackUser"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getCsvColIdx("picUrl"); loadIdx++;
		valLoadIdxInit = true;
	}

	//core info
	loadIdx = 0; int colIdx;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.uuid.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.tag.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.time.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.type.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.level.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.desc.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.detail.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.isRecover = atoi_n(cv->p, cv->len);}loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.needRecover = atoi_n(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.recoverTime.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.isAck = atoi_n(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.needAck = atoi_n(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.multiUnack = atoi_n(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.ackTime.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.ackInfo.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.ackUser.assign(cv->p, cv->len); }loadIdx++;
	colIdx = m_loadIdxToColIdx[loadIdx]; if (colIdx >= 0) { CELL_VAL* cv = cols + colIdx; ai.pic_url.assign(cv->p, cv->len); }loadIdx++;
}

string almTable::toCSV(ALARM_INFO& info)
{
	string str;
	//core info
	str += "\"" + info.uuid + "\""; str += ",";
	str += "\"" + info.tag + "\""; str += ",";
	str += info.time; str += ",";
	str += info.type; str += ",";
	str += info.level; str += ",";
	str += "\"" + info.desc + "\""; str += ",";
	str += "\"" + info.detail + "\""; str += ",";

	//ack and recover
	str += info.isRecover ? "1" : "0"; str += ",";
	str += info.needRecover ? "1" : "0"; str += ",";
	str += info.recoverTime; str += ",";
	str += info.isAck ? "1" : "0"; str += ",";
	str += info.needAck ? "1" : "0"; str += ",";
	str += info.multiUnack ? "1" : "0"; str += ",";
	str += info.ackTime; str += ",";
	str += "\"" + info.ackInfo + "\""; str += ",";
	str += info.ackUser; str += ",";

	//others
	str += info.pic_url;
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

almTable* almServer::getHistTable(string time)
{
	std::unique_lock<mutex> lock(m_csTableHistList);
	string year = time.substr(0, 4);
	string month = time.substr(5, 2);
	string yearMonth = year + month;
	map<string, almTable*>::iterator iter = tableHist.find(yearMonth);
	if (iter == tableHist.end()) {
		almTable* p = new almTable();
		p->init("history");
		p->bOneFilePerMonth = true;
		p->SetAlarmSrv(this);
		p->m_tableType = HISTORY_TABLE;
		p->loadFile(p->getFilePath(atoi(year.c_str()), atoi(month.c_str())));
		tableHist[yearMonth] = p;
		return p;
	}
	else {
		return iter->second;
	}
}

void almTable::init(string file)
{
	filePath = file;
}

void almTable::add(ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa);
	ALARM_INFO* pNew = new ALARM_INFO();
	*pNew = ai;
	buff[ai.getKey(m_tableType)] = pNew;
	saveFile(pa, buff);
}

void almTable::acknowledge(const ALARM_INFO& ai, bool remove)
{
	for (auto i = buff.begin(); i != buff.end(); )
	{
		ALARM_INFO* it = i->second;
		if (it->tag != ai.tag)
			goto LOOP_END;
		if (it->type != ai.type)
			goto LOOP_END;
		if (it->time != ai.time)
			goto LOOP_END;
		if (it->isAck)
			goto LOOP_END;
		it->isAck = true;
		if (remove && it->isAck && it->isRecover)
		{
			i = buff.erase(i);
			break;
		}
		it->ackUser = ai.ackUser;
		it->ackInfo = ai.ackInfo;
		it->ackTime = ai.ackTime;
	LOOP_END:
		i++;
	}
}

void almTable::acknowledge(const ALARM_INFO& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	if (bOneFilePerMonth)
	{
		/*
		__cplusplus
		C++98: 199711L
		C++03: 199711L（与 C++98 相同，C++03 只是对 C++98 的一些修正，没有新特性）
		C++11: 201103L
		C++14: 201402L
		C++17: 201703L
		C++20: 202002L
		*/
#if __cplusplus <= 201402L
		WIN32_FIND_DATAW  findFileData;
		std::string searchPath = db.m_path + "/alarms/";
		std::wstring searchPath_w = str::utf8_to_utf16(db.m_path + "/alarms/*").c_str();
		HANDLE hFind = FindFirstFileW(searchPath_w.c_str(), &findFileData);//添加通配符以匹配所有文件

		if (hFind == INVALID_HANDLE_VALUE) {
			//Error finding files in directory;
			return;
		}
		do {
			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) //目录
				continue;
			std::string filename = str::utf16_to_utf8(findFileData.cFileName);
			if (filename == "." || filename == "..") {
				continue;
			}

			if (filename.find("history_") == 0 && filename.find(".csv") == 14 && filename.size() == 18
				&& to_string(stoi(filename.substr(8, 6))) == filename.substr(8, 6))
			{ // "history_YYYYMM.csv" 的长度为 15
				string fi = searchPath + filename;
				loadFile(fi);
				acknowledge(ai, false);
				saveFile(fi, buff);
			}
		} while (FindNextFileW(hFind, &findFileData) != 0);

		FindClose(hFind); // 关闭句柄

#else
		// 获取当前路径
		std::filesystem::path currentPath = db.m_path + "/alarms/";
		// 遍历当前文件夹
		for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
			if (entry.is_regular_file()) { // 确保是文件
				std::string filename = entry.path().filename().string();
				// 检查文件名是否符合指定格式
				if (filename.find("history_") == 0 && filename.find(".csv") == 14 && filename.size() == 18
					&& to_string(stoi(filename.substr(8, 6))) == filename.substr(8, 6))
				{ // "history_YYYYMM.csv" 的长度为 15
					loadFile(entry.path().string());

					acknowledge(ai, false);

					saveFile(entry.path().string(), buff);
				}
			}
		}
#endif
	}
	else
	{
		string  pa = getFilePath("");
		loadFile(pa);

		acknowledge(ai, true);

		saveFile(pa, buff);
	}
}

//找基于uuid匹配的唯一一个 或 其他字段的组合匹配到的最后一个
bool almTable::query(json params, ALARM_INFO& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	bool bFind = false;
	ALARM_INFO* p = NULL;
	string time;
	if (params["time"] != nullptr)
		time = params["time"].get<string>();
	string  pa = getFilePath(time);
	loadFile(pa);
	const string strRecoverFlag = /*as_charCodec::gb_to_utf8(*/"恢复"/*)*/;
	string strType = "";
	if (params["type"] != nullptr)
	{
		strType = params["type"].get<string>();
		auto pos = strType.find(strRecoverFlag);
		if (pos != string::npos)
		{
			strType.replace(pos, strRecoverFlag.length(), "");
		}
	}

	for (auto& i : buff)
	{
		ALARM_INFO& it = *i.second;
		if (params["uuid"] != nullptr) {
			if (it.uuid == params["uuid"].get<string>()) {
				ai = it;
				bFind = true;
				break;
			}
		}
		else {
			if (params["tag"] != nullptr && it.tag != params["tag"].get<string>())
				continue;
			if (params["time"] != nullptr && it.time != params["time"].get<string>())
				continue;
			if (params["type"] != nullptr)
			{
				if (it.type != strType)
					continue;
			}
			if (params["isAck"] != nullptr && it.isAck != params["isAck"].get<bool>())
				continue;
			if (params["isRecover"] != nullptr && it.isRecover != params["isRecover"].get<bool>())
				continue;

			ai = it;
			bFind = true;
		}
	}
	if (bFind)
	{
		return true;
	}
	return false;
}

bool almTable::query(string customId, ALARM_INFO& ai, string time)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	bool bFind = false;
	ALARM_INFO* p = NULL;
	string  pa = getFilePath(time);
	loadFile(pa);
	for (auto& i : buff)
	{
		ALARM_INFO& it = *i.second;
		if (it.id == customId)
		{
			ai = it;
			bFind = true;
			break;
		}
	}
	if (bFind)
	{
		return true;
	}
	return false;
}

void almTable::update(ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa); //获取报警对应的数据文件
	ALARM_INFO* p = buff.at(ai.getKey(m_tableType));
	if (p)
	{
		*p = ai;
		saveFile(pa, buff);
	}
}
void almTable::remove(ALARM_KEY& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa);
	buff.erase(ai.getKey(m_tableType));
	saveFile(pa, buff);
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

	if (querier.contains("tag")) {
		aq.filter_tag = true;
		if (querier["tag"].is_array()) {
			for (int i = 0; i < querier["tag"].size(); i++) {
				if (querier["tag"][i].is_string()) {
					aq.vecTag.push_back(querier["tag"][i].get<string>());
				}
				else
					assert(false);
			}
		}
		else if (querier["tag"].is_string()) {
			aq.vecTag.push_back(querier["tag"].get<string>());
		}
		else {
			assert(false);
		}
	}
	if (querier.contains("time"))
	{
		aq.filter_time = true;
		if (querier["time"].is_string())
			aq.time = querier["time"].get<string>();
		else
			assert(false);
	}
	if (querier.contains("type"))
	{
		aq.filter_type = true;
		if (querier["type"].is_array()) {
			for (int i = 0; i < querier["type"].size(); i++) {
				if (querier["type"][i].is_string()) {
					aq.vecType.push_back(querier["type"][i].get<string>());
				}
				else
					assert(false);
			}
		}
		else if (querier["type"].is_string()) {
			str::split(aq.vecType, querier["type"].get<string>(), ",");
		}
		else {
			assert(false);
		}
	}
	if (querier.contains("level"))
	{
		aq.filter_level = true;
		if (querier["level"].is_array()) {
			for (int i = 0; i < querier["level"].size(); i++) {
				if (querier["level"][i].is_string()) {
					aq.vecLevel.push_back(querier["level"][i].get<string>());
				}
				else
					assert(false);
			}
		}
		else if (querier["level"].is_string()) {
			aq.vecLevel.push_back(querier["level"].get<string>());
		}
		else {
			assert(false);
		}
	}
	if (querier.contains("isAck"))
	{
		aq.filter_isAck = true;
		if (querier["isAck"].is_boolean())
			aq.isAck = querier["isAck"].get<bool>();
		else
			assert(false);
	}
	if (querier.contains("isRecover"))
	{
		aq.filter_isRecover = true;
		if (querier["isRecover"].is_boolean())
			aq.isRecover = querier["isRecover"].get<bool>();
		else
			assert(false);
	}
	if (querier.contains("a-sort"))
	{
		aq.ascendingSort = true;
		if (querier["a-sort"].is_string())
			aq.sortKey = querier["a-sort"].get<string>();
		else aq.sortKey = "time";
	}
	else if (querier.contains("d-sort"))
	{
		aq.ascendingSort = false;
		if (querier["d-sort"].is_string())
			aq.sortKey = querier["d-sort"].get<string>();
		else aq.sortKey = "time";
	}
	else
	{
		aq.ascendingSort = false;
		aq.sortKey = "time";
	}
	return aq;
}

vector<ALARM_INFO*> almTable::query(json querier)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	vector<ALARM_INFO*> dataSet;
	loadFile(getFilePath());
	ALARM_QUERY aq = parseQuerier(querier);

	TIME_SELECTOR ts;
	if (aq.filter_time) {
		ts.init(aq.time);
	}
	map<SORT_FLAG, ALARM_INFO*> deList_Sort;
	for (map<string, ALARM_INFO*>::iterator it = buff.begin(); it != buff.end(); it++) {
		//if (aq.filter_user && !userMng.checkTagPermission(aq.user, it->second->tag))
			//continue;
		if (aq.filter_user) {
			if (m_pAlmSrv->m_initParam.func_usrMng_checkTagPermission) {
				if (m_pAlmSrv->m_initParam.func_usrMng_checkTagPermission(aq.user, it->second->tag)==false) {
					continue;
				}
			}
		}

		ALARM_INFO* pAi = it->second;

		if (aq.filter_rootTag && pAi->tag.find(aq.rootTag) == string::npos)
			continue;

		//记录里存的绝对tag。 单独的tag是相对于roottag的。
		if (aq.filter_tag) {
			bool bMatch = false;
			for (const auto& oneTag : aq.vecTag) {
				string zong_tag = oneTag;
				if (aq.rootTag != "") {
					zong_tag = aq.rootTag + "." + oneTag;
				}
				if (matchTag(zong_tag, pAi->tag)) {
					bMatch = true;
					break;
				}
			}
			if (!bMatch)
				continue;
		}

		if (aq.filter_time) {
			if (false == ts.Match(pAi->time))
				continue;
		}
		if (aq.filter_type) {
			bool bMatch = false;
			for (const auto& one : aq.vecType) {
				if (generalMatch(one, pAi->type)) {
					bMatch = true;
					break;
				}
			}
			if (!bMatch)
				continue;
		}
		if (aq.filter_level) {
			bool bMatch = false;
			for (const auto& one : aq.vecLevel) {
				if (one == pAi->level) {
					bMatch = true;
					break;
				}
			}
			if (!bMatch)
				continue;
		}
		if (aq.filter_isAck) {
			if (aq.isAck != pAi->isAck)
				continue;
		}

		if (aq.filter_isRecover) {
			if (aq.isRecover != pAi->isRecover)
				continue;
		}

		SORT_FLAG sf;
		it->second->getSortKey(aq.sortKey,sf.sFlag);
		deList_Sort[sf] = it->second;
	}

	if (aq.ascendingSort)
	{
		for (auto it = deList_Sort.begin(); it != deList_Sort.end(); ++it) {
			dataSet.push_back(it->second);
		}
	}
	else
	{
		for (auto it = deList_Sort.rbegin(); it != deList_Sort.rend(); ++it) {
			dataSet.push_back(it->second);
		}
	}
	return dataSet;
}

string almTable::toJsonStr(const json& querier) {
	string rootTag = "";
	int pageNo = 1;
	int pageSize = 0;
	if (querier.contains("rootTag"))
		rootTag = querier["rootTag"].get<string>(); //org  or org + rootTag
	if (querier.contains("pageNo"))
	{
		if (querier["pageNo"].is_number_integer())
			pageNo = querier["pageNo"].get<int>();
	}
	if (querier.contains("pageSize"))
	{
		if (querier["pageSize"].is_number_integer())
			pageSize = querier["pageSize"].get<int>();
	}

	vector<ALARM_INFO*> vec = query(querier);

	string dataSet = "";
	if (pageSize > 0)
	{
		//query by page no
		json resultObj;
		resultObj["pageNo"] = pageNo;
		resultObj["pageSize"] = pageSize;
		resultObj["pageCount"] = vec.size() / pageSize + (vec.size() % pageSize == 0 ? 0 : 1);
		resultObj["deCount"] = vec.size();
		string jDataSet = "[";
		if (vec.size() > (pageNo - 1) * pageSize)
		{
			int iterScope = pageNo * pageSize;
			if (vec.size() < iterScope) {
				iterScope = vec.size();
			}
			for (int i = (pageNo - 1) * pageSize; i < iterScope; i++)
			{
				auto it = vec[i];
				if (jDataSet != "[")
					jDataSet += "," + it->toJsonStr(m_pAlmSrv, rootTag);
				else
					jDataSet += it->toJsonStr(m_pAlmSrv, rootTag);
			}
		}
		jDataSet += "]";
		json dataObj = json::parse(jDataSet);
		resultObj["pageData"] = dataObj;
		dataSet = resultObj.dump(2);
	}
	else
	{
		string jDataSet = "[";
		for (auto& it : vec) {
			if (jDataSet != "[")
				jDataSet += "," + it->toJsonStr(m_pAlmSrv, rootTag);
			else
				jDataSet += it->toJsonStr(m_pAlmSrv, rootTag);
		}
		jDataSet += "]";

		dataSet = jDataSet;
	}

	return dataSet;
}

void almTable::SetAlarmSrv(almServer* pSrv)
{
	m_pAlmSrv = pSrv;
}


