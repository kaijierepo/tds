#include "pch.h"
#include "scriptHost.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "mo.h"
#include "rpcHandler.h"

scriptHost sHost;





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
			json jResp;
			pmp->output(jVal, jResp);
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
			jerry_value_t ret = jerry_create_null();
			return ret;
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

		}
		jArguments.push_back(j);
	}

	return jArguments;
}


bool scriptHost::init()
{
	string conf;
	vector<string> sList;
	fs::getFileList(sList, tds->conf->projectConfPath + "/scripts");

	for (int i = 0; i < sList.size(); i++)
	{
		string name = sList[i];
		string script;
		if (fs::readFile(tds->conf->projectConfPath + "/scripts/" + name, script))
		{
			m_mapScripts[name] = script;
		}
	}
	return true;
}

void scriptThread(scriptHost* p)
{
	p->loopExe();
}

void scriptThread1(scriptHost* p)
{
	p->loopExe();
}

bool scriptHost::run()
{
	if (!tds->conf->enableScript)
		return false;

	init();
	thread t(scriptThread, this);
	t.detach();
	return false;
}

bool scriptHost::rpc_runScript(json& params,RPC_RESP& rpcResp,RPC_SESSION session)
{
	string org = session.org;
	org = str::replace(org, ".", "/");
	string scriptName = params["name"].get<string>();
	string scriptPath = tds->conf->projectConfPath + "/tasks/" + org + "/" + scriptName + ".js";

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
		

		jerry_release_value(property_name_getMp);
		jerry_release_value(property_func_getMp);
		jerry_release_value(property_name_log);
		jerry_release_value(property_func_log);
		jerry_release_value(property_name_output);
		jerry_release_value(property_func_output);
		jerry_release_value(global_object);

		jerry_cleanup();
	}
	catch (std::exception& e)
	{
		return false;
	}
	return true;
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
	//if (jerry_value_is_string(prop_name)) {
	//	jerry_char_t string_buffer[128];
	//	jerry_size_t copied_bytes = jerry_substring_to_char_buffer(prop_name,
	//		0,
	//		127,
	//		string_buffer,
	//		127);
	//	string_buffer[copied_bytes] = '\0';

	//	printf("Property: %s\n", string_buffer);

	//	struct iteration_data* data = (struct iteration_data*)user_data_p;
	//	data->string_property_count++;
	//}


	json j;
	if (jerry_value_is_boolean(prop_name))
	{
		j = jerry_value_to_boolean(prop_name);
	}
	else if (jerry_value_is_bigint(prop_name))
	{
		j = jerry_value_as_integer(prop_name);
	}
	else if (jerry_value_is_number(prop_name))
	{
		j = jerry_get_number_value(prop_name);
	}
	else if (jerry_value_is_string(prop_name))
	{
		jerry_value_t string_value = jerry_value_to_string(prop_name);
		jerry_size_t tSize = jerry_get_string_size(string_value);
		jerry_char_t* buffer = new jerry_char_t[tSize + 1];
		jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, tSize);
		buffer[copied_bytes] = '\0';
		jerry_release_value(string_value);
		string s = (const char*)buffer;
		j = s;
		delete buffer;
	}
	else if (jerry_value_is_object(prop_name))
	{

	}
	return true;
}

bool scriptHost::getScriptEngineObj(json& jObj, jerry_value_t engineObj)
{
	bool iteration_result = jerry_foreach_object_property(engineObj, setEngineObj2Json, &jObj);
	return false;
}
