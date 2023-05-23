#include "pch.h"
#include "scriptHost.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "rpcHandler.h"

scriptHost sHost;

void scriptThread(scriptHost* p)
{
#ifdef ENABLE_JERRY_SCRIPT
	p->loopExe();
#endif
}

bool scriptHost::init()
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

bool scriptHost::run()
{
	if (!tds->conf->enableScript)
		return false;

	init();
	updateVarExpScript();
	thread t(scriptThread, this);
	t.detach();
	return false;
}

void scriptHost::updateVarExpScript()
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
static jerry_value_t func_log(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	if (argument_count > 0)
	{
		/* Convert the first argument to a string (JS "toString" operation) */
		jerry_value_t string_value = jerry_value_to_string(arguments[0]);

		/* A naive allocation of buffer for the string */
		jerry_char_t buffer[8000] = { 0 };

		/* Copy the whole string to the buffer, without a null termination character,
		 * Please note that if the string does not fit into the buffer nothing will be copied.
		 * More details on the API reference page
		 */
		jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, sizeof(buffer) - 1);
		buffer[copied_bytes] = '\0';

		/* Release the "toString" result */
		jerry_release_value(string_value);

		string log = (const char*)buffer;

		LOG("[脚本日志]" + log);
		sHost.m_vecOutput.push_back(log);
	}

	return jerry_create_undefined();
}


static jerry_value_t func_output(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = scriptHost::engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		string tag = jArgs[0].get<string>();
		json jVal = jArgs[1];
		MP* pmp = prj.GetMPByTag(tag);
		if (pmp)
		{
			json jResp,jErr;
			pmp->output(jVal, jResp,jErr);
		}
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}


//如果rpc调用了脚本，当前rpc的会话信息
RPC_SESSION currentSession;

static jerry_value_t func_call(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = scriptHost::engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		string method = jArgs[0].get<string>();
		json params = jArgs[1];

		RPC_RESP resp;
		rpcSrv.handleMethodCall(method, params, resp, currentSession);
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}



static jerry_value_t func_getMp(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = scriptHost::engineArgsToJson(arguments, argument_count);

	if(jArgs.size()>0)
	{
		string tag = jArgs[0].get<string>();
		MP* pmp = prj.GetMPByTag(tag);
		if (pmp)
		{
			jerry_value_t obj_mo;
			json jMpStatus = pmp->getRTData();
			scriptHost::jsonVal2jerryVal(jMpStatus, obj_mo);
			return obj_mo;
		}
		else
		{
			jerry_value_t ret = jerry_create_null();
			return ret;
		}
	}
	else
	{
		jerry_value_t ret = jerry_create_null();
		return ret;
	}
}

static jerry_value_t func_sum(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = scriptHost::engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		json tag = jArgs[0];

		bool invalidAsZero = false;
		if (jArgs.size() > 1) {
			json jP = jArgs[1];
			if (jP.is_boolean()) {
				invalidAsZero = jP.get<bool>();
			}
		}


		vector<MP*> mpList;
		TAG_SELECTOR tagSel;
		tagSel.init(tag);
		prj.getMpByTagSelector(mpList, tagSel);

		double dbSum = 0;
		bool success = true;
		for (int i = 0; i < mpList.size(); i++) {
			MP* pmp = mpList[i];
			if (pmp->m_curVal.is_number()) {
				double val = pmp->m_curVal.get<double>();
				dbSum += val;
			}
			else {
				if (!invalidAsZero) {
					success = false;
					break;
				}
			}
		}

		if (success) {
			jerry_value_t ret = jerry_create_number(dbSum);
			return ret;
		}
		else {
			jerry_value_t ret = jerry_create_null();
			return ret;
		}
	}
	else {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}
}


static jerry_value_t func_val(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = scriptHost::engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		json tag = jArgs[0];
		if (tag.is_string()) { 
			string sTag = tag.get<string>();
			sTag = OBJ::ResolveTag(sTag, sHost.m_tagThis);
			MP* pmp = prj.getMp(sTag);
			if (pmp) {
				//取实时值
				if (jArgs.size() == 1) {
					if (pmp->m_curVal.is_number()) {
						double val = pmp->m_curVal.get<double>();
						jerry_value_t ret = jerry_create_number(val);
						return ret;
					}
					else if (pmp->m_curVal.is_boolean()) {
						bool val = pmp->m_curVal.get<bool>();
						jerry_value_t ret = jerry_create_boolean(val);
						return ret;
					}
				}
				//取历史值
				else if (jArgs.size() >= 2) {
					json time = jArgs[1];
					if (time.is_string()) {
						json jParams;
						jParams["tag"] = sTag;
						string sTime = time.get<string>();
						jParams["time"] = sTime;
						if (jArgs.size() >= 3) {
							json jAggr = jArgs[2];
							jParams["aggregate"] = jAggr;
						}
						
						DE_SELECTOR deSel;
						db.parseDESelector(jParams, deSel);
						SELECT_RLT rlt;
						db.Select_yyjson(deSel, rlt);
						if (rlt.dataList.length() > 0) {
							json jDeList = json::parse(rlt.dataList);
							if (jDeList.is_array() && jDeList.size() > 0) {
								json& jDe = jDeList[0];
								json& jVal = jDe["val"];
								jerry_value_t ret;
								scriptHost::jsonVal2jerryVal(jVal, ret);
								return ret;
							}
						}
					}
				}
			}
		}
	}
	jerry_value_t ret = jerry_create_null();
	return ret;
}

json scriptHost::engineValToJson(const jerry_value_t value)
{
	return nullptr;
}

json scriptHost::engineArgsToJson(const jerry_value_t arguments[],const jerry_length_t argument_count)
{
	json jArguments = json::array();
	for (int i = 0; i < argument_count; i++)
	{
		json j;
		if (jerry_value_is_boolean(arguments[i]))
		{
			j = jerry_value_to_boolean(arguments[i]);
		}
		else if (jerry_value_is_bigint(arguments[i]))
		{
			j = jerry_value_as_integer(arguments[i]);
		}
		else if (jerry_value_is_number(arguments[i]))
		{
			j = jerry_get_number_value(arguments[i]);
		}
		else if (jerry_value_is_string(arguments[i]))
		{
			jerry_value_t string_value = jerry_value_to_string(arguments[i]);
			jerry_size_t tSize = jerry_get_string_size(string_value);	
			jerry_char_t* buffer = new jerry_char_t[tSize+1];
			jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, tSize);
			buffer[copied_bytes] = '\0';
			jerry_release_value(string_value);
			string s =(const char*) buffer;
			j = s;
			delete buffer;
		}
		else if (jerry_value_is_object(arguments[i]))
		{
			scriptHost::getScriptEngineObj(j, arguments[i]);
		}
		jArguments.push_back(j);
	}

	return jArguments;
}






void scriptThread1(scriptHost* p)
{
	p->loopExe();
}



bool scriptHost::rpc_runScript(json& params,RPC_RESP& rpcResp,RPC_SESSION session)
{
	//直接执行脚本
	if (params["script"] != nullptr) {
		string s = params["script"];

		runScript(s);
		string sOutput;
		json jOutput = json::array();
		for (int i = 0; i < sHost.m_vecOutput.size(); i++) {
			string sline = sHost.m_vecOutput[i];
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
			currentSession = session;
			if (runScript(script))
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

bool scriptHost::initGlobalFunc()
{
	// getMp函数
	property_name_getMp = jerry_create_string((const jerry_char_t*)"getMo");
	property_func_getMp = jerry_create_external_function(func_getMp);
	jerry_value_t set_result = jerry_set_property(global_object, property_name_getMp, property_func_getMp);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);

	// log函数
	 property_name_log = jerry_create_string((const jerry_char_t*)"log");
	 property_func_log = jerry_create_external_function(func_log);
	set_result = jerry_set_property(global_object, property_name_log, property_func_log);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);


	// output函数
	 property_name_output = jerry_create_string((const jerry_char_t*)"output");
	 property_func_output = jerry_create_external_function(func_output);
	set_result = jerry_set_property(global_object, property_name_output, property_func_output);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);


	// call函数
	 property_name_call = jerry_create_string((const jerry_char_t*)"call");
	 property_func_call = jerry_create_external_function(func_call);
	set_result = jerry_set_property(global_object, property_name_call, property_func_call);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);

	// sum函数
	property_name_sum = jerry_create_string((const jerry_char_t*)"sum");
	property_func_sum = jerry_create_external_function(func_sum);
	set_result = jerry_set_property(global_object, property_name_sum, property_func_sum);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);

	// val函数
	property_name_val = jerry_create_string((const jerry_char_t*)"val");
	property_func_val = jerry_create_external_function(func_val);
	set_result = jerry_set_property(global_object, property_name_val, property_func_val);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);

	return true;
}

void scriptHost::releaseGlobalFunc() {
	jerry_release_value(property_name_getMp);
	jerry_release_value(property_func_getMp);
	jerry_release_value(property_name_log);
	jerry_release_value(property_func_log);
	jerry_release_value(property_name_output);
	jerry_release_value(property_func_output);
	jerry_release_value(property_name_call);
	jerry_release_value(property_func_call);
	jerry_release_value(property_name_sum);
	jerry_release_value(property_func_sum);
	jerry_release_value(property_name_val);
	jerry_release_value(property_func_val);
}

bool scriptHost::runScript(string& script)
{
	m_vecOutput.clear();
	try {
		jerry_init(JERRY_INIT_EMPTY);
		global_object = jerry_get_global_object();

		initGlobalFunc();

		///* Run the demo script with 'eval' */
		jerry_value_t eval_ret = jerry_eval((jerry_char_t*)script.c_str(),
			script.length(),
			JERRY_PARSE_NO_OPTS);

		/* Check if there was any error (syntax or runtime) */
		bool run_ok = !jerry_value_is_error(eval_ret);
		jerry_error_t error = jerry_get_error_type(eval_ret);

		if (run_ok)
		{
			bool bRunSuccess = jerry_value_to_boolean(eval_ret);
		}
		else
		{
		}
		jerry_release_value(error);
		jerry_release_value(eval_ret);
		

		releaseGlobalFunc();
		jerry_release_value(global_object);

		jerry_cleanup();
	}
	catch (std::exception& e)
	{
		string s = e.what();
		m_vecOutput.push_back(s);
		return false;
	}
	return true;
}

bool scriptHost::rpc_getScriptList(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string path = getScriptPath(params, session);

	//有信息文件
	if (fs::fileExist(path + "/list.json"))
	{
		string s;
		fs::readFile(path + "/list.json", s);
		if (s.length() > 0)
		{
			json j = json::parse(s);
			rpcResp.result = j.dump(4);
			return true;
		}
		
	}


	//无信息文件
	vector<string> scriptList;
	fs::getFileList(scriptList, path);

	vector<string> nameList;
	//提取出.js文件
	for (int i = 0; i < scriptList.size(); i++)
	{
		string s = scriptList[i];
		if (s.find(".js") != string::npos)
		{
			string n = str::trimSuffix(s, ".js");
			nameList.push_back(n);
		}
	}
	json j = nameList;
	rpcResp.result = j.dump(4);
	

	return true;
}


string scriptHost::getScriptPath(json& params, RPC_SESSION session)
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

bool scriptHost::rpc_getScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
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

bool scriptHost::rpc_setScript(json& params, RPC_RESP& rpcResp, RPC_SESSION session)
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

json scriptHost::getScriptList(string tag)
{
	return json();
}

void scriptHost::exeAllGlobalScripts()
{
	shared_lock<shared_mutex> lock(prj.m_csPrj);//moTree的读写锁. 读方式锁

	jerry_init(JERRY_INIT_EMPTY);
	 global_object = jerry_get_global_object();

	initGlobalFunc();

	for (auto& i : m_mapScripts)
	{
		string& script = i.second;

		/* Run the demo script with 'eval' */
		jerry_value_t eval_ret = jerry_eval((jerry_char_t*)script.c_str(),
			script.length(),
			JERRY_PARSE_NO_OPTS);

		/* Check if there was any error (syntax or runtime) */
		bool run_ok = !jerry_value_is_error(eval_ret);
		jerry_error_t error = jerry_get_error_type(eval_ret);

		if (run_ok)
		{
			bool bRunSuccess = jerry_value_to_boolean(eval_ret);
		}
		else
		{
		}
		jerry_release_value(error);
		jerry_release_value(eval_ret);
	}

	releaseGlobalFunc();
	jerry_release_value(global_object);

	jerry_cleanup();
}

void scriptHost::exeAllVarExpScripts()
{
	shared_lock<shared_mutex> lock(prj.m_csPrj);//moTree的读写锁. 读方式锁


	jerry_init(JERRY_INIT_EMPTY);
	global_object = jerry_get_global_object();
	initGlobalFunc();

	for (auto& i : m_mapVarExpScripts)
	{
		VAR_EXP_SCRIPT_INFO& info = i.second;
		string& script = info.script;
		m_tagThis = info.tagThis;

		/* Run the demo script with 'eval' */
		jerry_value_t eval_ret = jerry_eval((jerry_char_t*)script.c_str(),
			script.length(),
			JERRY_PARSE_NO_OPTS);

		if (jerry_value_is_number(eval_ret))
		{
			double val = jerry_get_number_value(eval_ret);
			json jParams;
			jParams["tag"] = i.first;
			jParams["val"] = val;
			tds->callAsyn("input", jParams.dump());
		}

		/* Check if there was any error (syntax or runtime) */
		bool run_ok = !jerry_value_is_error(eval_ret);
		jerry_error_t error = jerry_get_error_type(eval_ret);

		if (run_ok)
		{
			bool bRunSuccess = jerry_value_to_boolean(eval_ret);
		}
		else
		{
		}
		jerry_release_value(error);
		jerry_release_value(eval_ret);
	}

	releaseGlobalFunc();
	jerry_release_value(global_object);

	jerry_cleanup();
}

void scriptHost::loopExe()
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

bool scriptHost::jsonVal2jerryVal(json& jVal, jerry_value_t& jerryVal) {
	if (jVal.is_string())
		jerryVal = jerry_create_string_from_utf8((const jerry_char_t*)jVal.get<string>().c_str());
	else if (jVal.is_null()) {
		jerryVal = jerry_create_null();
	}
	else if (jVal.is_number_float())
		jerryVal = jerry_create_number(jVal.get<double>());
	else if (jVal.is_number_integer())
	{
		uint64_t digits[1] = { jVal.get<unsigned int>() };
		jerryVal = jerry_create_bigint(digits, 1, true);
	}
	else if (jVal.is_number_unsigned())
	{
		uint64_t digits[1] = { jVal.get<int>() };
		jerryVal = jerry_create_bigint(digits, 1, false);
	}
	else if (jVal.is_boolean())
		jerryVal = jerry_create_boolean(jVal.get<bool>());
	else if (jVal.is_object()) {
		jerryVal = jerry_create_object();
		for (auto& [key, value] : jVal.items()) {
			jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)key.c_str());
			jerry_value_t prop_value;
			jsonVal2jerryVal(value, prop_value);

			jerry_value_t set_result = jerry_set_property(jerryVal, prop_name, prop_value);
			if (jerry_value_is_error(set_result)) {
				jerry_error_t error = jerry_get_error_type(set_result);
				jerry_release_value(error);
			}
			jerry_release_value(set_result);
			jerry_release_value(prop_name);
			jerry_release_value(prop_value);
		}
	}
	else if (jVal.is_array()) {
		jerryVal = jerry_create_array(jVal.size());
		for (int i = 0; i < jVal.size();i++) {
			json jItem = jVal[i];
			jerry_value_t array_value;
			jsonVal2jerryVal(jItem, array_value);
			jerry_set_property_by_index(jerryVal, i, array_value);
		}
	}
	return true;
}


static bool setEngineObj2Json(const jerry_value_t prop_name,
	const jerry_value_t prop_value,
	void* user_data_p)
{
	json& jObj = *(json*)user_data_p;

	//解析key
	string key;
	if (jerry_value_is_string(prop_name)) {
		jerry_char_t string_buffer[128];
		jerry_size_t copied_bytes = jerry_substring_to_char_buffer(prop_name,
			0,
			127,
			string_buffer,
			127);
		string_buffer[copied_bytes] = '\0';
		key = (char*)string_buffer;
	}

	//解析val
	json j;
	if (jerry_value_is_boolean(prop_value))
	{
		j = jerry_value_to_boolean(prop_value);
	}
	else if (jerry_value_is_bigint(prop_value))
	{
		j = jerry_value_as_integer(prop_value);
	}
	else if (jerry_value_is_number(prop_value))
	{
		j = jerry_get_number_value(prop_value);
	}
	else if (jerry_value_is_string(prop_value))
	{
		jerry_value_t string_value = jerry_value_to_string(prop_value);
		jerry_size_t tSize = jerry_get_string_size(string_value);
		jerry_char_t* buffer = new jerry_char_t[tSize + 1];
		jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, tSize);
		buffer[copied_bytes] = '\0';
		jerry_release_value(string_value);
		string s = (const char*)buffer;
		j = s;
		delete buffer;
	}
	else if (jerry_value_is_object(prop_value))
	{
		json j;
		scriptHost::getScriptEngineObj(j,prop_value);
	}
	jObj[key] = j;
	return true;
}

bool scriptHost::getScriptEngineObj(json& jObj, jerry_value_t engineObj)
{
	bool iteration_result = jerry_foreach_object_property(engineObj, setEngineObj2Json, &jObj);
	return false;
}
#endif
