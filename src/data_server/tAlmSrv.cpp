#include "tAlmSrv.h"
#include <regex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <random>
#include "common.h"
#include <filesystem>


string ALM_TABLE_HEAD_LINE = "uuid,tag,time,type,level,info,detail,isRecover,needRecover,recoverTime,isAck,needAck,multiUnack,ackTime,ackInfo,ackUser\r\n";

almServer almSrv;
COMMON::ThreadPool g_asynCallWorker(1);

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


bool loadAlmDBFile(string strFile, vector<ALARM_INFO*>& almList) {
	string strDBData;
	as_fs::readFile(strFile, strDBData);
	//strDBData = as_charCodec::gb_to_utf8(strDBData); //default utf8 file
	std::vector<LINE_VAL> recLines;
	parse_csv_lines(strDBData.data(), recLines);

	LINE_PARSER lineParser;
	if (recLines.size() >= 1) {
		string tableHeader(recLines[0].p, recLines[0].len);
		vector<string> colNames;
		str::split(colNames, tableHeader, ",");
		for (int i = 0; i < colNames.size(); i++) {
			string name = colNames[i];
			lineParser.m_colNameToColIdx[name] = i;
		}
	}

	for (int i = 1; i < recLines.size(); i++)
	{
		LINE_VAL& lv = recLines.at(i);
		ALARM_INFO* pAi = new ALARM_INFO();
		lineParser.parse(lv.p, lv.len, *pAi);
		almList.push_back(pAi);
	}

	return true;
}

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

	bool fileExist(string pszFileName)
	{
#ifndef _WINXP
#ifdef _WIN32
		std::filesystem::path filePath = DB_STR::utf8_to_utf16(pszFileName);
#else
		std::filesystem::path filePath = pszFileName;
#endif

		if (std::filesystem::exists(filePath)) {
			return true;
		}
		else if (std::filesystem::is_directory(filePath)) {
			return true;
		}
		return  false;
#else
		wstring filePath = charCodec::tds_to_utf16(pszFileName);
		DWORD fileAttributes = GetFileAttributesW(filePath.c_str());
		return (fileAttributes != INVALID_FILE_ATTRIBUTES && !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
#endif
	}
}

almServer::almServer(void)
{
	m_enable = true;
	m_iUpdateCallCount = 0;
	tableCurrent.m_tableType = CURRENT_TABLE;
	m_init = false;
	m_dbFileMode = ONE_FILE_PER_DAY;
}


almServer::~almServer(void)
{
}

void almServer::init(const string dbPath, AsInitParam& asInitParam)
{
	m_initParam = asInitParam;
	m_dbPath = dbPath;
	m_dbPath = as_fs::fixPath(m_dbPath);

	tableCurrent.SetAlarmSrv(this);
	string currFilePath = m_dbPath + "/current.csv";
	tableCurrent.loadFile(currFilePath);
	tableCurrent.initUnAckUnRecover();

	string abpConf = m_dbPath + "/alarmBlockingPlan.json";
	string s;
	as_fs::readFile(abpConf, s);
	if (s != "") {
		json j = json::parse(s);
		m_blockingPlan.fromJson(j);
	}

	m_init = true;
}


bool almServer::isRecover(ALARM_INFO& key) {
	string almKey = key.getKeyUnrecover();
	shared_lock<shared_mutex> lock(tableCurrent.m_csTable);
	auto iter = tableCurrent.unRecoverList.find(almKey);
	if (iter != tableCurrent.unRecoverList.end())
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

	auto func_log = m_initParam.func_log;
	if (func_log)
		func_log("[almSrv]new alarm,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());

	//ai.uuid = ai.time + ai.tag + ai.type + ai.level;

	tableCurrent.add(ai);
	almTable* pHistTable = getHistTable(ai.time);
	pHistTable->add(ai);

	//通知给TDS客户端
	json j = ai.toJson(this);

	if (m_initParam.func_rpcHand_notify && notify)
		m_initParam.func_rpcHand_notify("onAlarmAdd", j);

	//sms send
	//string msg = "报警类型:" + ai.typeLabel + "; ";
	//msg += "报警对象:" + ai.tag + "; ";
	//msg += "报警时间:" + ai.time + "; ";

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
	//if (m_initParam.func_sms_notify) {
	//	m_initParam.func_sms_notify(ai.tag, msg);
	//}
}


int almServer::getCallCount(std::chrono::steady_clock::duration duration, vector<std::chrono::steady_clock::time_point>& latestCall) {
	auto now = std::chrono::steady_clock::now();
	auto cutoff = now - duration;

	auto it = std::lower_bound(
		latestCall.begin(),
		latestCall.end(),
		cutoff,
		[](const auto& time, const auto& cutoff) {
			return time < cutoff;
		});

	return std::distance(it, latestCall.end());
}

int almServer::getLastMinuteCalls(vector<std::chrono::steady_clock::time_point>& latestCall) {
	return getCallCount(std::chrono::minutes(1),latestCall);
}

int almServer::getLastHourCalls(vector<std::chrono::steady_clock::time_point>& latestCall) {
	return getCallCount(std::chrono::hours(1), latestCall);
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
	g_asynCallWorker.enqueue([newStatus, notify] {
		almSrv.UpdateSync(newStatus,notify);
		});
}

void almServer::UpdateSync(ALARM_INFO newStatus, bool notify)
{
	if (!m_init)
		return;
	if (!m_enable)
		return;
	if (m_blockingPlan.isBlocking())
		return;

	if (!newStatus.needRecover) {
		return;
	}

	auto start = std::chrono::high_resolution_clock::now();
	m_latestUpdateCall.push_back(start);
	if (m_latestUpdateCall.size() > 10000) {
		m_latestUpdateCall.erase(m_latestUpdateCall.begin());
	}
	if (newStatus.time == "" || newStatus.time == "0000-00-00 00:00:00")
	{
		TIME st;
		st.setNow();
		newStatus.time = st.toStr(false);
	}

	//the time attr of a status record is always the newest occuring event
	//time attr is not needed to specify a status record 
	string keyUnrecover = newStatus.getKeyUnrecover();

	//deal status change
	bool bTagAlarmStatusChanged = false; 
	bool bNeedRecover = false;
	bool bNeedAdd = false;
	string curLevel;
	ALARM_INFO curStatus;
	tableCurrent.m_csTable.lock();
	auto iter = tableCurrent.unRecoverList.find(keyUnrecover);
	//get current level
	if (iter != tableCurrent.unRecoverList.end()){
		curLevel = iter->second->level;
		if (curLevel != newStatus.level)
		{
			ALARM_INFO* pai = iter->second;
			pai->isRecover = true;
			pai->recoverTime = newStatus.time;
			tableCurrent.unRecoverList.erase(iter);
			curStatus = *pai;
			if (pai->isRecover && pai->isAck) {
				tableCurrent.buff.erase(pai->getKey());
				delete pai;
			}
			tableCurrent.saveFile();
			bNeedRecover = true;
		}
	}
	else{
		curLevel = ALARM_LEVEL::normal;
	}
	tableCurrent.m_csTable.unlock();

	//recover alarm in history table
	if (bNeedRecover) {
		almTable* pTableHist = getHistTable(curStatus.time);
		string almKeyHist = curStatus.getKey();
		pTableHist->m_csTable.lock();
		auto iterHist = pTableHist->buff.find(almKeyHist);
		if (iterHist != pTableHist->buff.end())
		{
			iterHist->second->isRecover = 1;
			iterHist->second->recoverTime = newStatus.time;
			pTableHist->saveFile();
		}
		pTableHist->m_csTable.unlock();

		json j = curStatus.toJson(this);
		if (m_initParam.func_rpcHand_notify && notify) {
			m_initParam.func_rpcHand_notify("onAlarmRecover", j);
		}
	}

	if (curLevel != newStatus.level)
	{
		bTagAlarmStatusChanged = true;
		if(newStatus.level != "normal")
			bNeedAdd = true;
	}

	if (bNeedAdd) {
		addAlarm(newStatus, notify);
	}

	if (bTagAlarmStatusChanged)
	{
		//notify client
		if (notify) {
			json j = newStatus.toJson(this);

			if (m_initParam.func_rpcHand_notify && notify)
				m_initParam.func_rpcHand_notify("onAlarmUpdate", j);
		}
	}
	auto end = std::chrono::high_resolution_clock::now();

	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	float msCost = duration.count() / 1000.0;
	m_latestUpdateCallTimeCost.push_back(msCost);
	if (m_latestUpdateCallTimeCost.size() > 10) {
		m_latestUpdateCallTimeCost.erase(m_latestUpdateCallTimeCost.begin());
	}
	float total = 0;
	for (auto& cost : m_latestUpdateCallTimeCost) {
		total += cost;
	}
	m_lastUpdateCallTimeCostAvg = (float)total / (float)m_latestUpdateCallTimeCost.size();
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

void almServer::Add(ALARM_INFO ai, bool bNotify)
{
	g_asynCallWorker.enqueue([ai, bNotify] {
		almSrv.UpdateSync(ai, bNotify);
		});
}

void almServer::AddSync(ALARM_INFO ai, string& err, bool bNotify)
{
	if (m_blockingPlan.isBlocking()) {
		err = "blocked by blocking plan";
		return;
	}

	auto start = std::chrono::high_resolution_clock::now();
	m_latestAddCall.push_back(start);
	if (m_latestAddCall.size() > 10000) {
		m_latestAddCall.erase(m_latestAddCall.begin());
	}

	if (!ai.needRecover) {
		ai.isRecover = true;
	}

	bool bNeedAdd = false;
	if (ai.needRecover) {
		string alarmKeyStr = ai.getKeyUnrecover();
		tableCurrent.m_csTable.lock();
		auto iter = tableCurrent.unRecoverList.find(alarmKeyStr);
		if (iter == tableCurrent.unRecoverList.end()) {
			bNeedAdd = true;
		}
		tableCurrent.m_csTable.unlock();
	}
	else { // state less alarm
		bNeedAdd = true;
	}

	if (bNeedAdd) {
		addAlarm(ai, bNotify);
	}
	else {
		err = "unrecover alarm with the same alarm key already existed,add fail";
	}	

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	float msCost = duration.count() / 1000.0;
	m_latestAddCallTimeCost.push_back(msCost);
	if (m_latestAddCallTimeCost.size() > 10) {
		m_latestAddCallTimeCost.erase(m_latestAddCallTimeCost.begin());
	}
	float total = 0;
	for (auto& cost : m_latestAddCallTimeCost) {
		total += cost;
	}
	m_lastAddCallTimeCostAvg = total / (float)m_latestAddCallTimeCost.size();
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

	string err;
	AddSync(ai,err, true);
	if (err == "") {
		resp.result = ai.toJsonStr(this);
	}
	else {
		json j = err;
		resp.error = j.dump();
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
	j.erase("time");// time is invalid in recover method
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
		UpdateSync(ai);
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
	js["latestUpdateCallTimeCost"] = m_lastUpdateCallTimeCostAvg;
	js["latestAddCallTimeCost"] = m_lastAddCallTimeCostAvg;
	js["UpdateCallLastMinute"] = getLastMinuteCalls(m_latestUpdateCall);
	js["UpdateCallLastHour"] = getLastHourCalls(m_latestUpdateCall);
	js["AddCallLastMinute"] = getLastMinuteCalls(m_latestAddCall);
	js["AddCallLastHour"] = getLastHourCalls(m_latestAddCall);
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

//基于  tag+ time+ type 匹配记录 
void almServer::rpc_acknowledge(json& params, RPC_RESP& resp, RPC_SESSION session) {
	if (!params.contains("time")) {
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定time字段");
		resp.error = error;
		return;
	}
	
	if (!params.contains("tag")) {
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定tag字段");
		resp.error = error;
		return;
	}

	if (!params.contains("type")) {
		string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定type字段");
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

	string user = session.user;
	string info;
	if (params.contains("ackInfo"))
		info = params["ackInfo"];

	ALARM_KEY queryKey;
	queryKey.time = params["time"];
	queryKey.tag = params["tag"];
	queryKey.type = params["type"];
	TIME t; t.setNow();
	string ackTime = t.toStr(true);
	ALARM_INFO ai;
	{
		unique_lock<shared_mutex> lock(tableCurrent.m_csTable);
		string almKey = queryKey.getKey();
		string almkeyAnsi = str::utf8_to_gb(almKey);
		for (auto& item: tableCurrent.unAckList)
		{
			string temp = str::utf8_to_gb(item.first);
		}
		auto iter = tableCurrent.unAckList.find(almKey);
		if (iter != tableCurrent.unAckList.end())
		{
			iter->second->isAck = 1;
			iter->second->ackUser = session.user;
			iter->second->ackInfo = info;
			iter->second->ackTime = ackTime;
			ai = *iter->second;
			if (iter->second->isRecover && iter->second->isAck) {
				tableCurrent.buff.erase(almKey);
				delete iter->second;
			}
			tableCurrent.saveFile();
			tableCurrent.unAckList.erase(iter);
		}
		else {
			string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "alarm not found in unack list");
			resp.error = error;
			return;
		}
	}
	
	almTable* pTableHist = getHistTable(queryKey.time);
	{
		unique_lock<shared_mutex> lock(pTableHist->m_csTable);
		auto iter = pTableHist->buff.find(queryKey.getKey());
		if (iter != pTableHist->buff.end())
		{
			iter->second->isAck = 1;
			iter->second->ackUser = session.user;
			iter->second->ackInfo = info;
			iter->second->ackTime = ackTime;
			pTableHist->saveFile();
		}
		else {
			string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "alarm not found in history");
			resp.error = error;
			return;
		}
	}
	
	json j = ai.toJson(this);
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

void almServer::rpc_convertDBMode(json& params, RPC_RESP& resp, RPC_SESSION session)
{
	ALM_SELECTOR almSel;
	if (!parseAlmSelector(params, session, almSel)) {
		json jErr = almSel.parseError;
		resp.error = jErr.dump();
		return;
	}

	string srcMode = params["src"];
	string desMode = params["des"];
	if (srcMode == "" && desMode == "") {
		json jErr = "srcMode and desMode must be specified";
		resp.error = jErr.dump();
	}

	vector<string> timeKey;
	DB_FILE_MODE  dfm_srcMode;
	if (srcMode == "monthly") {
		getDBFileTimeKey_monthly(almSel, timeKey);
		dfm_srcMode = DB_FILE_MODE::ONE_FILE_PER_MONTH;
	}
	else if (srcMode == "daily") {
		getDBFileTimeKey_daily(almSel, timeKey);
		dfm_srcMode = DB_FILE_MODE::ONE_FILE_PER_DAY;
	}
	DB_FILE_MODE  dfm_desMode;
	if (desMode == "monthly") {
		dfm_desMode = DB_FILE_MODE::ONE_FILE_PER_MONTH;
	}
	else if (desMode == "daily") {
		dfm_desMode = DB_FILE_MODE::ONE_FILE_PER_DAY;
	}

	vector<ALARM_INFO*> almList;
	for (auto& time : timeKey) {
		string path = getFilePath(time, ALM_TABLE_TYPE::HISTORY_TABLE, dfm_srcMode);
		loadAlmDBFile(path, almList);
	}

	map<string, vector<ALARM_INFO*>> desFileData;
	for (auto& ai : almList) {
		string tk;
		if (desMode == "monthly") {
			tk = ai->time.substr(0, 7);
		}
		else if (desMode == "daily") {
			tk = ai->time.substr(0, 10);
		}
		
		auto iter = desFileData.find(tk);
		if (iter == desFileData.end()) {
			vector<ALARM_INFO*> vec;
			desFileData[tk] = vec;
		}
		iter = desFileData.find(tk);
		iter->second.push_back(ai);
	}

	for (auto& fileData : desFileData) {
		string path = getFilePath(fileData.first, ALM_TABLE_TYPE::HISTORY_TABLE, dfm_desMode);
		string data = ALM_TABLE_HEAD_LINE;
		for (auto& almInfo : fileData.second) {
			data += almInfo->toCSVLine();
		}
		as_fs::writeFile(path, data);
	}

	resp.result = RPC_OK;
}

//params ：对应ai那个结构
//返回负数  失败, 非负数 成功：0 未通过，1通过
//按原理，会马上恢复掉，仅恢复掉的允许审核 前端保证
int almServer::rpc_approve(json& params, RPC_RESP& resp, RPC_SESSION session) {
	//int nRet = -1;
	//if (params.contains("uuid") == false && (params.contains("tag") == false)) {
	//	string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定uuid或tag字段");
	//	resp.error = error;
	//	return nRet;
	//}
	//if (false == params["isRecover"].get<bool>()) {
	//	resp.error = "尚未恢复";
	//	nRet = -2;
	//	return nRet;
	//}
	//if (params.contains("tag")) {
	//	string rootTag;
	//	if (params.contains("rootTag")) {
	//		rootTag = params["rootTag"];
	//	}
	//	string tag = params["tag"];
	//	tag = TAG::addRoot(tag, rootTag);

	//	//用户位号转系统位号
	//	tag = TAG::addRoot(tag, session.org);
	//	params["tag"] = tag;
	//}

	//ALARM_INFO ai;
	//if (tableCurrent.query(params, ai))
	//{
	//	string user = session.user;
	//	string info;
	//	if (params.contains("ackInfo"))
	//		info = params["ackInfo"];
	//	if (info == "通过") {
	//		nRet = 1;
	//	}
	//	ai.isAck = 1;
	//	ai.ackUser = session.user;
	//	ai.ackInfo = info;
	//	TIME t; t.setNow();
	//	ai.ackTime = t.toStr();
	//	if (ai.isAck && ai.isRecover)//删除已消除已确认报警
	//	{
	//		tableCurrent.remove(ai);
	//	}
	//	else
	//		tableCurrent.update(ai);
	//}
	//else
	//{
	//	string error = makeRPCError(RPC_ERROR_CODE::ALM_alarmEventNotFound, "未找到报警事件");
	//	resp.error = error;
	//	return nRet;
	//}

	//if (params.contains("time") == false)//用时间对应历史表文件  时间来自未确定文件.
	//	params["time"] = ai.time;
	//almTable* pTableHist = getHistTable(ai.time);
	//if (pTableHist->query(params, ai))
	//{
	//	string user = session.user;
	//	string info;
	//	if (params.contains("ackInfo"))
	//		info = params["ackInfo"];

	//	ai.isAck = 1;
	//	ai.ackUser = session.user;
	//	ai.ackInfo = info;
	//	TIME t; t.setNow();
	//	ai.ackTime = t.toStr();
	//	pTableHist->update(ai);
	//}

	//json j = ai.toJson(this);

	////rpcSrv.notify("onAlarmAck", j);  
	//if (m_initParam.func_rpcHand_notify)
	//	m_initParam.func_rpcHand_notify("onAlarmAck", j);

	//resp.result = "\"ok\"";
	//return nRet;
	return 0;
}

void almServer::rpc_getCurrent(json params, RPC_RESP& resp, RPC_SESSION session)
{
	if (!m_enable){
		json jRlt = json::array();
		resp.result = jRlt.dump();
		return;
	}

	ALM_SELECTOR almSel;
	almSel.timeSel.enable = false; //current alarm do not need time selector
	if (!parseAlmSelector(params, session, almSel)) {
		json jErr = almSel.parseError;
		resp.error = jErr.dump();
		return;
	}

	vector<ALARM_INFO*> vecAlarm;
	for (auto i = tableCurrent.buff.rbegin(); i != tableCurrent.buff.rend();i++) {
		if (isSelected(i->second, almSel)) {
			vecAlarm.push_back(i->second);
		}
	}
	getPagedDateSet(vecAlarm, almSel, resp.result);
}

void almServer::rpc_getUnRecover(json params, RPC_RESP& resp, RPC_SESSION session)
{
	params["isRecover"] = false; 
	rpc_getCurrent(params, resp, session);
}

void almServer::rpc_getUnack(json params, RPC_RESP& resp, RPC_SESSION session)
{
	params["isAck"] = false;
	rpc_getCurrent(params, resp, session);
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

bool almServer::parseAlmSelector(json& params, RPC_SESSION& session,ALM_SELECTOR& almSel)
{
	almSel.org = session.org;

	//root tag  to system tag
	string rootTag = "";
	if (params["rootTag"].is_string()) {
		rootTag = params["rootTag"].get<string>();
	}
	rootTag = TAG::addRoot(rootTag, session.org);
	params["rootTag"] = rootTag;
	if (!params.contains("tag")) {
		params["tag"] = "*";
	}
	string sSel,error;
	sSel = params.dump();
	db.parseDESelector(sSel, almSel, error);
	if (error != "") {
		almSel.parseError = error;
		return false;
	}


	//keyword search
	if (params["keyword"].is_string()) {
		vector<string> kwtemp;
		string kw = params["keyword"];
		kw = str::trim(kw);
		if (kw != "") {
			str::split(kwtemp, kw, " ");
			for (auto& i : kwtemp) {
				string s = str::trim(i);
				if (s != "") {
					almSel.keywords.push_back(s);
				}
			}
		}
	}

	//此代码忘了干什么用的了
	//bool getTypeTag = false;
	//if (params.contains("getTypeTag") && params["getTypeTag"].is_boolean()) {
	//	getTypeTag = true;
	//}

	almSel.type.clear();
	if (params.contains("type")) {
		if (params["type"].is_array()) {
			for (int i = 0; i < params["type"].size(); i++) {
				if (params["type"][i].is_string()) {
					almSel.type.push_back(params["type"][i].get<string>());
				}
			}
		}
		else if (params["type"].is_string()) {
			str::split(almSel.type, params["type"].get<string>(), ",");
		}
	}

	almSel.level.clear();
	if (params.contains("level")) {
		if (params["level"].is_array()) {
			for (int i = 0; i < params["level"].size(); i++) {
				if (params["level"][i].is_string()) {
					almSel.level.push_back(params["level"][i].get<string>());
				}
			}
		}
		else if (params["level"].is_string()) {
			vector<string> vecLevel;
			str::split(vecLevel, params["level"].get<string>(), ",");
			almSel.level = vecLevel;
		}
	}

	almSel.filter_isRecover = false;
	almSel.isRecover = false;
	if (params.contains("isRecover")) {
		almSel.filter_isRecover = true;
		almSel.isRecover = params["isRecover"].get<bool>();
	}

	almSel.filter_isAck = false;
	almSel.isAck = false;
	if (params.contains("isAck")) {
		almSel.filter_isAck = true;
		almSel.isAck = params["isAck"].get<bool>();
	}

	return true;
}

void almServer::getDBFileTimeKey_monthly(ALM_SELECTOR& almSel, vector<string>& timeKey) {
	int startYear = almSel.timeSel.atomSelList[0].stStart.wYear;
	int startYearStartMonth = almSel.timeSel.atomSelList[0].stStart.wMonth;
	int endYear = almSel.timeSel.atomSelList[0].stEnd.wYear;
	int endYearEndMonth = almSel.timeSel.atomSelList[0].stEnd.wMonth;
	//defaut time desending. only db file in time range is loaded
	for (int iYear = endYear; iYear >= startYear; iYear--) {
		int startMonth = 1;
		if (iYear == startYear)
			startMonth = startYearStartMonth;
		int endMonth = 12;
		if (iYear == endYear)
			endMonth = endYearEndMonth;

		for (int iMonth = endMonth; iMonth >= startMonth; iMonth--) {
			string time = str::format("%04d-%02d", iYear, iMonth);
			timeKey.push_back(time);
		}
	}
}

void almServer::getDBFileTimeKey_daily(ALM_SELECTOR& almSel, vector<string>& timeKey) {
	time_t startDate = almSel.timeSel.atomSelList[0].stStart.toUnixTime();
	time_t endDate = almSel.timeSel.atomSelList[0].stEnd.toUnixTime();
	time_t loadDate = endDate;
	for (; loadDate >= startDate; loadDate -= 24 * 60 * 60)
	{
		DB_TIME t;
		t.fromUnixTime(loadDate);
		string time = str::format("%04d-%02d-%02d", t.wYear, t.wMonth, t.wDay);
		timeKey.push_back(time);
	}
}

void almServer::getDBFileTimeKey(ALM_SELECTOR& almSel, vector<string>& timeKey) {
	if (m_dbFileMode == ONE_FILE_PER_MONTH) {
		getDBFileTimeKey_monthly(almSel, timeKey);
	}
	else if (m_dbFileMode == ONE_FILE_PER_DAY) {
		getDBFileTimeKey_daily(almSel, timeKey);
	}
}

bool almServer::isSelected(ALARM_INFO* ai, ALM_SELECTOR& almSel) {
	if (almSel.org != "") {
		if (ai->tag.find(almSel.org) != 0) {
			return false;
		}
	}

	if (almSel.keywords.size() > 0) {
		vector<bool> matchRlt;
		for (int i = 0; i < almSel.keywords.size(); i++) {
			string& kw = almSel.keywords[i];
			bool match = false;
			if (ai->tag.find(kw) != string::npos) {
				match = true;
			}
			else if (ai->time.find(kw) != string::npos) {
				match = true;
			}
			else if (ai->type.find(kw) != string::npos) {
				match = true;
			}
			else if (ai->level.find(kw) != string::npos) {
				match = true;
			}
			else if (ai->desc.find(kw) != string::npos) {
				match = true;
			}
			else if (ai->detail.find(kw) != string::npos) {
				match = true;
			}
			matchRlt.push_back(match);
		}

		bool bMatchRlt = true;
		for (int i = 0; i < matchRlt.size(); i++) {
			if (matchRlt[i] == false) {
				bMatchRlt = false;
			}
		}

		if (bMatchRlt == false) {
			return false;
		}
	}

	if (!almSel.tagSel.match(ai->tag)) {
		return false;
	}
	if (!almSel.timeSel.Match(ai->time)) {
		return false;
	}

	bool bTypeMatch = false;
	if (almSel.type.size() == 0)
		bTypeMatch = true;
	else {
		for (auto& one : almSel.type) {
			if (generalMatch(one, ai->type)) {
				bTypeMatch = true;
				break;
			}
		}
	}
	if (!bTypeMatch)
		return false;

	bool bLevelMatch = false;
	if (almSel.level.size() == 0)
		bLevelMatch = true;
	else {
		for (auto& one : almSel.level) {
			if (generalMatch(one, ai->level)) {
				bLevelMatch = true;
				break;
			}
		}
	}
	if (!bLevelMatch)
		return false;

	if (almSel.filter_isRecover) {
		if (almSel.isRecover != ai->isRecover) {
			return false;
		}
	}

	if (almSel.filter_isAck) {
		if (almSel.isAck != ai->isAck) {
			return false;
		}
	}

	return true;
}

void almServer::loadHistAlarm(vector<ALARM_INFO*>& almList,ALM_SELECTOR& almSel,RPC_SESSION session) {
	vector<string> timeKey;
	getDBFileTimeKey(almSel, timeKey);

	for(auto& time:timeKey){
		almTable* pTableHist = getHistTable(time);
		std::shared_lock<shared_mutex> lock(pTableHist->m_csTable);
		for (auto it = pTableHist->buff.rbegin(); it != pTableHist->buff.rend(); it++) {
			if (isSelected(it->second, almSel)) {
				almList.push_back(it->second); //time desending
			}
		}
	}
}
void  almServer::getPagedDateSet(vector<ALARM_INFO*> almList,ALM_SELECTOR& almSel, string& dataSet) {
	//return page 1 if pageNo no specified
	if (almSel.pageSize > 0)
	{
		yyjson_mut_doc* yyDoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* resultObj = yyjson_mut_obj(yyDoc);
		yyjson_mut_val* jDataSet = yyjson_mut_arr(yyDoc);
		yyjson_mut_obj_add_int(yyDoc, resultObj, "pageNo", almSel.pageNo);
		yyjson_mut_obj_add_int(yyDoc, resultObj, "pageSize", almSel.pageSize);
		yyjson_mut_obj_add_int(yyDoc, resultObj, "pageCount", almList.size() / almSel.pageSize + (almList.size() % almSel.pageSize == 0 ? 0 : 1));
		yyjson_mut_obj_add_int(yyDoc, resultObj, "deCount", almList.size());
		if (almList.size() > (almSel.pageNo - 1) * almSel.pageSize)
		{
			int curPageSize = almSel.pageSize;
			int leftPageCount = almList.size() - (almSel.pageNo - 1) * almSel.pageSize;
			if (leftPageCount < curPageSize) {
				curPageSize = leftPageCount;
			}

			for (int i = 0; i < curPageSize; i++)
			{
				int almIdx = i + (almSel.pageNo - 1) * almSel.pageSize;
				auto it = almList[almIdx];
				yyjson_mut_val* j = nullptr;
				it->toJson(this, almSel.tagSel.m_rootTag, j, yyDoc);
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
		yyjson_mut_doc* yyDoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* jDataSet = yyjson_mut_arr(yyDoc);
		for (int i = 0; i < almList.size(); i++)
		{
			auto it = almList[i];
			yyjson_mut_val* j = nullptr;
			it->toJson(this, almSel.tagSel.m_rootTag, j, yyDoc);
			yyjson_mut_arr_append(jDataSet, j);
		}
		size_t len;
		dataSet = yyjson_mut_val_write(jDataSet, 0, &len);
		yyjson_mut_doc_free(yyDoc);
	}
}

void almServer::rpc_getHistory(json params, RPC_RESP& resp, RPC_SESSION session)
{
	ALM_SELECTOR almSel;
	parseAlmSelector(params, session, almSel);

	vector<ALARM_INFO*> almList;
	loadHistAlarm(almList, almSel, session);

	getPagedDateSet(almList, almSel, resp.result);
}
#endif

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
		string str = ai.toCSVLine();
		data += str;
	}
	as_fs::createFolderOfPath(strFile);
	as_fs::writeFile(strFile, data);
}

void almTable::appendFile(string strFile, ALARM_INFO* pNew)
{
	string line = pNew->toCSVLine();
	fs::appendFile(strFile, line);
}

string almServer::getFilePath(int y, int m,int day, ALM_TABLE_TYPE tableType, DB_FILE_MODE fileMode) {
	string p;
	if (fileMode == ONE_FILE_PER_MONTH)
	{
		string strYM = str::format("%04d%02d", y, m);
		p = m_dbPath + "/history_" + strYM + ".csv";
	}
	else if (fileMode == ONE_FILE_PER_DAY) {
		string s = str::format("%04d%02d/%02d", y, m,day);
		p = m_dbPath + "/" + s + ".csv";
	}
	else
	{
		p = m_dbPath + "/history.csv";
	}
	return p;
}

string almServer::getFilePath(string time, ALM_TABLE_TYPE tableType, DB_FILE_MODE fileMode) {
	if (tableType == CURRENT_TABLE || time == "")
		return m_dbPath + "/current.csv";
	else if (time != "")
	{
		TIME st;
		st.fromStr(time);
		int y, m;
		y = st.wYear;
		m = st.wMonth;
		return getFilePath(y, m,st.wDay,tableType, fileMode);
	}
	else
	{
		return m_dbPath + "/history.csv";
	}
}

void almTable::loadFile(string strFile)
{
	//already loaded
	if (buffFilePath == strFile)
		return;

	//load new path db file
	freeBuff(buff);
	buffFilePath = strFile;

	//init db file when not exist
	if (!as_fs::fileExist(strFile)) {
		as_fs::writeFile(strFile, ALM_TABLE_HEAD_LINE);
	}
	//load db file
	else {
		string strDBData;
		bool ret = as_fs::readFile(strFile, strDBData);
#ifdef _WIN32
		if (!ret) {
			MessageBox(NULL, "read current.csv fail",NULL,MB_OK);
			exit(0);
		}
#endif
		//strDBData = as_charCodec::gb_to_utf8(strDBData); //default utf8 file
		std::vector<LINE_VAL> recLines;
		parse_csv_lines(strDBData.data(), recLines);

		LINE_PARSER& lineParser = m_lineParser;
		if (recLines.size() >= 1) {
			string tableHeader(recLines[0].p, recLines[0].len);
			vector<string> colNames;
			str::split(colNames, tableHeader, ",");
			for (int i = 0; i < colNames.size(); i++) {
				string name = colNames[i];
				lineParser.m_colNameToColIdx[name] = i;
			}
		}

		for (int i = 1; i < recLines.size(); i++)
		{
			LINE_VAL& lv = recLines.at(i);
			ALARM_INFO* pAi = new ALARM_INFO();
			lineParser.parse(lv.p, lv.len, *pAi);
			buff[pAi->getKey()] = pAi;
		}
	}
}

void almTable::saveFile()
{
	saveFile(buffFilePath, buff);
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
	yyjson_mut_obj_add_bool(doc, jVal, "multiUnack", info->multiUnack);
}

int LINE_PARSER::getColIdxByColName(string colName) {
	map<string, int>::iterator iter = m_colNameToColIdx.find(colName);
	if (iter != m_colNameToColIdx.end()) {
		int idx = m_colNameToColIdx[colName];
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

void LINE_PARSER::parse(const char* line,int lineLen, ALARM_INFO& ai)
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
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("uuid"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("tag"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("time"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("type"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("level"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("info"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("detail"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("isRecover"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("needRecover"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("recoverTime"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("isAck"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("needAck"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("multiUnack"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("ackTime"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("ackInfo"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("ackUser"); loadIdx++;
		m_loadIdxToColIdx[loadIdx] = getColIdxByColName("picUrl"); loadIdx++;
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

string ALARM_INFO::toCSVLine()
{
	ALARM_INFO& info = *this;
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
	string histTableName;
	if (m_dbFileMode == DB_FILE_MODE::ONE_FILE_PER_MONTH) {
		histTableName = time.substr(0, 7);
	}
	else if (m_dbFileMode == DB_FILE_MODE::ONE_FILE_PER_DAY) {
		histTableName = time.substr(0, 10);
	}
	else {
		return nullptr;
	}

	
	std::unique_lock<mutex> lock(m_csTableHistList);
	map<string, almTable*>::iterator iter = tableHist.find(histTableName);
	if (iter == tableHist.end()) {
		almTable* p = new almTable();
		p->dbFileMode = m_dbFileMode;
		p->SetAlarmSrv(this);
		p->m_tableType = HISTORY_TABLE;
		p->loadFile(getFilePath(time, HISTORY_TABLE, m_dbFileMode));
		tableHist[time] = p;
		return p;
	}
	else {
		return iter->second;
	}
}

void almTable::add(ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	ALARM_INFO* pNew = new ALARM_INFO();
	*pNew = ai;
	string almKey = ai.getKey();
	buff[almKey] = pNew;
	if (m_tableType == ALM_TABLE_TYPE::CURRENT_TABLE) {
		if(ai.needRecover){
			string unrecoverKey = ai.getKeyUnrecover();
			unRecoverList[unrecoverKey] = pNew;
		}
		if (ai.needAck) {
			unAckList[almKey] = pNew;
		}
	}
	appendFile(buffFilePath, pNew);
}

void almTable::acknowledge(ALARM_INFO& ai, bool remove)
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

void almTable::initUnAckUnRecover()
{
	for (auto& i : buff) {
		if (i.second->isAck == false) {
			unAckList[i.first] = i.second;
		}

		if (i.second->isRecover == false) {
			unRecoverList[i.second->getKeyUnrecover()] = i.second;
		}
	}
}

void almTable::acknowledge(ALARM_INFO& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	auto iter = buff.find(ai.getKey());
	if (iter != buff.end()) {
		ALARM_INFO* p = iter->second;
		p->ackUser = ai.ackUser;
		p->ackInfo = ai.ackInfo;
		p->ackTime = ai.ackTime;
		saveFile(buffFilePath, buff);
	}
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

//vector<ALARM_INFO*> almTable::query(json querier)
//{
//	std::unique_lock<shared_mutex> lock(m_csTable);
//	vector<ALARM_INFO*> dataSet;
//	loadFile(m_pAlmSrv->getFilePath("",m_tableType,dbFileMode));
//	ALARM_QUERY aq = parseQuerier(querier);
//
//	TIME_SELECTOR ts;
//	if (aq.filter_time) {
//		ts.init(aq.time);
//	}
//	map<SORT_FLAG, ALARM_INFO*> deList_Sort;
//	for (map<string, ALARM_INFO*>::iterator it = buff.begin(); it != buff.end(); it++) {
//		//if (aq.filter_user && !userMng.checkTagPermission(aq.user, it->second->tag))
//			//continue;
//		if (aq.filter_user) {
//			if (m_pAlmSrv->m_initParam.func_usrMng_checkTagPermission) {
//				if (m_pAlmSrv->m_initParam.func_usrMng_checkTagPermission(aq.user, it->second->tag)==false) {
//					continue;
//				}
//			}
//		}
//
//		ALARM_INFO* pAi = it->second;
//
//		if (aq.filter_rootTag && pAi->tag.find(aq.rootTag) == string::npos)
//			continue;
//
//		//记录里存的绝对tag。 单独的tag是相对于roottag的。
//		if (aq.filter_tag) {
//			bool bMatch = false;
//			for (const auto& oneTag : aq.vecTag) {
//				string zong_tag = oneTag;
//				if (aq.rootTag != "") {
//					zong_tag = aq.rootTag + "." + oneTag;
//				}
//				if (matchTag(zong_tag, pAi->tag)) {
//					bMatch = true;
//					break;
//				}
//			}
//			if (!bMatch)
//				continue;
//		}
//
//		if (aq.filter_time) {
//			if (false == ts.Match(pAi->time))
//				continue;
//		}
//		if (aq.filter_type) {
//			bool bMatch = false;
//			for (const auto& one : aq.vecType) {
//				if (generalMatch(one, pAi->type)) {
//					bMatch = true;
//					break;
//				}
//			}
//			if (!bMatch)
//				continue;
//		}
//		if (aq.filter_level) {
//			bool bMatch = false;
//			for (const auto& one : aq.vecLevel) {
//				if (one == pAi->level) {
//					bMatch = true;
//					break;
//				}
//			}
//			if (!bMatch)
//				continue;
//		}
//		if (aq.filter_isAck) {
//			if (aq.isAck != pAi->isAck)
//				continue;
//		}
//
//		if (aq.filter_isRecover) {
//			if (aq.isRecover != pAi->isRecover)
//				continue;
//		}
//
//		SORT_FLAG sf;
//		it->second->getSortKey(aq.sortKey,sf.sFlag);
//		deList_Sort[sf] = it->second;
//	}
//
//	if (aq.ascendingSort)
//	{
//		for (auto it = deList_Sort.begin(); it != deList_Sort.end(); ++it) {
//			dataSet.push_back(it->second);
//		}
//	}
//	else
//	{
//		for (auto it = deList_Sort.rbegin(); it != deList_Sort.rend(); ++it) {
//			dataSet.push_back(it->second);
//		}
//	}
//	return dataSet;
//}

void almTable::SetAlarmSrv(almServer* pSrv)
{
	m_pAlmSrv = pSrv;
}


