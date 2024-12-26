#include "tAlmSrv.h"
#include "tdb.h"

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


	//带后缀 .XXX 作为文件路径
//不带后缀作为文件夹路径。不要输入无后缀的文件路径
//filesystem::path 统一用 wstring utf16输入，可以做到windows与linux兼容
	bool createFolderOfPath(string strFile)
	{
		strFile = str::replace(strFile, "\\", "/");
		strFile = str::replace(strFile, "////", "/");
		strFile = str::replace(strFile, "///", "/");
		strFile = str::replace(strFile, "//", "/");

		size_t iDotPos = strFile.rfind('.');
		size_t iSlashPos = strFile.rfind('/');
		if (iDotPos != string::npos && iDotPos > iSlashPos)//是一个文件
		{
			strFile = strFile.substr(0, iSlashPos);
		}
		//如果路径的末尾是/，创建成功也会返回false,因此删除末尾的 /
		if (iSlashPos == strFile.length() - 1) {
			strFile = strFile.substr(0, iSlashPos);
		}

#ifdef _WIN32
#ifndef _WINXP

#if __cplusplus <= 201402L
		CreateDirectoryPlus_old(strFile);
		return true;
#else
		return filesystem::create_directories(as_charCodec::tds_to_utf16(strFile));
#endif

#endif
#else
		filesystem::path p = strFile;
		return filesystem::create_directories(p);
#endif
	}


	bool readFile(string path, char*& pData, int& len)
	{
		FILE* fp = nullptr;
#ifdef UTF8
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
#ifdef UTF8
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
		CreateDirectoryPlus_old(path);

		FILE* fp = nullptr;
#ifdef UTF8
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
#ifdef UTF8
			string err = TDS_LAST_ERROR();
			err = str::utf8_to_gb(err);
			printf("[error]%s", err.c_str());
#endif
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
	m_bTestSrv = false;
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

void almServer::init(const string& aCurPath, const string& aHisPath, AsInitParam& asInitParam)
{
	m_initParam = asInitParam;

	m_curPath = aCurPath;
	m_histPath = aHisPath;

	init();

	tableCurrent.init(aCurPath);
	tableCurrent.SetAlarmSrv(this);

	tableHist.init(aHisPath);
	tableHist.bOneFilePerMonth = true;
	tableHist.SetAlarmSrv(this);

	initMOAlarmStatus();
}

void almServer::recover(ALARM_INFO& key)
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
		ai.bRecover = 1;
		ai.stRecoverTime = key.stRecoverTime;
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
		ai.stRecoverTime = key.stRecoverTime;
		tableHist.update(ai);
	}

	json j = ai.toJson(this);
	//rpcSrv.notify("onAlarmRecover", j);  
	if (m_initParam.func_rpcHand_notify)
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
2类as：  正式 和 测试
对于正式as
1）tag支持 小括号和 中括号
真正tag可放小括号里外面的表示备注或额外说明，或   中括号表示备注 外面的表示备注
即： 真正tag、xxx(真正tag)、真正tag[xxx]
2）支持基于真正tag的报警过滤
*/
void almServer::addAlarm(ALARM_INFO ai)
{
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
		auto func_obj_isEnableAlarm = m_initParam.func_obj_isEnableAlarm;
		if (func_obj_isEnableAlarm != NULL && func_obj_isEnableAlarm(sTag, "zh") == false) {
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			//LOG("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			return;
		}

		//如果没有位号，报警默认不禁用
	}

	//LOG("[报警服务]新报警,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());
	auto func_log = m_initParam.func_log;
	if (func_log)
		func_log("[报警服务]新报警,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());

	//事件报警重复性检查
	if (m_eventAlarmRepetitiveCheck) {
		json q;
		q["time"] = ai.time;
		RPC_SESSION rs;
		string s = rpc_getHistory(q, rs);
		json j = json::parse(s);
		if (j.is_array() && j.size() > 0) {
			return;
		}
	}

	ai.uuid = uuid();

	tableCurrent.add(ai);
	tableHist.add(ai);

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
		if (m_initParam.func_rpcHand_notify)
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
			//LOG("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息");
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log(("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息").c_str());
		}
	}

	std::lock_guard<mutex> g(m_csAlarmData);

	if (newStatus.time == "")
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
	if (tableCurrent.query(filter, lastStatus))
	{
		//check if status has changed
		//如果当前报警等级和之前发生改变。
		if (lastStatus.level != newStatus.level)
		{
			lastStatus.stRecoverTime.fromStr(newStatus.time);
			//先进行报警恢复。例如从报警到预警的变化。先恢复报警。
			recover(lastStatus);
			if (newStatus.level != "" && newStatus.level != "normal" && newStatus.level != "正常")
			{
				//再产生新的报警
				addAlarm(newStatus);
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
			addAlarm(newStatus);
			bTagAlarmStatusChanged = true;
		}
	}


	//更新mo对象中的缓存
	//if (bTagAlarmStatusChanged)
	//{
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

	//}

	//json j = newStatus.toJson(this);
	//rpcSrv.notify("onUpdateAlarmStatus", j);
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

void almServer::AddEvent(ALARM_INFO ai)
{
	std::lock_guard<mutex>  g(m_csAlarmData);
	ai.uuid = uuid();
	tableCurrent.add(ai);
	tableHist.add(ai);
}

#if 1
string almServer::rpc_addAlarm(json j, RPC_RESP& resp, bool bUpdate)
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

	m_eventAlarmRepetitiveCheck = false;
	if (j.contains("repeteCheck")) {
		bool b0 = j["repeteCheck"].get<bool>();
		if (b0) {
			m_eventAlarmRepetitiveCheck = true;
		}
	}

	addAlarm(ai);
	return "\"success\"";
}

void almServer::rpc_recoverAlarm(json j, RPC_RESP& resp)
{
	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = TAG::addRoot(tag, rootTag);
	}

	if (!j.contains("level"))
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


//基于 uuid,或 tag+ time+ type 匹配记录 
void almServer::rpc_acknowledge(json& params, RPC_RESP& resp, RPC_SESSION session) {
	if (params.contains("uuid") == false && (params.contains("tag") == false)) {
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

		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		ai.stConfirmTime.setNow();
		//if (ai.bAck && ai.bRecover)//删除已消除已确认报警
		//{
		//	tableCurrent.remove(ai);
		//}
		//else
		//	tableCurrent.update(ai);

		tableCurrent.acknowledge(ai);
	}
	else
	{
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未找到报警事件");
		resp.error = error;
		return;
	}

	if (params.contains("time") == false)//用时间对应历史表文件  时间来自未确定文件.
		params["time"] = ai.time;
	if (tableHist.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		ai.stConfirmTime.setNow();
		//tableHist.update(ai);
		tableHist.acknowledge(ai);

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
		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		ai.stConfirmTime.setNow();
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
		return nRet;
	}

	if (params.contains("time") == false)//用时间对应历史表文件  时间来自未确定文件.
		params["time"] = ai.time;
	if (tableHist.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		ai.stConfirmTime.setNow();
		tableHist.update(ai);
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
	//全局报警禁用功能
	if (!m_initParam.enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getUnRecover(json params, RPC_SESSION session)
{
	//全局报警禁用功能
	if (!m_initParam.enableGlobalAlarm)
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

	std::lock_guard<mutex> g(m_csAlarmData);
	int startYear = timeSelector.atomSelList[0].stStart.wYear;
	int startMonth = timeSelector.atomSelList[0].stStart.wMonth;
	int endYear = timeSelector.atomSelList[0].stEnd.wYear;
	int endMonth = timeSelector.atomSelList[0].stEnd.wMonth;
	int iMonth = 0;
	int iEndMonth = 0;
	map<SORT_FLAG, ALARM_INFO*> deList_Sort;
	vector<almTable*> histTables;
	for (int iYear = startYear; iYear <= endYear; iYear++) {
		if (iYear == startYear) iMonth = startMonth;
		else iMonth = 1;
		if (iYear == endYear) iEndMonth = endMonth;
		else iEndMonth = 12;
		for (; iMonth <= iEndMonth; iMonth++) {
			almTable* tableTemp = new almTable();
			tableTemp->init(m_histPath);
			tableTemp->bOneFilePerMonth = true;
			tableTemp->SetAlarmSrv(this);
			histTables.push_back(tableTemp);
			tableTemp->loadFile(tableTemp->getFilePath(iYear, iMonth));
			for (map<string, ALARM_INFO*>::iterator it = tableTemp->buff.begin(); it != tableTemp->buff.end(); it++) {
				if (session.user != "") {
					//if (!userMng.checkTagPermission(session.user, it->second->tag))
						//continue;
					if (m_initParam.func_usrMng_checkTagPermission) {
						if (m_initParam.func_usrMng_checkTagPermission(session.user, it->second->tag)) {
							continue;
						}
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
					if (isRecover != it->second->bRecover) {
						continue;
					}
				}

				if (filter_isAck) {
					if (isAck != it->second->bAck) {
						continue;
					}
				}

				SORT_FLAG sf;
				sf.sFlag = it->second->getSortKey(deSel.sortKey);
				deList_Sort[sf] = it->second;
			}
		}
	}

	vector<ALARM_INFO*> afterSortList;
	if (deSel.sortKey == "") deSel.ascendingSort = false;
	//因为有升降序之后还有分页需求,所以要再把map转为Vector;
	//也可以直接根据map直接生成最后的json,但是逻辑稍微复杂,所以转换成Vector
	//报警这块没有配置的话,按时间降序排列
	if (deSel.ascendingSort)
	{
		for (auto it = deList_Sort.begin(); it != deList_Sort.end(); ++it) {
			afterSortList.push_back(it->second);
		}
	}
	else
	{
		for (auto it = deList_Sort.rbegin(); it != deList_Sort.rend(); ++it) {
			afterSortList.push_back(it->second);
		}
	}

	string dataSet;
	//pageNo缺省时默认返回第一页
	if (deSel.pageSize > 0)
	{
		json resultObj;
		json jDataSet = json::array();
		resultObj["pageNo"] = deSel.pageNo;
		resultObj["pageSize"] = deSel.pageSize;
		resultObj["pageCount"] = afterSortList.size() / deSel.pageSize + (afterSortList.size() % deSel.pageSize == 0 ? 0 : 1);
		resultObj["deCount"] = afterSortList.size();
		if (afterSortList.size() > (deSel.pageNo - 1) * deSel.pageSize)
		{
			for (int i = 0; i < min(deSel.pageSize, afterSortList.size() - (deSel.pageNo - 1) * deSel.pageSize); i++)
			{
				auto it = afterSortList[i + (deSel.pageNo - 1) * deSel.pageSize];
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
		}
		resultObj["pageData"] = jDataSet;
		dataSet = resultObj.dump(2);
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

	for (auto& i : histTables) {
		delete i;
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
	string data = "uuid,位号,报警时间,报警类型,报警等级,报警信息,报警详情,恢复状态,恢复时间,确认状态,确认时间,确认信息,确认用户\r\n";
#ifdef UTF8
#else
	data = str::gb_to_utf8(data);
#endif
	map<string, ALARM_INFO*>::iterator i;
	for (i = memData.begin(); i != memData.end(); i++)
	{
		ALARM_INFO& ai = *i->second;
		string str = toCSV(ai);
		data += str;
	}
	as_fs::CreateDirectoryPlus_old(strFile);
	//data = as_charCodec::utf8_to_gb(data);//直接存储utf8
	as_fs::writeFile(strFile, data);
}

string almTable::getFilePath(int y, int m) {
	string p;
	if (bOneFilePerMonth)
	{
		string strYM = str::format("%04d%02d", y, m);
		p = db.m_path + filePath + "_" + strYM + ".csv";
	}
	else
	{
		p = db.m_path + filePath + ".csv";
	}
	return p;
}

string almTable::getFilePath(string time) {
	if (time == "")
		return db.m_path + filePath + ".csv";

	TIME st;
	st.fromStr(time);
	int y, m;
	y = st.wYear;
	m = st.wMonth;
	return getFilePath(y, m);
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
	vector<string> recLines;
	str::split(recLines, strDBData, "\r\n");
	for (int i = 1; i < recLines.size(); i++)
	{
		string str = recLines.at(i);
		if (str::trim(str) == "")
			continue;
		ALARM_INFO* pAi = new ALARM_INFO();
		*pAi = fromCSV(str);
		buff[pAi->getKey()] = pAi;
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
		ai.strAlarmDesc = j["desc"];
	if (j["isRecover"] != nullptr)
		ai.bRecover = j["isRecover"].get<bool>();
	if (j["recoverTime"] != nullptr)
		ai.stRecoverTime.fromStr(j["recoverTime"]);
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
	j["desc"] = info->strAlarmDesc;
	j["detail"] = info->strAlarmDetail;
	j["time"] = info->time;
	j["suggest"] = info->strSuggest;
	j["isRecover"] = info->bRecover;
	j["recoverTime"] = info->stRecoverTime.toStr();
	j["isAck"] = info->bAck;
	j["ackTime"] = info->stConfirmTime.toStr();
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
	for (int i = 0; i < line.length(); i++)
	{
		char* p = (char*)line.c_str() + i;
		if (!bInQuotation && *p == ',')
		{
			cols.push_back(el);
			el = "";
		}
		else if (*p == '\"')
		{
			bInQuotation = !bInQuotation;
		}
		else
		{
			el += *p;
		}
	}
	cols.push_back(el);

	int paddingSize = 14 - cols.size();
	for (int i = 0; i < paddingSize; i++) {
		cols.push_back("");
	}

	ALARM_INFO ai;
	ai.uuid = cols[0];
	ai.tag = cols[1];
	ai.time = cols[2].c_str();
	ai.type = cols[3].c_str();
	ai.level = cols[4].c_str();
	ai.strAlarmDesc = cols[5].c_str();
	ai.strAlarmDetail = cols[6].c_str();
	ai.bRecover = atoi(cols[7].c_str());
	ai.stRecoverTime.fromStr(cols[8].c_str());
	ai.bAck = atoi(cols[9].c_str());
	ai.stConfirmTime.fromStr(cols[10].c_str());
	ai.strConfirmInfo = cols[11].c_str();
	ai.strConfirmUser = cols[12].c_str();
	ai.pic_url = cols[13].c_str();
	return ai;
}

string almTable::toCSV(ALARM_INFO& info)
{
	string str;
	str += info.uuid; str += ",";
	/*0*/str += info.tag; str += ",";
	/*1*/str += info.time; str += ",";
	/*2*/str += info.type; str += ",";
	/*3*/str += info.level; str += ",";
	/*4*/str += "\"" + info.strAlarmDesc + "\""; str += ",";
	/*5*/str += "\"" + info.strAlarmDetail + "\""; str += ",";
	/*6*/str += info.bRecover ? "1" : "0"; str += ",";
	/*7*/str += info.stRecoverTime.toStr(); str += ",";
	/*8*/str += info.bAck ? "1" : "0"; str += ",";
	/*9*/str += info.stConfirmTime.toStr(); str += ",";
	/*10*/str += "\"" + info.strConfirmInfo + "\""; str += ",";
	/*11*/str += info.strConfirmUser; str += ",";
	/*12*/str += info.pic_url;
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
	string  pa = getFilePath(ai.time);
	loadFile(pa);
	ALARM_INFO* pNew = new ALARM_INFO();
	*pNew = ai;
	buff[ai.getKey()] = pNew;
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
		if (it->bAck)
			goto LOOP_END;
		it->bAck = true;
		if (remove && it->bAck && it->bRecover)
		{
			i = buff.erase(i);
			continue;
		}
		it->strConfirmUser = ai.strConfirmUser;
		it->strConfirmInfo = ai.strConfirmInfo;
		it->stConfirmTime = ai.stConfirmTime;
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
		WIN32_FIND_DATA findFileData;
		std::string searchPath = db.m_path + "/alarms/";
		HANDLE hFind = FindFirstFile((searchPath + "*").c_str(), &findFileData);//添加通配符以匹配所有文件

		if (hFind == INVALID_HANDLE_VALUE) {
			//Error finding files in directory;
			return;
		}
		do {
			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) //目录
				continue;
			std::string filename = findFileData.cFileName;
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
		} while (FindNextFile(hFind, &findFileData) != 0);

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
			if (params["isAck"] != nullptr && it.bAck != params["isAck"].get<bool>())
				continue;
			if (params["isRecover"] != nullptr && it.bRecover != params["isRecover"].get<bool>())
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
void almTable::update(ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa); //获取报警对应的数据文件
	ALARM_INFO* p = buff.at(ai.getKey());
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
	buff.erase(ai.getKey());
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
			if (aq.isAck != pAi->bAck)
				continue;
		}

		if (aq.filter_isRecover) {
			if (aq.isRecover != pAi->bRecover)
				continue;
		}

		SORT_FLAG sf;
		sf.sFlag = it->second->getSortKey(aq.sortKey);
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
		//分页查询
		json resultObj;
		resultObj["pageNo"] = pageNo;
		resultObj["pageSize"] = pageSize;
		resultObj["pageCount"] = vec.size() / pageSize + (vec.size() % pageSize == 0 ? 0 : 1);
		resultObj["deCount"] = vec.size();
		string jDataSet = "[";
		if (vec.size() > (pageNo - 1) * pageSize)
		{
			//pageNo=1,说明从0开始,往后走pageSize个元素
			//pageNo=2,说明从pageSize开始,往后走pageSize个元素
			for (int i = (pageNo - 1) * pageSize; i < min(pageNo * pageSize, vec.size()); i++)
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


