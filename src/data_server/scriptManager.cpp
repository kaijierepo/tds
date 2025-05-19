//#include "pch.h"
#include "scriptManager.h"
#include "scriptEngine.h"
#include "scriptFunc_tds.h"
#include "logger.h"
#include "jerryscript-port.h"

#ifdef TDS
#include "ioSrv.h"
#endif

#include "common.h"

ScriptManager scriptManager;

void scriptManager_logImp(string log, ScriptEngine* pEngine, bool logToHost) {
	pEngine->m_vecOutput.push_back(log);
	if (logToHost) {
		string l = "[脚本日志]" + log;
		LOG(l);
	}
}

bool initGlobalFunc(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc)
{
	initScriptFunc(global_object, m_vecGlobalFunc);
#ifdef TDS
	initScripFunc_tds(global_object, m_vecGlobalFunc);
#endif
	return true;
}

void scriptThread(ScriptManager* p)
{
#ifdef ENABLE_JERRY_SCRIPT
	p->loopExe();
#endif
}

ScriptManager::ScriptManager()
{
	loopRunning = false;
	m_bRun = false;
}

bool ScriptManager::init()
{
	unique_lock<mutex> lock(m_csScripts);

	string sScriptList;
	if (fs::readFile(m_confPath + "/scripts/list.json", sScriptList))
	{
		json jSL = json::parse(sScriptList);
		for (int i = 0; i < jSL.size(); i++) {
			json jInfo = jSL[i];
			SCRIPT_INFO si;
			si.lastExe = timeopt::now();
			si.fromJson(jInfo);
			string scriptFilePath = m_confPath + "/scripts/" + si.name + ".js";
			string scriptData;
			if (fs::readFile(scriptFilePath, scriptData)) {
				si.script = scriptData;
				m_mapScripts[si.name] = si;
			}
			else {
				continue;
				LOG("[error]加载脚本文件失败," + scriptFilePath);
			}

		}
	}


	
	return true;
}

bool ScriptManager::run()
{
	//该配置一般用于临时关闭脚本调用，方便调试打断点
#ifdef TDS
	if (tds->conf->getInt("enableScript", 1) == 0) {
		return true;
}
#endif



	m_bRun = true;
	thread t(scriptThread, this);
	t.detach();
	return false;
}

void ScriptManager::setConfPath(const string& conf)
{
	m_confPath = conf;
}

bool ScriptManager::hasScripts()
{
	{
		unique_lock<mutex> lock(m_csScripts);
		if (m_mapScripts.size() > 0)
			return true;
	}
	{
		unique_lock<mutex> lock(m_csExpScripts);
		if (m_vecVarExpScripts.size() > 0)
			return true;
	}
	return false;
}

void ScriptManager::updateVarExpScript(vector<SCRIPT_INFO>& varExpScripts)
{
	unique_lock<mutex> lock(m_csExpScripts);
	m_vecVarExpScripts.clear();
	m_vecVarExpScripts = varExpScripts;
}

void scriptThreadTmp(string scriptName, string callerObjTag)
{
#ifdef ENABLE_JERRY_SCRIPT
	SCRIPT_INFO si;
	if (scriptManager.getScript(scriptName, si)) {
		si.callerObjTag = callerObjTag;

		ScriptEngine se;
		se.m_logImp = scriptManager_logImp;

		se.m_initGlobalFunc = initGlobalFunc;

		
		se.m_tagContext = si.getContextTag();
		se.runScript(si.script, si.lastModifyUser);
		si.lastExe = timeopt::now();
	}
#endif
}

bool ScriptManager::handleRpc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	bool bHandled = true;

	if (method == "getScriptMngerStatus") {
		rpc_getScriptMngStatus(params, rpcResp, session);
	}
#ifdef ENABLE_JERRY_SCRIPT
	else if (method == "runScript")
	{
		rpc_runScript(params, rpcResp, session);
	}
	else if (method == "getScriptList")
	{
		rpc_getScriptList(params, rpcResp, session);
	}
	else if (method == "getScriptFile")
	{
		rpc_getScript(params, rpcResp, session);
	}
	else if (method == "deleteScriptFile") {
		rpc_deleteScript(params, rpcResp, session);
	}
	else if (method == "setScriptFile")
	{
		rpc_setScript(params, rpcResp, session);
	}
#endif
	else
	{
		bHandled = false;
	}

	return bHandled;
}

bool ScriptManager::runScriptFileAsyn(string scriptName,string tagThis)
{
#ifdef ENABLE_JERRY_SCRIPT
	thread t(scriptThreadTmp, scriptName,tagThis);
	t.detach();
#endif
	return false;
}

#ifdef ENABLE_JERRY_SCRIPT

void scriptThread1(ScriptManager* p)
{
	p->loopExe();
}


bool ScriptManager::getScript(string name, SCRIPT_INFO& sInfo) {
#ifdef ENABLE_JERRY_SCRIPT
	unique_lock<mutex> lock(scriptManager.m_csScripts);
	for (auto& i : scriptManager.m_mapScripts) {
		SCRIPT_INFO& si = i.second;
		if (si.name == name) {
			sInfo = si;
			return true;
		}
	}
	return false;
#endif
}



bool ScriptManager::rpc_runScript(json& params,RPC_RESP& rpcResp,RPC_SESSION session)
{
	//直接执行脚本
	if (params["script"] != nullptr) {
		string s = params["script"];

		if (params.contains("envVarScript")) {
			string sEnvVar = params["envVarScript"];
			if (sEnvVar != "") {
				s = sEnvVar + "\n" + s;
			}
		}

		SCRIPT_INFO si;
		si.org = session.org;
		if (params.contains("rootTag"))
			si.rootTag = params["rootTag"];

		if (params.contains("devAddr"))
			si.devAddr = params["devAddr"];

		bool getExpRet = false; //是否获取表达式的返回值。测试表达式时使用
		if (params.contains("getExpRet")) {
			getExpRet = params["getExpRet"].get<bool>();
		}

		ScriptEngine se;
		se.m_logImp = scriptManager_logImp;
		se.m_initGlobalFunc = initGlobalFunc;
		se.m_tagContext = si.getContextTag();
		if (si.devAddr != "") {
			ioDev* p = nullptr;
#ifdef TDS
			p = ioSrv.getIODevByIPPort(si.devAddr);
			if (!p)
				p = ioSrv.getIODevById(si.devAddr);
#endif
			if (p) {
				se.m_ioDevThis = p;
				
#ifdef TDS
				se.m_initIODevFunc = initIODevFunc;
#endif
			}
			else {
				json jError = "specified ioDev not found";
				rpcResp.error = jError.dump();
			}
		}

		se.runScript(s,session.user);

		json jOutput = json::array();

		if(getExpRet)
		jOutput.push_back("计算结果=" + se.m_sEvalRet);

		for (int i = 0; i < se.m_vecOutput.size(); i++) {
			string sline = se.m_vecOutput[i];
			jOutput.push_back(sline);
		}
		
		rpcResp.result = jOutput.dump();
	}
	//执行保存的脚本文件
	else {
		string scriptName = params["name"].get<string>();
		SCRIPT_INFO si;
		if (getScript(scriptName, si)) {
			ScriptEngine se;
			se.m_logImp = scriptManager_logImp;
			
			se.m_initGlobalFunc = initGlobalFunc;

			se.m_tagContext = si.getContextTag();
			if (se.runScript(si.script, si.lastModifyUser)) {
				rpcResp.result = "\"ok\"";
			}
			else {
				json jError = "run fail";
				rpcResp.error = jError.dump();
			}
			si.lastExe = timeopt::now();
		}
		else {
			json jError = "specified script not found";
			rpcResp.error = jError.dump();
		}
	}

	return true;
}

bool ScriptManager::rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string type = "global";
	if (params.contains("type")) {
		type = params["type"];
	}
	bool getStatus = false;
	if (params.contains("getStatus")) {
		getStatus = params["getStatus"].get<bool>();
	}

	if (type == "global") {
		unique_lock<mutex> lock(m_csScripts);
		json j = json::array();
		for (auto& i : m_mapScripts) {
			SCRIPT_INFO& si = i.second;
			if (si.org.find(session.org) != 0)
				continue;
			json jSi;
			si.toJson(jSi,getStatus);
			j.push_back(jSi);
		}
		rpcResp.result = j.dump(2);
	}
	else if (type == "exp") {
		unique_lock<mutex> lock(m_csExpScripts);
		json j = json::array();
		for (auto& i : m_vecVarExpScripts) {
			SCRIPT_INFO& si = i;
			if (si.org.find(session.org) != 0)
				continue;
			json jSi;
			si.toJson(jSi,getStatus);
			j.push_back(jSi);
		}
		rpcResp.result = j.dump(2);
	}

	return true;
}

bool ScriptManager::rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	unique_lock<mutex> lock(m_csScripts);
	string name = params["name"].get<string>();
	m_mapScripts.erase(name);
	saveScriptList("", m_mapScripts);

	fs::deleteFile(m_confPath + "/scripts/" + name + ".js");

	
	rpcResp.result = "\"ok\"";
	return true;
}


string ScriptManager::getScriptPath(json& params, RPC_SESSION session)
{
	string rootTag = "";
	if (!params.is_null())
	{
		if(!params["tag"].is_null())
			rootTag = params["tag"].get<string>();
	}
		

	rootTag = TAG::addRoot(rootTag, session.org);
	rootTag = str::replace(rootTag, ".", "/");
	string path = m_confPath + "/scripts/" + rootTag;

	return path;
}

bool ScriptManager::rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	unique_lock<mutex> lock(m_csScripts);
	string path = getScriptPath(params,session);
	string fileName = params["name"].get<string>();
	string path1 = path + "/" + fileName + ".js";
	string path2 = path + "/" + fileName + "_envVar.js";

	string s;
	if (fs::readFile(path1, s))
	{
		json j;
		j["code"] = s;
		string s1;
		if (fs::readFile(path2, s1)) {
			j["envVarCode"] = s1;
		}

		rpcResp.result = j.dump();
	}
	else
	{
		rpcResp.result = "";
	}


	return true;
}

bool ScriptManager::rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	unique_lock<mutex> lock(m_csScripts);

	string name = params["info"]["name"];

	if (m_mapScripts.find(name) == m_mapScripts.end()) {
		SCRIPT_INFO si;
		m_mapScripts[name] = si;
	}

	SCRIPT_INFO& si = m_mapScripts[name];

	//支持局部更新，info当中可以只包含1，2个需要修改的字段
	si.fromJson(params["info"]);
	si.lastModifyUser = session.user;

	//保存脚本代码
	if (params.contains("code")) {
	
		string codePath = m_confPath + "/scripts/" + si.name + ".js";

		string s = params["code"].get<string>();
		fs::writeFile(codePath, s);
		si.script = s;
	}

	if (params.contains("envVarCode")) {

		string codePath = m_confPath + "/scripts/" + si.name + "_envVar.js";

		string s = params["envVarCode"].get<string>();
		fs::writeFile(codePath, s);
		si.envVarScript = s;
	}

	saveScriptList("", m_mapScripts);

	rpcResp.result = RPC_OK;

	//触发脚本循环。如果已经在循环中，此句无效果
	run();

	return true;
}

bool ScriptManager::rpc_getScriptMngStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	json j;
	j["lastExpScriptTimeCost"] = m_lastExpScripTimeCost;
	j["lastExpScriptRunTime"] = m_tLastExpScriptRunTime.toStr(true);
	int expScriptCount;
	vector<SCRIPT_INFO> allExpInfo;
	m_csExpScripts.lock();
	expScriptCount = m_vecVarExpScripts.size();
	allExpInfo = m_vecVarExpScripts;
	m_csExpScripts.unlock();
	j["expScriptCount"] = expScriptCount;
	json jRunInfoList = json::array();
	for (int i = 0; i < allExpInfo.size(); i++) {
		SCRIPT_INFO& si = allExpInfo[i];
		json jRunInfo;
		jRunInfo["script"] = si.script;
		jRunInfo["retVal"] = si.lastRunInfo.retVal;
		jRunInfo["runSuccess"] = si.lastRunInfo.runSuccess;
		jRunInfo["valNullInCalc"] = si.lastRunInfo.valNullInCalc;
		json jTagRefDataTime = json::object();
		for (auto i : si.lastRunInfo.tagRefDataTime) {
			jTagRefDataTime[i.first] = i.second;
		}
		jRunInfo["tagRefDataTime"] = jTagRefDataTime;
		jRunInfo["runTime"] = si.lastExe.toStr(true);
		jRunInfoList.push_back(jRunInfo);
	}
	j["runInfo"] = jRunInfoList;


	rpcResp.result = j.dump();
	return true;
}

void ScriptManager::scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl,json& j)
{
	org = str::replace(org, ".", "/");

	j = json::array();
	for (auto& i : sl) {
		SCRIPT_INFO& si = i.second;
		json jSi;
		si.toJson(jSi);
		j.push_back(jSi);
	}
	
}

void ScriptManager::saveScriptList(string org,std::map<string, SCRIPT_INFO>& sl, bool saveScriptData)
{
	json j;
	scriptList2Json(org, sl, j);

	string path = m_confPath + "/scripts/" + org + "/list.json";

	string s = j.dump(2);
	fs::writeFile(path,s);
}

json ScriptManager::getScriptList(string tag)
{
	return json();
}

void ScriptManager::exeAllGlobalScripts()
{
	//获取所有需要执行的脚本
	vector<SCRIPT_INFO> toExeScripts;
	m_csScripts.lock();
	for (auto& i : m_mapScripts) {
		SCRIPT_INFO& si = i.second;
		if (si.mode == "cyclic" && timeopt::CalcTimePassMilliSecond(si.lastExe) > si.interval) {
			toExeScripts.push_back(si);
			si.lastExe = timeopt::now();
		}
	}
	m_csScripts.unlock();

	//执行脚本(执行脚本时，不要占用 m_csScripts锁)
	//设计原则： 执行脚本前不要锁住任何锁，因为脚本内部函数可能会调用某些锁，避免出现死锁
	for (auto& si : toExeScripts) {
		ScriptEngine se;
		se.m_logImp = scriptManager_logImp;
		
		se.m_initGlobalFunc = initGlobalFunc;

		se.m_tagContext = si.getContextTag();
		se.runScript(si.script, si.lastModifyUser);
	}
}
/*
表达式脚本中如果使用了val函数，该函数返回null时，将不会生成计算结果。
例如，需要计算今天的用电量 val("总电量") - val("总电量","today","first")，使用当前值减去今天的第一个值，
如果今天没有采集到过任何数据，后面那个val函数返回null
前面的val函数返回最新值，可能是昨天采集的
那么该二次计算表达式将不返回计算结果
*/
void ScriptManager::exeAllVarExpScripts()
{
	TIME startTime;
	startTime.setNow();
	m_tLastExpScriptRunTime = startTime;
	//获取所有需要执行的脚本
	//计算表达式脚本都是立即执行的，里面一定没有sleep或者output一类的延时函数，因此以下脚本的执行时间可以认为一致
	vector<SCRIPT_INFO> toExeScripts;
	m_csExpScripts.lock();
	toExeScripts = m_vecVarExpScripts;
	for (int i = 0; i < m_vecVarExpScripts.size(); i++) {
		SCRIPT_INFO& si = m_vecVarExpScripts[i];
		si.lastExe = startTime;
	}
	m_csExpScripts.unlock();

	//设计原则： 执行脚本前不要锁住任何锁，因为脚本内部函数可能会调用某些锁，避免出现死锁
	for (int i = 0; i < toExeScripts.size();i++) {
		SCRIPT_INFO& info = toExeScripts[i];
		string& script = info.script;
		ScriptEngine se;
		se.m_logImp = scriptManager_logImp;
		
		se.m_initGlobalFunc = initGlobalFunc;

		se.m_tagContext = info.getContextTag();
		se.m_bValNullInCalc = false;
		bool runOk = se.runScript(script, info.lastModifyUser);

		if (!runOk) {
			info.lastRunInfo.runSuccess = false;
			continue;
		}
		info.lastRunInfo.runSuccess = true;

		if (se.m_bValNullInCalc) {
			//如果val函数返回null并且参与了计算，本次计算无效
			info.lastRunInfo.valNullInCalc = true;
			continue;
		}
		info.lastRunInfo.valNullInCalc = false;

		json j = json::parse(se.m_sEvalRet);
		info.lastRunInfo.retVal = j;
		info.lastRunInfo.tagRefDataTime = se.m_vecValRefTime;
		if (j.is_number())
		{
			double val = j.get<double>();
			json jParams;
			jParams["tag"] = info.calcMpTag;
			jParams["val"] = val;
			if (se.m_vecValRefTime.size() > 0) { //最后的val取值时间作为计算结果的时间
				map<string, string> refTime;
				for (auto i : se.m_vecValRefTime) {
					refTime[i.second] = i.second;
				}
				auto i = refTime.rbegin();
				jParams["time"] = i->first;
			}
#ifdef TDS
			tds->callAsyn("input", jParams);
#endif
			
		}
	}

	//保存执行结果。主要用于问题诊断分析
	m_csExpScripts.lock();
	for (int i = 0; i < m_vecVarExpScripts.size(); i++) {
		SCRIPT_INFO& si = m_vecVarExpScripts[i];
		SCRIPT_INFO& si1 = toExeScripts[i];
		si.lastRunInfo = si1.lastRunInfo;
	}
	m_csExpScripts.unlock();

	TIME endTime;
	endTime.setNow();
	m_lastExpScripTimeCost = ((float)timeopt::CalcTimePassMilliSecond(startTime)) / 1000.0;
}

void ScriptManager::loopExe()
{
	loopRunning = true;
	TIME lastExe1 = timeopt::now();
	TIME lastExe2 = timeopt::now();

	while (1)
	{
		if (!hasScripts()) {
			break;
		}

		if (!m_bRun)
			break;

		exeAllGlobalScripts();

		if (m_vecVarExpScripts.size() > 0) {
			if (timeopt::CalcTimePassSecond(lastExe2) > 5) {
				exeAllVarExpScripts();
				lastExe2 = timeopt::now();
			}
		}

		timeopt::sleepMilli(50);
	}
	loopRunning = false;
}


#endif

string SCRIPT_INFO::getContextTag()
{
	string envTag = rootTag;
	envTag = TAG::addRoot(envTag, callerObjTag);
	envTag = TAG::addRoot(envTag, org);
	return envTag;
}

string SCRIPT_INFO::getExpContextTag()
{
	string envTag = rootTag;
	envTag = TAG::addRoot(envTag, callerObjTag);
	envTag = TAG::addRoot(envTag, org);
	return envTag;
}

void SCRIPT_INFO::toJson(json& j,bool getStatus)
{
	j["mode"] = mode;
	j["name"] = name;
	j["desc"] = desc;
	j["lastModifyTime"] = lastModifyTime;
	j["lastModifyUser"] = lastModifyUser;
	int min = interval / (60 * 1000);
	int time = interval % (60 * 1000);
	int sec = time / 1000;
	int milli = time % 1000;
	json jIter;
	jIter["min"] = min;
	jIter["sec"] = sec;
	jIter["milli"] = milli;
	j["interval"] = jIter;
	j["rootTag"] = rootTag;
	j["devAddr"] = devAddr;
	j["calcMpTag"] = calcMpTag;
	j["callerObjTag"] = callerObjTag;

	if (getStatus) {
		j["lastExeTime"] = lastExe.toStr();
		j["lastCalcVal"] = lastRunInfo.retVal;
	}
}

void SCRIPT_INFO::fromJson(json& j)
{
	if(j.contains("mode"))
		mode = j["mode"];

	if (j.contains("name")) {
		name = j["name"];
	}

	if (j.contains("lastModifyUser")) {
		lastModifyUser = j["lastModifyUser"];
	}

	if(j.contains("desc"))
		desc = j["desc"];

	if (j.contains("rootTag"))
		rootTag = j["rootTag"];

	if (j.contains("devAddr"))
		devAddr = j["devAddr"];

	if (j.contains("interval")) {
		json jInter = j["interval"];
		int min = jInter["min"].get<int>();
		int sec = jInter["sec"].get<int>();
		int milli = jInter["milli"].get<int>();
		interval = min * 60 * 1000 + sec * 1000 + milli;
	}
}
