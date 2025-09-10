#include "scriptManager.h"
#include "scriptEngine.h"
#include "scriptFunc.h"
#include "logger.h"

#ifdef TDS
#include "ioSrv.h"
#endif

#include "tdb.h"

ScriptManager scriptManager;

void scriptManager_logImp(string log, ScriptEngine* pEngine, bool logToHost) {
	pEngine->m_vecOutput.push_back(log);

	if (logToHost) {
		string l = "[脚本日志]" + log;
		LOG(l);
	}
}

void scriptThread(ScriptManager* p){
#ifdef ENABLE_QJS
	p->loopExe();
#endif

	p->m_bRun = false;
}

ScriptManager::ScriptManager() {
	m_bRun = false;
	m_reloadFile = false;
}

bool ScriptManager::init() {
	return loadScriptList();
}

bool ScriptManager::loadScriptList() {
	unique_lock<mutex> lock(m_csScripts);
	m_mapScripts.clear();
	string sScriptList;
	if (DB_FS::readFile(m_confPath + "/scripts/list.json", sScriptList)) {
		yyjson_doc* doc = yyjson_read(sScriptList.c_str(), sScriptList.size(), 0);
		yyjson_val* root = yyjson_doc_get_root(doc);

		size_t idx, count = yyjson_arr_size(root);
		yyjson_val* info;
		yyjson_arr_foreach(root, idx, count, info) {
			SCRIPT_INFO si;
			si.lastRunInfo.lastExe = TIME::nowStr(true);
			si.fromJson(info);

			if (!si.isFolder) {
				string scriptFilePath = m_confPath + "/scripts/" + si.name + ".js";
				if (DB_FS::readFile(scriptFilePath, si.script)) {

				}
				else {
					LOG("[error]加载脚本文件失败," + scriptFilePath);
					continue;
				}
			}

			m_mapScripts[si.name] = si;
		}
		yyjson_doc_free(doc);
	}

	return true;
}

bool ScriptManager::run() {
#ifdef TDS
	if (tds->conf->getInt("enableScript", 1) == 0) {
		return false;
	}
#endif

	if (m_bRun) {
		return false;
	}

	m_bRun = true;
	thread t(scriptThread, this);
	t.detach();

	return true;
}

void ScriptManager::setConfPath(const string& conf) {
	m_confPath = conf;
}

bool ScriptManager::hasScripts() {
	{
		unique_lock<mutex> lock(m_csScripts);
		if (m_mapScripts.size() > 0) {
			return true;
		}
	}
	
	{
		unique_lock<mutex> lock(m_csExpScripts);
		if (m_vecVarExpScripts.size() > 0) {
			return true;
		}
	}
	
	return false;
}

void ScriptManager::updateVarExpScript(vector<SCRIPT_INFO>& varExpScripts) {
	unique_lock<mutex> lock(m_csExpScripts);

	m_vecVarExpScripts.clear();
	m_vecVarExpScripts = varExpScripts;
}

void scriptThreadTmp(string scriptName, string callerObjTag) {
#ifdef ENABLE_QJS
	SCRIPT_INFO si;
	if (scriptManager.getScript(scriptName, si)) {
		si.callerObjTag = callerObjTag;

		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		se.m_tagContext = si.getContextTag();
		se.runScript(si,si.lastRunInfo);
	}
#endif
}

bool ScriptManager::handleRpc(string method, yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	bool bHandled = true;

	if (method == "getScriptMngerStatus") {
		rpc_getScriptMngStatus(params_obj, rpcResp, session);
	}
#ifdef ENABLE_QJS
	else if (method == "runScript") {
		rpc_runScript(params_obj, rpcResp, session);
	}
	else if (method == "getScriptList") {
		rpc_getScriptList(params_obj, rpcResp, session);
	}
	else if (method == "setScriptList") {
		rpc_setScriptList(params_obj, rpcResp, session);
	}
	else if (method == "loadScriptList") {
		rpc_loadScriptList(params_obj, rpcResp, session);
	}
	else if (method == "getScriptFile") {
		rpc_getScript(params_obj, rpcResp, session);
	}
	else if (method == "deleteScriptFile") {
		rpc_deleteScript(params_obj, rpcResp, session);
	}
	else if (method == "setScriptFile") {
		rpc_setScript(params_obj, rpcResp, session);
	}
	else if (method == "setScriptEnable") {
		rpc_setScriptEnable(params_obj, rpcResp, session);
	}
#endif
	else {
		bHandled = false;
	}

	return bHandled;
}

bool ScriptManager::runScriptFileAsyn(string scriptName,string tagThis) {
#ifdef ENABLE_QJS
	thread t(scriptThreadTmp, scriptName,tagThis);
	t.detach();
#endif
	return false;
}

void scriptThread1(ScriptManager* p) {
	p->loopExe();
}

bool ScriptManager::getScript(string name, SCRIPT_INFO& sInfo) {
	unique_lock<mutex> lock(scriptManager.m_csScripts);

	for (auto& i : scriptManager.m_mapScripts) {
		SCRIPT_INFO& si = i.second;
		if (si.name == name) {
			sInfo = si;
			return true;
		}
	}
	return false;
}

bool ScriptManager::setRunInfo(string name, SCRIPT_RUN_INFO& sri)
{
	unique_lock<mutex> lock(scriptManager.m_csScripts);
	if (m_mapScripts.find(name) != m_mapScripts.end()) {
		m_mapScripts.at(name).lastRunInfo = sri;
		return true;
	}
	return false;
}

bool ScriptManager::rpc_runScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	// getExpRet
	yyjson_val* get_exp_ret_val = yyjson_obj_get(params_obj, "getExpRet");

	bool getExpRet = false;
	if (get_exp_ret_val && yyjson_is_bool(get_exp_ret_val)) {
		getExpRet = yyjson_get_bool(get_exp_ret_val);
	}

	ScriptEngine se;
	string folder;
	SCRIPT_INFO si;

	yyjson_val* script_val = yyjson_obj_get(params_obj, "script");
	yyjson_val* filePath_val = yyjson_obj_get(params_obj, "filePath");
	if (script_val && yyjson_is_str(script_val)) {
		si.script = yyjson_get_str(script_val);
		si.org = session.org;

		// rootTag
		yyjson_val* root_tag_val = yyjson_obj_get(params_obj, "rootTag");
		if (root_tag_val && yyjson_is_str(root_tag_val)) {
			si.rootTag = yyjson_get_str(root_tag_val);
		}

		// devAddr
		yyjson_val* dev_addr_val = yyjson_obj_get(params_obj, "devAddr");
		if (dev_addr_val && yyjson_is_str(dev_addr_val)) {
			si.devAddr = yyjson_get_str(dev_addr_val);
		}

		yyjson_val* dev_addr_id = yyjson_obj_get(params_obj, "devId");
		if (dev_addr_id && yyjson_is_str(dev_addr_id)) {
			si.devId = yyjson_get_str(dev_addr_id);
		}
	}
	else if (filePath_val && yyjson_is_str(filePath_val)) { //vscode任意文件夹调试模式
		si.filePath = yyjson_get_str(filePath_val);
		//读取路径下的debug.json
		string sDebugParams;
		if (si.folderPath == "") {
			size_t pos = si.filePath.find_last_of("/");
			if (pos != string::npos) {
				si.folderPath = si.filePath.substr(0, pos);
			}
		}
		if (DB_FS::readFile(si.folderPath + "/debug.json", sDebugParams) && sDebugParams != "") {
			yyjson_doc* d = yyjson_read(sDebugParams.c_str(), sDebugParams.size(), 0);
			yyjson_val* r = yyjson_doc_get_root(d);
			yyjson_val* yy_dev = yyjson_obj_get(r, "dev");
			if (yy_dev) {
				si.devAddr = yyjson_get_str(yy_dev);
				se.m_vecOutput.push_back("环境设备:" + si.devAddr);
			}
		}
	}
	else {
		yyjson_val* name_val = yyjson_obj_get(params_obj, "name");

		std::string scriptName;
		if (name_val && yyjson_is_str(name_val)) {
			scriptName = yyjson_get_str(name_val);
		}

		if (getScript(scriptName, si)) {
		}
		else {
			json jError = "specified script not found";
			rpcResp.error = jError.dump();
			return true;
		}
	}


#ifdef TDS
	se.m_initTdsFunc = initTdsFunc;

	if (si.devAddr != "" || si.devId != "") {
		ioDev* p = nullptr;
		if (si.devId != "") {
			p = ioSrv.getIODevByNodeID(si.devId);
		}
		if (!p && si.devAddr != "") {
			p = ioSrv.getIODevByIPPort(si.devAddr);
			if (!p) {
				p = ioSrv.getIODevById(si.devAddr);
			}
		}


		if (p) {
			se.m_ioDevThis = p;
		}
		else {
			json jError = "specified ioDev not found";
			rpcResp.error = jError.dump();
			return true;
		}
	}
#endif


	string sMockjs;
	if (DB_FS::readFile(si.folderPath + "/mock.js", sMockjs) && sMockjs != "") {
		si.envVarScript = sMockjs;
		se.m_envVarScriptLine = static_cast<int>(std::count(sMockjs.begin(), sMockjs.end(), '\n')) + 1;
	}

	se.m_tagContext = si.getContextTag();
	se.m_reloadFile = true;
	si.user = session.user;
	bool runOk = se.runScript(si,si.lastRunInfo);

	json jOutput = json::array();
	if (getExpRet) {
		jOutput.push_back("计算结果=" + se.m_sEvalRet.dump());
	}

	for (int i = 0; i < se.m_vecOutput.size(); i++) {
		string sline = se.m_vecOutput[i];
		jOutput.push_back(sline);
	}

	rpcResp.result = jOutput.dump();
	setRunInfo(si.name, si.lastRunInfo);

	return true;
}

bool ScriptManager::rpc_setScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {

	return true;
}

bool ScriptManager::rpc_loadScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	bool ret = loadScriptList();
	if (ret) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = "\"load script list failed\"";
	}
	return true;
}

bool ScriptManager::rpc_getScriptList(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	string type = "global";

	yyjson_val* type_val = yyjson_obj_get(params_obj, "type");
	if (type_val && yyjson_is_str(type_val)) {
		type = yyjson_get_str(type_val);
	}

	bool getStatus = false;
	yyjson_val* getStatus_val = yyjson_obj_get(params_obj, "getStatus");
	if (getStatus_val && yyjson_is_bool(getStatus_val)) {
		getStatus = yyjson_get_bool(getStatus_val);
	}

	yyjson_mut_doc* mutDoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* mutRoot = yyjson_mut_arr(mutDoc);
	yyjson_mut_doc_set_root(mutDoc, mutRoot);

	if (type == "global") {
		unique_lock<mutex> lock(m_csScripts);

		for (auto& iter : m_mapScripts) {
			SCRIPT_INFO& si = iter.second;

			if (si.org.find(session.org) != 0) {
				continue;
			}

			yyjson_mut_val* obj = yyjson_mut_obj(mutDoc);
			si.toJson(mutDoc, obj, getStatus);

			yyjson_mut_arr_append(mutRoot, obj);
		}
	}
	else if (type == "exp") {
		unique_lock<mutex> lock(m_csExpScripts);

		for (auto& i : m_vecVarExpScripts) {
			SCRIPT_INFO& si = i;

			if (si.org.find(session.org) != 0) {
				continue;
			}

			yyjson_mut_val* obj = yyjson_mut_obj(mutDoc);
			si.toJson(mutDoc, obj, getStatus);

			yyjson_mut_arr_append(mutRoot, obj);
		}
	}

	size_t len = 0;
	char* result = yyjson_mut_write(mutDoc, 0, &len);
	if (result) {
		rpcResp.result = result;
		free(result);
	}
	else {
		rpcResp.result = "[]";
	}

	yyjson_mut_doc_free(mutDoc);
	
	return true;
}

bool ScriptManager::rpc_deleteScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	unique_lock<mutex> lock(m_csScripts);

	yyjson_val* name_val = yyjson_obj_get(params_obj, "name");

	std::string name;
	if (name_val && yyjson_is_str(name_val)) {
		name = yyjson_get_str(name_val);
	}

	if (m_mapScripts.find(name) == m_mapScripts.end()) {
		json jError = "specified script not found";
		rpcResp.error = jError.dump();
		return true;
	}

	SCRIPT_INFO& si = m_mapScripts[name];
	if (si.isFolder) {
		string strPath = DB_STR::utf8_to_gb(m_confPath + "/scripts/" + name);
		DB_FS::deleteDirectory(strPath);
	}
	else {
		DB_FS::deleteFile(m_confPath + "/scripts/" + name + ".js");
	}

	m_mapScripts.erase(name);
	saveScriptList("", m_mapScripts);

	rpcResp.result = "\"ok\"";
	return true;
}

bool ScriptManager::rpc_getScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	unique_lock<mutex> lock(m_csScripts);

	string path = getScriptPath(params_obj, session);
	yyjson_val* name_val = yyjson_obj_get(params_obj, "name");
	std::string fileName;
	if (name_val && yyjson_is_str(name_val)) {
		fileName = yyjson_get_str(name_val);
	}

	string path1 = path + "/" + fileName + ".js";
	string path2 = path + "/" + fileName + "_envVar.js";

	string s;
	json j;
	if (DB_FS::readFile(path1, s)) {
		j["code"] = s;
	}

	string s1;
	if (DB_FS::readFile(path2, s1)) {
		j["envVarCode"] = s1;
	}

	rpcResp.result = j.dump();

	return true;
}

bool ScriptManager::rpc_setScript(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	unique_lock<mutex> lock(m_csScripts);

	std::string oldName;
	std::string name;

	yyjson_val* info_val = yyjson_obj_get(params_obj, "info");
	if (info_val && yyjson_is_obj(info_val)) {
		yyjson_val* oldName_val = yyjson_obj_get(info_val, "oldName");
		if (oldName_val && yyjson_is_str(oldName_val)) {
			oldName = yyjson_get_str(oldName_val);
		}

		yyjson_val* name_val = yyjson_obj_get(info_val, "name");
		if (name_val && yyjson_is_str(name_val)) {
			name = yyjson_get_str(name_val);
		}
	}

	if (!oldName.empty()) {
		if (m_mapScripts.find(oldName) == m_mapScripts.end()) {
			json jError = "specified script not found";
			rpcResp.error = jError.dump();
			return true;
		}

		bool success = false;
		string oldPath = DB_STR::utf8_to_gb(m_confPath + "/scripts/" + oldName);
		string newPath = DB_STR::utf8_to_gb(m_confPath + "/scripts/" + name);

		SCRIPT_INFO& si = m_mapScripts[oldName];
		if (si.isFolder) {
			success = DB_FS::rename(oldPath, newPath);
		}
		else {
			success = DB_FS::rename(oldPath + ".js", newPath + ".js");
		}

		if (!success) {
			json jError = "rename failed";
			rpcResp.error = jError.dump();
			return true;
		}

		m_mapScripts.erase(oldName);
		m_mapScripts[name] = si;
	}
	
	if (m_mapScripts.find(name) == m_mapScripts.end()) {
		SCRIPT_INFO si;
		m_mapScripts[name] = si;
	}

	SCRIPT_INFO& si = m_mapScripts[name];

	if (info_val) {
		si.fromJson(info_val);
	}

	si.lastModifyUser = session.user;

	if (!si.isFolder) {
		yyjson_val* code_val = yyjson_obj_get(params_obj, "code");
		if (code_val && yyjson_is_str(code_val)) {
			string codePath = m_confPath + "/scripts/" + si.name + ".js";
			string s = yyjson_get_str(code_val);

			DB_FS::writeFile(codePath, (char*)s.c_str(), s.length());
			si.script = s;
		}
	}
	else {

	}

	yyjson_val* env_var_code_val = yyjson_obj_get(params_obj, "envVarCode");
	if (env_var_code_val && yyjson_is_str(env_var_code_val)) {
		string codePath = m_confPath + "/scripts/" + si.name + "_envVar.js";
		string s = yyjson_get_str(env_var_code_val);

		DB_FS::writeFile(codePath, (char*)s.c_str(), s.length());
		si.envVarScript = s;
	}

	saveScriptList("", m_mapScripts);
	rpcResp.result = RPC_OK;

	return true;
}

bool ScriptManager::rpc_getScriptMngStatus(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	yyjson_mut_doc* mutDoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* mutRoot = yyjson_mut_obj(mutDoc);
	yyjson_mut_doc_set_root(mutDoc, mutRoot);

	yyjson_mut_obj_add_real(mutDoc, mutRoot, "lastExpScriptTimeCost", m_lastExpScripTimeCost);
	yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "lastExpScriptRunTime", m_tLastExpScriptRunTime.toStr(true).c_str());

	int expScriptCount;
	vector<SCRIPT_INFO> allExpInfo;

	m_csExpScripts.lock();
	expScriptCount = m_vecVarExpScripts.size();
	allExpInfo = m_vecVarExpScripts;
	m_csExpScripts.unlock();

	yyjson_mut_obj_add_int(mutDoc, mutRoot, "expScriptCount", expScriptCount);

	yyjson_mut_val* runInfo = yyjson_mut_arr(mutDoc);
	for (int i = 0; i < allExpInfo.size(); i++) {
		SCRIPT_INFO& si = allExpInfo[i];

		yyjson_mut_val* runInfoObj = yyjson_mut_obj(mutDoc);
		yyjson_mut_obj_add_strcpy(mutDoc, runInfoObj, "tag", si.calcMpTag.c_str());
		yyjson_mut_obj_add_strcpy(mutDoc, runInfoObj, "script", si.script.c_str());
		yyjson_mut_obj_add_strcpy(mutDoc, runInfoObj, "retVal", si.lastRunInfo.retVal.c_str());
		yyjson_mut_obj_add_bool(mutDoc, runInfoObj, "runSuccess", si.lastRunInfo.runSuccess);
		yyjson_mut_obj_add_bool(mutDoc, runInfoObj, "valNullInCalc", si.lastRunInfo.valNullInCalc);

		yyjson_mut_val* tagRefDataTime = yyjson_mut_obj(mutDoc);
		for (const auto& kv : si.lastRunInfo.tagRefDataTime) {
			yyjson_mut_obj_add_strcpy(mutDoc, tagRefDataTime, kv.first.c_str(), kv.second.c_str());
		}

		yyjson_mut_obj_add_val(mutDoc, runInfoObj, "tagRefDataTime", tagRefDataTime);
		yyjson_mut_obj_add_strcpy(mutDoc, runInfoObj, "runTime", si.lastRunInfo.lastExe.c_str());

		yyjson_mut_arr_append(runInfo, runInfoObj);
	}

	yyjson_mut_obj_add_val(mutDoc, mutRoot, "runInfo", runInfo);

	size_t len = 0;
	char* result = yyjson_mut_write(mutDoc, 0, &len);
	if (result) {
		rpcResp.result = result;
		free(result);
	}

	yyjson_mut_doc_free(mutDoc);

	return true;
}

bool ScriptManager::rpc_setScriptEnable(yyjson_val* params_obj, RPC_RESP& rpcResp, RPC_SESSION session) {
	unique_lock<mutex> lock(m_csScripts);
	yyjson_val* name_val = yyjson_obj_get(params_obj, "name");
	std::string name;
	if (name_val && yyjson_is_str(name_val)) {
		name = yyjson_get_str(name_val);
	}

	if (m_mapScripts.find(name) == m_mapScripts.end()) {
		json jResult = "can not find script name";
		rpcResp.error = jResult.dump();
		return false;
	}

	yyjson_val* enable_val = yyjson_obj_get(params_obj, "enable");
	bool enable = false;
	if (enable_val && yyjson_is_bool(enable_val)) {
		enable = yyjson_get_bool(enable_val);
	}

	SCRIPT_INFO& si = m_mapScripts[name];
	si.enable = enable;

	saveScriptList("", m_mapScripts);
	rpcResp.result = RPC_OK;

	return true;
}

void ScriptManager::scriptList2Json(string org, std::map<string, SCRIPT_INFO>& sl, yyjson_mut_doc* mutDoc, yyjson_mut_val* mutRoot) {
	org = str::replace(org, ".", "/");

	for (auto& i : sl) {
		SCRIPT_INFO& si = i.second;

		yyjson_mut_val* obj = yyjson_mut_obj(mutDoc);
		si.toJson(mutDoc, obj);

		yyjson_mut_arr_append(mutRoot, obj);
	}
}

void ScriptManager::saveScriptList(string org, std::map<string, SCRIPT_INFO>& sl, bool saveScriptData) {
	yyjson_mut_doc* mutDoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* mutRoot = yyjson_mut_arr(mutDoc);
	yyjson_mut_doc_set_root(mutDoc, mutRoot);

	scriptList2Json(org, sl, mutDoc, mutRoot);

	string path = m_confPath + "/scripts/" + org + "/list.json";

	size_t len = 0;
	char* s = yyjson_mut_write(mutDoc, YYJSON_WRITE_PRETTY, &len);
	if (s) {
		string str = s;
		DB_FS::writeFile(path, (char*)str.c_str(), str.length());

		free(s);
	}

	yyjson_mut_doc_free(mutDoc);
}

string ScriptManager::getScriptPath(yyjson_val* params_obj, RPC_SESSION session) {
	string rootTag;

	yyjson_val* tag_val = yyjson_obj_get(params_obj, "tag");
	if (tag_val && yyjson_is_str(tag_val)) {
		rootTag = yyjson_get_str(tag_val);
	}

	rootTag = TAG::addRoot(rootTag, session.org);
	rootTag = str::replace(rootTag, ".", "/");

	string path = m_confPath + "/scripts/" + rootTag;
	return path;
}

json ScriptManager::getScriptList(string tag) {
	return json();
}

ioDev* ScriptManager::getEvnDev(SCRIPT_INFO& si) {
	return nullptr;
}

void ScriptManager::exeAllGlobalScripts() {
	vector<SCRIPT_INFO> toExeScripts;

	m_csScripts.lock();
	for (auto& i : m_mapScripts) {
		SCRIPT_INFO& si = i.second;

		TIME tLastExe;
		tLastExe.fromStr(si.lastRunInfo.lastExe);

		if (si.enable && si.mode == "cyclic" && TIME::calcTimePassMilliSecond(tLastExe) > si.interval) {
			toExeScripts.push_back(si);
			si.lastRunInfo.lastExe = TIME::nowStr(true);
		}
	}
	m_csScripts.unlock();

	// Execute the script(do not hold the m_csScripts lock while the script is being executed).
	for (auto& si : toExeScripts) {
		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		se.m_tagContext = si.getContextTag();
		se.m_reloadFile = m_reloadFile;

		//TIME tStart;
		//tStart.setNow();
		bool runOk = se.runScript(si, si.lastRunInfo);

		//int costMilli = TIME::calcTimePassMilliSecond(tStart);

		//// db data
		//auto mutdoc = yyjson_mut_doc_new(nullptr);
		//auto mutroot = yyjson_mut_obj(mutdoc);

		//yyjson_mut_doc_set_root(mutdoc, mutroot);
		//yyjson_mut_obj_add_strcpy(mutdoc, mutroot, "name", si.name.c_str());
		//yyjson_mut_obj_add_strcpy(mutdoc, mutroot, "time", si.lastRunInfo.lastExe.c_str());
		//yyjson_mut_obj_add_int(mutdoc, mutroot, "success", si.lastRunInfo.runSuccess ? 1 : 0);
		//yyjson_mut_obj_add_strcpy(mutdoc, mutroot, "cost", str::format("%dms", costMilli).c_str());

		//string str;

		//auto len = yyjson_mut_get_len(mutroot);
		//char* writeResult = yyjson_mut_val_write_opts(mutroot, YYJSON_WRITE_NOFLAG, nullptr, &len, nullptr);
		//if (writeResult) {
		//	str = writeResult;
		//	free(writeResult);
		//}

		//yyjson_mut_doc_free(mutdoc);
		//
		//if (!str.empty()) {
		//	DB_TIME dbt;
		//	dbt.fromStr(si.lastRunInfo.lastExe);

		//	TDB* ssdb = db.getChildDB("autoScript");
		//	ssdb->InsertValJsonStr("runStatus", dbt, str);
		//}
	}

	m_csScripts.lock();
	for (auto& si : toExeScripts) {
		if (m_mapScripts.find(si.name) != m_mapScripts.end()) {
			m_mapScripts.at(si.name).lastRunInfo = si.lastRunInfo;
		}
	}
	m_csScripts.unlock();
}

void ScriptManager::exeAllVarExpScripts() {
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
	}
	m_csExpScripts.unlock();

	//设计原则： 执行脚本前不要锁住任何锁，因为脚本内部函数可能会调用某些锁，避免出现死锁
	for (int i = 0; i < toExeScripts.size();i++) {
		SCRIPT_INFO& info = toExeScripts[i];
		string& script = info.script;
		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		se.m_tagContext = info.getContextTag();
		se.m_bValNullInCalc = false;

		bool runOk = se.runScript(info,info.lastRunInfo);
		if (!runOk) {
			continue;
		}

		//如果val函数返回null并且参与了计算，本次计算无效
		if (se.m_bValNullInCalc) {
			continue;
		}


		if (se.m_sEvalRet.is_number()) {
			double val = se.m_sEvalRet.get<double>();

			yyjson_mut_doc* mutDoc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_val* mutRoot = yyjson_mut_obj(mutDoc);
			yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "tag", info.calcMpTag.c_str());
			yyjson_mut_obj_add_real(mutDoc, mutRoot, "val", val);

			//最后的val取值时间作为计算结果的时间
			if (se.m_vecValRefTime.size() > 0) {
				map<string, string> refTime;
				for (auto i : se.m_vecValRefTime) {
					refTime[i.second] = i.second;
				}

				auto it = refTime.rbegin();
				yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "time", it->first.c_str());
			}

#ifdef TDS
			size_t len = 0;
			char* result = yyjson_mut_val_write(mutRoot, 0, &len);
			if (result) {
				string str = result;
				tds->callAsyn("input", str);
				free(result);
			}
#endif
			yyjson_mut_doc_free(mutDoc);
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

	m_lastExpScripTimeCost = ((float)TIME::calcTimePassMilliSecond(startTime)) / 1000.0;
}

void ScriptManager::loopExe() {
	TIME lastExe;
	lastExe.setNow();

	while (1){
		if (!hasScripts()) {
			break;
		}
		
		exeAllGlobalScripts();

		if (m_vecVarExpScripts.size() > 0) {
			if (TIME::calcTimePassSecond(lastExe) > 5) {
				exeAllVarExpScripts();
				lastExe.setNow();
			}
		}

		TIME::sleepMilli(50);
	}
}
