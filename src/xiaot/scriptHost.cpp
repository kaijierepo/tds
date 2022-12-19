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
			m_mapVarExpScripts[p->getTag()] = p->m_expression;
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
			jerry_value_t obj_mo = jerry_create_object();
			json jMpStatus = pmp->getRTData();
			scriptHost::setScriptEngineObj(jMpStatus, obj_mo);
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

	}
	else {

	}
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
			jerry_value_t string_value = jerry_value_to_string(arguments[0]);
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
	string scriptName = params["name"].get<string>();
	string scriptPath = getScriptPath(params,session) + "/" + scriptName + ".js";
	session.queryRootTag = "";
	if(params["tag"]!=nullptr)
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


	return true;
}

bool scriptHost::runScript(string& script)
{
	try {
		jerry_init(JERRY_INIT_EMPTY);
		jerry_value_t global_object = jerry_get_global_object();

		// getMp函数
		jerry_value_t property_name_getMp = jerry_create_string((const jerry_char_t*)"getMo");
		jerry_value_t property_func_getMp = jerry_create_external_function(func_getMp);
		jerry_value_t set_result = jerry_set_property(global_object, property_name_getMp, property_func_getMp);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);

		// log函数
		jerry_value_t property_name_log = jerry_create_string((const jerry_char_t*)"log");
		jerry_value_t property_func_log = jerry_create_external_function(func_log);
		set_result = jerry_set_property(global_object, property_name_log, property_func_log);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);


		// output函数
		jerry_value_t property_name_output = jerry_create_string((const jerry_char_t*)"output");
		jerry_value_t property_func_output = jerry_create_external_function(func_output);
		set_result = jerry_set_property(global_object, property_name_output, property_func_output);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);


		// call函数
		jerry_value_t property_name_call = jerry_create_string((const jerry_char_t*)"call");
		jerry_value_t property_func_call = jerry_create_external_function(func_call);
		set_result = jerry_set_property(global_object, property_name_call, property_func_call);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);


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
		

		jerry_release_value(property_name_getMp);
		jerry_release_value(property_func_getMp);
		jerry_release_value(property_name_log);
		jerry_release_value(property_func_log);
		jerry_release_value(property_name_output);
		jerry_release_value(property_func_output);
		jerry_release_value(property_name_call);
		jerry_release_value(property_func_call);
		jerry_release_value(global_object);

		jerry_cleanup();
	}
	catch (std::exception& e)
	{
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

void scriptHost::loopExe()
{
	while (1)
	{
		Sleep(100);

		shared_lock<shared_mutex> lock(prj.m_csPrj);//moTree的读写锁. 读方式锁

		jerry_init(JERRY_INIT_EMPTY);
		jerry_value_t global_object = jerry_get_global_object();

		// getMp函数
		jerry_value_t property_name_getMp = jerry_create_string((const jerry_char_t*)"getMo");
		jerry_value_t property_func_getMp = jerry_create_external_function(func_getMp);
		jerry_value_t set_result = jerry_set_property(global_object, property_name_getMp, property_func_getMp);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);

		// log函数
		jerry_value_t property_name_log = jerry_create_string((const jerry_char_t*)"log");
		jerry_value_t property_func_log = jerry_create_external_function(func_log);
		set_result = jerry_set_property(global_object, property_name_log, property_func_log);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);


		// output函数
		jerry_value_t property_name_output = jerry_create_string((const jerry_char_t*)"output");
		jerry_value_t property_func_output = jerry_create_external_function(func_output);
		set_result = jerry_set_property(global_object, property_name_output, property_func_output);
		if (jerry_value_is_error(set_result)) {
		}
		jerry_release_value(set_result);


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

		jerry_release_value(property_name_getMp);
		jerry_release_value(property_func_getMp);
		jerry_release_value(property_name_log);
		jerry_release_value(property_func_log);
		jerry_release_value(property_name_output);
		jerry_release_value(property_func_output);
		jerry_release_value(global_object);

		jerry_cleanup();
	}
}


bool scriptHost::setScriptEngineObj(json& jObj, jerry_value_t engineObj)
{
	for (auto& [key, value] : jObj.items()) {
		if (value.is_null())
			continue;

		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)key.c_str());
		jerry_value_t prop_value;
		if (value.is_string())
			prop_value = jerry_create_string_from_utf8((const jerry_char_t*)value.get<string>().c_str());
		else if (value.is_number_float())
			prop_value = jerry_create_number(value.get<double>());
		else if (value.is_number_integer())
		{
			uint64_t digits[1] = { value.get<unsigned int>() };
			prop_value = jerry_create_bigint(digits, 1, true);
		}
		else if (value.is_number_unsigned())
		{
			uint64_t digits[1] = { value.get<int>() };
			prop_value = jerry_create_bigint(digits, 1, false);
		}
		else if (value.is_boolean())
			prop_value = jerry_create_boolean(value.get<bool>());
		else if (value.is_object())
		{
			prop_value = jerry_create_object();
			setScriptEngineObj(value, prop_value);
		}


		jerry_value_t set_result = jerry_set_property(engineObj, prop_name, prop_value);
		if (jerry_value_is_error(set_result)) {
			jerry_error_t error = jerry_get_error_type(set_result);
			jerry_release_value(error);
		}
		jerry_release_value(set_result);
		jerry_release_value(prop_name);
		jerry_release_value(prop_value);
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
