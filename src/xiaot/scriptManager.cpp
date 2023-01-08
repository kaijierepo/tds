#include "pch.h"
#include "scriptManager.h"
#include "scriptEngine.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "rpcHandler.h"
#include "jerryscript-port.h"

ScriptManager scriptManager;

void scriptThread(ScriptManager* p)
{
#ifdef ENABLE_JERRY_SCRIPT
	p->loopExe();
#endif
}

bool ScriptManager::init()
{
	string conf;
	vector<string> sList;
	fs::getFileList(sList, tds->conf->confPath + "/scripts");

	for (int i = 0; i < sList.size(); i++)
	{
		string name = sList[i];
		string script;
		if (fs::readFile(tds->conf->confPath + "/scripts/" + name, script))
		{
			m_mapScripts[name] = script;
		}
	}
	return true;
}

bool ScriptManager::run()
{
	if (!tds->conf->enableScript)
		return false;

	init();
	updateVarExpScript();
	thread t(scriptThread, this);
	t.detach();
	return false;
}

void ScriptManager::updateVarExpScript()
{
	m_mapVarExpScripts.clear();
	std::vector<MP*> aryMP;
	prj.GetAllChildMp(aryMP);
	for (int i = 0; i < aryMP.size(); i++) {
		MP* p = aryMP[i];
		if (p->m_ioType == "v" && p->m_expression!="") {
			VAR_EXP_SCRIPT_INFO i;
			i.script = p->m_expression;
			i.tagThis = p->getTag();
			m_mapVarExpScripts[p->getTag()] = i;
		}
	}
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
		se.runScript(s);
		string sOutput;
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
		session.queryRootTag = "";
		if (params["tag"] != nullptr)
			session.queryRootTag = params["tag"].get<string>();

		session.rootTag = TAG::addRoot(session.queryRootTag, session.org);
		string script;
		fs::readFile(scriptPath, script);
		if (script.length() > 0)
		{
			ScriptEngine se;
			se.currentSession = session;
			if (se.runScript(script))
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
	string path = getScriptPath(params, session);

	//有信息文件
	json j = json::array();
	if (fs::fileExist(path + "/list.json"))
	{
		string s;
		fs::readFile(path + "/list.json", s);
		if (s.length() > 0)
		{
			j = json::parse(s);
		}

	}
	rpcResp.result = j.dump(4);
	return true;
}

bool ScriptManager::rpc_deleteScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string path = getScriptPath(params, session);
	int idx = params["index"].get<int>();

	//有信息文件
	if (fs::fileExist(path + "/list.json"))
	{
		string s;
		fs::readFile(path + "/list.json", s);
		if (s.length() > 0)
		{
			json j = json::parse(s);
			
			if (j.size() - 1 >= idx) {
				json jInfo = j[idx];
				string fileName = jInfo["name"].get<string>();
				fs::deleteFile(path + "/" + fileName + ".js");
				j.erase(idx);

				string s = j.dump(2);
				fs::writeFile(path + "/list.json", s);
			}
		}

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
	string path = getScriptPath(params, session);
	json jInfo = params["info"];
	string sDesc = params["info"]["desc"].get<string>();
	string sName = params["info"]["name"].get<string>();

	//保存脚本代码
	string codePath = path + "/" + sName + ".js";
	string s = params["code"].get<string>();
	if (fs::writeFile(codePath, s))
	{
		rpcResp.result = "\"ok\"";
	}
	else
	{
		rpcResp.error = "\"save fail\"";
	}

	//保存脚本信息
	json jList = json::array();
	string infoPath = path + "/list.json";
	string sList;
	fs::readFile(infoPath, sList);
	if (sList != "")
	{
		jList = json::parse(sList);
	}

	bool existed = false;
	for (int i = 0; i < jList.size(); i++)
	{
		json& jInfoTmp = jList[i];
		if (jInfoTmp["name"].get<string>() == sName)
		{
			jInfoTmp = jInfo;
			existed = true;
		}
	}

	if (!existed) {
		jList.push_back(jInfo);
	}

	sList = jList.dump(4);
	fs::writeFile(infoPath,sList);

	return true;
}

json ScriptManager::getScriptList(string tag)
{
	return json();
}

void ScriptManager::exeAllGlobalScripts()
{
	shared_lock<shared_mutex> lock(prj.m_csPrj);//moTree的读写锁. 读方式锁

	for (auto& i : m_mapScripts)
	{
		string& script = i.second;
		ScriptEngine se;
		se.runScript(script);
	}
}

void ScriptManager::exeAllVarExpScripts()
{
	shared_lock<shared_mutex> lock(prj.m_csPrj);//moTree的读写锁. 读方式锁

	for (auto& i : m_mapVarExpScripts)
	{
		VAR_EXP_SCRIPT_INFO& info = i.second;
		string& script = info.script;

		ScriptEngine se;
		se.m_tagThis = info.tagThis;
		se.runScript(script);

		if (se.m_jEvalRet.is_number())
		{
			double val = se.m_jEvalRet.get<double>();
			json jParams;
			jParams["tag"] = i.first;
			jParams["val"] = val;
			tds->callAsyn("input", jParams.dump());
		}
	}
}

void ScriptManager::loopExe()
{
	TIME lastExe1 = timeopt::now();
	TIME lastExe2 = timeopt::now();

	while (1)
	{
		//if (m_mapScripts.size() > 0) {
		//	if (timeopt::CalcTimePassSecond(lastExe1) > 1) {
		//		exeAllGlobalScripts();
		//		lastExe1 = timeopt::now();
		//	}
		//}
		
		if (m_mapVarExpScripts.size() > 0) {
			if (timeopt::CalcTimePassSecond(lastExe2) > 5) {
				exeAllVarExpScripts();
				lastExe2 = timeopt::now();
			}
		}

		Sleep(100);
	}
}


#endif
