#include "pch.h"
#include "scriptManager.h"
#include "scriptEngine.h"
#include "logger.h"
#include "jerryscript-port.h"

ScriptManager scriptManager;

void scriptThread(ScriptManager* p)
{
#ifdef ENABLE_JERRY_SCRIPT
	p->loopExe();
#endif
}

ScriptManager::ScriptManager()
{
	loopRunning = false;
}

bool ScriptManager::init()
{
	string conf;
	vector<fs::FILE_INFO> sIndexFiles;
	fs::getFileList(sIndexFiles, tds->conf->confPath + "/scripts",true,true,".json");

	unique_lock<mutex> lock(m_csScripts);
	for (int i = 0; i < sIndexFiles.size(); i++)
	{
		fs::FILE_INFO fi = sIndexFiles[i];
		string sScriptList;
		if (fs::readFile(fi.path, sScriptList))
		{
			//根据路径获取组织结构
			string org = str::trimPrefix(fi.path,tds->conf->confPath + "/scripts/");
			org = str::trimSuffix(org, "list.json");
			org = str::trimSuffix(org, "/");
			//加载一个组织结构下的所有脚本文件
			json jSL = json::parse(sScriptList);
			map<string, SCRIPT_INFO> mapScriptList;
			for (int i = 0; i < jSL.size(); i++) {
				json jInfo = jSL[i];
				SCRIPT_INFO si;
				si.lastExe = timeopt::now();
				si.fromJson(jInfo);
				string scriptFilePath = fi.folderPath + "/" + si.name +".js";
				string scriptData;
				if (fs::readFile(scriptFilePath, scriptData)) {
					si.script = scriptData;
					mapScriptList[si.name] = si;
				}
				else {
					continue;
					LOG("[error]加载脚本文件失败," + scriptFilePath);
				}
	
			}
			m_mapScripts[org] = mapScriptList;
		}
	}
	return true;
}

bool ScriptManager::run()
{
	thread t(scriptThread, this);
	t.detach();
	return false;
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
		if (m_mapVarExpScripts.size() > 0)
			return true;
	}
	return false;
}

void ScriptManager::updateVarExpScript(std::map<string, SCRIPT_INFO>& varExpScripts)
{
	unique_lock<mutex> lock(m_csExpScripts);
	m_mapVarExpScripts.clear();
	m_mapVarExpScripts = varExpScripts;
}

void scriptThreadTmp(string scriptName, string tagThis)
{
#ifdef ENABLE_JERRY_SCRIPT
	unique_lock<mutex> lock(scriptManager.m_csScripts);
	for (auto& i : scriptManager.m_mapScripts) {
		map<string, SCRIPT_INFO>& mapSL = i.second;
		for (auto& j : mapSL) {
			SCRIPT_INFO& si = j.second;
			if (si.name == scriptName) {
				ScriptEngine se;
				se.m_tagContext = tagThis;
				se.runScript(si.script, si.lastModifyUser);
				si.lastExe = timeopt::now();
			}
		}
	}
#endif
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



bool ScriptManager::rpc_runScript(json& params,RPC_RESP& rpcResp,RPC_SESSION session)
{
	//直接执行脚本
	if (params["script"] != nullptr) {
		string s = params["script"];

		ScriptEngine se;
		se.runScript(s,session.user);

		json jOutput = json::array();
		for (int i = 0; i < se.m_vecOutput.size(); i++) {
			string sline = se.m_vecOutput[i];
			jOutput.push_back(sline);
		}
		
		rpcResp.result = jOutput.dump();
	}
	//执行保存的脚本文件
	else {
		string scriptName = params["name"].get<string>();
		string scriptPath = getScriptPath(params, session) + "/" + scriptName + ".js";

		string script;
		fs::readFile(scriptPath, script);
		if (script.length() > 0)
		{
			ScriptEngine se;
			se.currentSession = session;
			if (se.runScript(script,session.user))
			{
				rpcResp.result = "\"ok\"";
			}
			else
			{
				json jError = "run fail";
				rpcResp.error = jError.dump();
			}
		}
		else
		{
			json jError = "script not found";
			rpcResp.error = jError.dump();
		}
	}

	return true;
}

bool ScriptManager::rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	unique_lock<mutex> lock(m_csScripts);
	string orgKey = str::replace(session.org, ".", "/");
	json j = json::array();
	std::map<string, std::map<string, SCRIPT_INFO>>::iterator iter = m_mapScripts.find(orgKey);
	if (iter != m_mapScripts.end()) {
		std::map<string, SCRIPT_INFO>& sl = iter->second;

		scriptList2Json(session.org, sl, j);
	}
	rpcResp.result = j.dump(2);
	return true;
}

bool ScriptManager::rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	unique_lock<mutex> lock(m_csScripts);
	string orgKey = str::replace(session.org, ".", "/");
	json j = json::array();
	std::map<string, std::map<string, SCRIPT_INFO>>::iterator iter = m_mapScripts.find(orgKey);
	if (iter != m_mapScripts.end()) {
		std::map<string, SCRIPT_INFO>& sl = iter->second;
		string name = params["name"].get<string>();
		sl.erase(name);
		saveScriptList(session.org, sl);
		fs::deleteFile(tds->conf->confPath + "/scripts/" + orgKey + "/" + name + ".js");
	}
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
	string path = tds->conf->confPath + "/scripts/" + rootTag;
	return path;
}

bool ScriptManager::rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	unique_lock<mutex> lock(m_csScripts);
	string path = getScriptPath(params,session);
	string fileName = params["name"].get<string>();
	path += "/" + fileName + ".js";

	string s;
	if (fs::readFile(path, s))
	{
		json j = s;
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
	string orgKey = str::replace(session.org, ".", "/");

	if (m_mapScripts.find(orgKey) == m_mapScripts.end()) {
		std::map<string, SCRIPT_INFO> ls;
		m_mapScripts[orgKey] = ls;
	}

	std::map<string, SCRIPT_INFO>& ls = m_mapScripts[orgKey];

	string name = params["info"]["name"];

	if (ls.find(name) == ls.end()) {
		SCRIPT_INFO si;
		ls[name] = si;
	}

	SCRIPT_INFO& si = ls[name];

	//支持局部更新，info当中可以只包含1，2个需要修改的字段
	si.fromJson(params["info"]);
	si.lastModifyUser = session.user;

	//保存脚本代码
	if (params.contains("code")) {
		string codePath = tds->conf->confPath + "/scripts/" + orgKey + "/" + si.name + ".js";
		string s = params["code"].get<string>();
		fs::writeFile(codePath, s);
		si.script = s;
	}

	saveScriptList(session.org, ls);

	rpcResp.result = RPC_OK;

	//触发脚本循环。如果已经在循环中，此句无效果
	run();

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

	string path = tds->conf->confPath + "/scripts/" + org + "/list.json";
	string s = j.dump(2);
	fs::writeFile(path,s);
}

json ScriptManager::getScriptList(string tag)
{
	return json();
}

void ScriptManager::exeAllGlobalScripts()
{
	unique_lock<mutex> lock(m_csScripts);
	for (auto& i : m_mapScripts) {
		map<string,SCRIPT_INFO>& mapSL = i.second;
		for (auto& j : mapSL) {
			SCRIPT_INFO& si = j.second;
			if (si.mode == "cyclic" && timeopt::CalcTimePassMilliSecond(si.lastExe) > si.interval) {
				ScriptEngine se;
				se.m_tagContext = si.tagThis;
				se.runScript(si.script,si.lastModifyUser);
				si.lastExe = timeopt::now();
			}
		}
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
	unique_lock<mutex> lock(m_csExpScripts);
	for (auto& i : m_mapVarExpScripts)
	{
		SCRIPT_INFO& info = i.second;
		string& script = info.script;

		ScriptEngine se;
		size_t pos = info.tagThis.rfind(".");
		if (pos == string::npos)
			continue;



		se.m_tagContext = info.tagThis.substr(0,pos); //tagThis的父位号作为context位号
		se.m_bValNullInCalc = false;
		se.runScript(script,info.lastModifyUser);

		if (se.m_bValNullInCalc) {
			//如果val函数返回null并且参与了计算，本次计算无效
			continue;
		}

		if (se.m_jEvalRet.is_number())
		{
			double val = se.m_jEvalRet.get<double>();
			json jParams;
			jParams["tag"] = i.first;
			jParams["val"] = val;
			tds->callAsyn("input", jParams);
		}
	}
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

		exeAllGlobalScripts();

		if (m_mapVarExpScripts.size() > 0) {
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

void SCRIPT_INFO::toJson(json& j)
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

	if (j.contains("interval")) {
		json jInter = j["interval"];
		int min = jInter["min"].get<int>();
		int sec = jInter["sec"].get<int>();
		int milli = jInter["milli"].get<int>();
		interval = min * 60 * 1000 + sec * 1000 + milli;
	}
}
