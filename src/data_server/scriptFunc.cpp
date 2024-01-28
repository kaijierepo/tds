#ifdef ENABLE_JERRY_SCRIPT
#include "pch.h"
#include "scriptEngine.h"
#include "logger.h"
#include "tds.h"
#include "rpcHandler.h"
#include "prj.h"


bool jerryItem2JsonItem(const jerry_value_t prop_name,
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
	json jItem;
	jerryVal2jsonVal(prop_value, jItem);
	jObj[key] = jItem;
	return true;
}

json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count)
{
	json jArguments = json::array();
	for (size_t i = 0; i < argument_count; i++)
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
			jerry_char_t* buffer = new jerry_char_t[tSize + 1];
			jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, tSize);
			buffer[copied_bytes] = '\0';
			jerry_release_value(string_value);
			string s = (const char*)buffer;
			j = s;
			delete buffer;
		}
		else if (jerry_value_is_object(arguments[i]))
		{
			getScriptEngineObj(j, arguments[i]);
		}
		jArguments.push_back(j);
	}

	return jArguments;
}

bool getScriptEngineObj(json& jObj, jerry_value_t engineObj)
{
	bool iteration_result = jerry_foreach_object_property(engineObj, jerryItem2JsonItem, &jObj);
	return false;
}

void jerryVal2jsonVal(jerry_value_t jerryVal, json& jVal) {
	if (jerry_value_is_boolean(jerryVal))
	{
		jVal = jerry_value_to_boolean(jerryVal);
	}
	else if (jerry_value_is_bigint(jerryVal))
	{
		jVal = jerry_value_as_integer(jerryVal);
	}
	else if (jerry_value_is_number(jerryVal))
	{
		jVal = jerry_get_number_value(jerryVal);
	}
	else if (jerry_value_is_string(jerryVal))
	{
		jerry_value_t string_value = jerry_value_to_string(jerryVal);
		jerry_size_t tSize = jerry_get_string_size(string_value);
		jerry_char_t* buffer = new jerry_char_t[tSize + 1];
		jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, tSize);
		buffer[copied_bytes] = '\0';
		jerry_release_value(string_value);
		string s = (const char*)buffer;
		jVal = s;
		delete buffer;
	}
	else if (jerry_value_is_object(jerryVal))
	{
		jerry_foreach_object_property(jerryVal, jerryItem2JsonItem, &jVal);
	}
	else if (jerry_value_is_array(jerryVal))
	{
		jerry_foreach_object_property(jerryVal, jerryItem2JsonItem, &jVal);
	}
}

void jsonVal2jerryVal(json& jVal, jerry_value_t& jerryVal) {
	if (jVal.is_string())
		jerryVal = jerry_create_string_from_utf8((const jerry_char_t*)jVal.get<string>().c_str());
	else if (jVal.is_null()) {
		jerryVal = jerry_create_null();
	}
	else if (jVal.is_number_float())
		jerryVal = jerry_create_number(jVal.get<double>());
	else if (jVal.is_number_integer())
	{
		int val = jVal.get<int>();
		jerryVal = jerry_create_number(val);
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
		jerryVal = jerry_create_array((uint32_t)jVal.size());
		for (size_t i = 0; i < jVal.size(); i++) {
			json jItem = jVal[i];
			jerry_value_t array_value;
			jsonVal2jerryVal(jItem, array_value);
			jerry_set_property_by_index(jerryVal, (uint32_t)i, array_value);
			jerry_release_value(array_value);
		}
	}
}



jerry_value_t func_log(const jerry_call_info_t* call_info_p,
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

		//LOG("[脚本日志]" + log);
		pEngine->m_vecOutput.push_back(log);
	}

	return jerry_create_undefined();
}

jerry_value_t func_notify(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		json jParams= jArgs[1];

		string method = jArgs[0].get<string>();

		RPC_SESSION session;
		json err, rlt;
		rpcSrv.notify(method, jParams,true);
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}


jerry_value_t func_input(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		string sTag = jArgs[0].get<string>();
		sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);

		json jParams;
		jParams["tag"] = sTag;
		jParams["val"] = jArgs[1];

		RPC_SESSION session;
		json err, rlt;
		tds->call("input", jParams, err, rlt, session);
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}

jerry_value_t func_output(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		string sTag = jArgs[0].get<string>();
		sTag = TAG::resolveTag(sTag, pEngine->m_tagContext); 

		json jParams;
		jParams["tag"] = sTag;
		jParams["val"] = jArgs[1];

		json err, rlt;
		RPC_SESSION session;
		session.user = pEngine->m_user;
		tds->call("output",jParams, err,rlt, session);
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}

jerry_value_t func_call(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		string method = jArgs[0].get<string>();
		json params = jArgs[1];

		json err, rlt;
		tds->call(method, params, err,rlt, pEngine->currentSession);
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}


jerry_value_t func_sleep(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		int milli = jArgs[0].get<int>();
		timeopt::sleepMilli(milli);
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_parseTag(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		string tag = jArgs[0].get<string>();
		string sTag = TAG::resolveTag(tag, pEngine->m_tagContext);

		if (tag.find("*") == string::npos) {
			json jTag = sTag;
			jerry_value_t obj;
			jsonVal2jerryVal(jTag, obj);
			return obj;
		}
		else {
			vector<string> vecTags;
			TAG_SELECTOR ts;
			ts.init(sTag);
			prj.getTagsByTagSelector(vecTags, ts);
			json jTags = json::array();
			for (int i = 0; i < vecTags.size(); i++) {
				jTags.push_back(vecTags[i]);
			}
			jerry_value_t obj;
			jsonVal2jerryVal(jTags, obj);
			return obj;
		}

	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_getObj(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		json tag = jArgs[0];
		if (tag.is_string()) {
			string sTag = tag.get<string>();
			sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);
			OBJ* pObj = prj.queryObj(sTag);
			if (pObj) {
				json j;
				OBJ_QUERIER query;
				query.getConf = true;
				query.getStatus = true;
				query.getChild = false;
				query.getMp = false;
				pObj->toJson(j, query);
				jerry_value_t obj;
				jsonVal2jerryVal(j, obj);
				return obj;
			}
		}
	}


	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_getMp(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	/*json jArgs = engineArgsToJson(arguments, argument_count);

	if(jArgs.size()>0)
	{
		string tag = jArgs[0].get<string>();
		MP* pmp = prj.GetMPByTag(tag);
		if (pmp)
		{
			jerry_value_t obj_mo;
			json jMpStatus = pmp->getRTData();
			jsonVal2jerryVal(jMpStatus, obj_mo);
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
	}*/

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_sum(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		json tag = jArgs[0];
		if (tag.is_string()) {
			string sTag = tag.get<string>();
			sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);
			json params;
			params["tag"] = sTag;
			if (jArgs.size() > 1) {
				params["invalidAsZero"] = jArgs[1];
			}

			json err, rlt;
			tds->call("sum", params, err, rlt, pEngine->currentSession);


			if (rlt != nullptr) {
				jerry_value_t ret;
				jsonVal2jerryVal(rlt, ret);
				return ret;
			}
		}
		else if (tag.is_array()) {
			json jResolvedTag = json::array();
			for (auto& t : tag) {
				if (t.is_string()) {
					string s = t.get<string>();
					s = TAG::resolveTag(s, pEngine->m_tagContext);
					jResolvedTag.push_back(s);
				}
			}

			json params;
			params["tag"] = jResolvedTag;
			if (jArgs.size() > 1) {
				params["invalidAsZero"] = jArgs[1];
			}

			json err, rlt;
			tds->call("sum", params, err, rlt, pEngine->currentSession);

			if (rlt != nullptr) {
				jerry_value_t ret;
				jsonVal2jerryVal(rlt, ret);
				return ret;
			}
		}
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_avg(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		json tag = jArgs[0];
		if (tag.is_string()) {
			string sTag = tag.get<string>();
			sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);
			json params;
			params["tag"] = sTag;

			json err, rlt;
			tds->call("avg", params, err, rlt, pEngine->currentSession);

			if (rlt != nullptr) {
				jerry_value_t ret;
				jsonVal2jerryVal(rlt, ret);
				return ret;
			}
		}
		else if (tag.is_array()) {
			json jResolvedTag = json::array();
			for (auto& t : tag) {
				if (t.is_string()) {
					string s = t.get<string>();
					s = TAG::resolveTag(s, pEngine->m_tagContext);
					jResolvedTag.push_back(s);
				}
			}

			json params;
			params["tag"] = jResolvedTag;
			json err, rlt;
			tds->call("avg", params, err, rlt, pEngine->currentSession);

			if (rlt != nullptr) {
				jerry_value_t ret;
				jsonVal2jerryVal(rlt, ret);
				return ret;
			}
		}
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}



jerry_value_t func_val(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() > 0)
	{
		json tag = jArgs[0];
		if (tag.is_string()) { 
			string sTag = tag.get<string>();
			sTag = TAG::resolveTag(sTag, pEngine->m_tagContext);  
			if (jArgs.size() == 1) {
				json params;
				params["tag"] = sTag;
				params["getStatus"] = true;
				params["getConf"] = false;
				json err, rlt;
				tds->call("getMp", params, err, rlt, pEngine->currentSession);
				if (rlt != nullptr) {
					json jVal = rlt["val"];
					jerry_value_t jerryVal;
					jsonVal2jerryVal(jVal,jerryVal);
					return jerryVal;
				}
				else {
					int errCode = err["code"].get<int>();
					string errMsg = err["message"].get<string>();
					string errInfo = str::format("函数val执行错误,错误码:%d,错误信息:%s", errCode, errMsg.c_str());
					pEngine->m_vecOutput.push_back(errInfo);
					LOG("[脚本引擎]运行错误,错误信息:%s,\r\n环境位号:%s,脚本用户:%s\r\n脚本:%s", errInfo.c_str(), pEngine->m_tagContext.c_str(),pEngine->m_user.c_str(), pEngine->m_script.c_str());
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

					json err, rlt;
					tds->call("db.select", jParams, err, rlt, pEngine->currentSession);
						
					if (rlt.is_array() && rlt.size() > 0) {
						json& jDe = rlt[0];
						json& jVal = jDe["val"];
						jerry_value_t ret;
						jsonVal2jerryVal(jVal, ret);
						return ret;
					}
				}
			}
		}
	}
	jerry_value_t ret = jerry_create_null();
	pEngine->m_bValNullInCalc = true;
	return ret;
}


jerry_value_t
backtrace_handler(const jerry_call_info_t* call_info_p,
	const jerry_value_t args_p[],
	const jerry_length_t args_count)
{
	if (!jerry_is_feature_enabled(JERRY_FEATURE_LINE_INFO))
	{
		printf("Line info disabled, no backtrace will be printed\n");
		return jerry_create_undefined();
	}

	/* If the line info feature is disabled an empty array will be returned. */
	jerry_value_t backtrace_array = jerry_get_backtrace(5);
	uint32_t array_length = jerry_get_array_length(backtrace_array);

	for (uint32_t idx = 0; idx < array_length; idx++)
	{
		jerry_value_t property = jerry_get_property_by_index(backtrace_array, idx);

		jerry_char_t string_buffer[64];
		jerry_size_t copied_bytes = jerry_substring_to_char_buffer(property,
			0,
			63,
			string_buffer,
			63);
		string_buffer[copied_bytes] = '\0';
		printf(" %d: %s\n", idx, string_buffer);

		jerry_release_value(property);
	}

	jerry_release_value(backtrace_array);

	return jerry_create_undefined();
} /* backtrace_handler */

bool initGlobalFunc(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc)
{
	GLOBAL_FUNC gf;
	jerry_value_t& property_name = gf.property_name;
	jerry_value_t& property_func = gf.property_func;
	// getMp函数
	property_name = jerry_create_string((const jerry_char_t*)"getMo");
	property_func = jerry_create_external_function(func_getMp);
	jerry_value_t set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	// log函数
	property_name = jerry_create_string((const jerry_char_t*)"log");
	property_func = jerry_create_external_function(func_log);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	// output函数
	property_name = jerry_create_string((const jerry_char_t*)"output");
	property_func = jerry_create_external_function(func_output);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	// call函数
	property_name = jerry_create_string((const jerry_char_t*)"call");
	property_func = jerry_create_external_function(func_call);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);
	// sum函数
	property_name = jerry_create_string((const jerry_char_t*)"sum");
	property_func = jerry_create_external_function(func_sum);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	// avg函数
	property_name = jerry_create_string((const jerry_char_t*)"avg");
	property_func = jerry_create_external_function(func_avg);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	// val函数
	property_name = jerry_create_string((const jerry_char_t*)"val");
	property_func = jerry_create_external_function(func_val);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);
	// input函数
	property_name = jerry_create_string((const jerry_char_t*)"input");
	property_func = jerry_create_external_function(func_input);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);
	// sleep函数
	property_name = jerry_create_string((const jerry_char_t*)"sleep");
	property_func = jerry_create_external_function(func_sleep);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	// backtrace
	property_name = jerry_create_string((const jerry_char_t*)"backtrace");
	property_func = jerry_create_external_function(backtrace_handler);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	//notify
	property_name = jerry_create_string((const jerry_char_t*)"notify");
	property_func = jerry_create_external_function(func_notify);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);


	//getObj
	property_name = jerry_create_string((const jerry_char_t*)"getObj");
	property_func = jerry_create_external_function(func_getObj);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	//getObj
	property_name = jerry_create_string((const jerry_char_t*)"parseTag");
	property_func = jerry_create_external_function(func_parseTag);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);


	//以下全局对象
	//console对象
	{
		jerry_value_t obj = jerry_create_object();
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"console");

		jerry_value_t obj_prop_name = jerry_create_string((const jerry_char_t*)"log");
		jerry_value_t obj_prop_func = jerry_create_external_function(func_log);
		jerry_release_value(jerry_set_property(obj, obj_prop_name, obj_prop_func));
		jerry_release_value(obj_prop_name);
		jerry_release_value(obj_prop_func);

		jerry_release_value(jerry_set_property(global_object, prop_name, obj));
		jerry_release_value(prop_name);
		jerry_release_value(obj);
	}


	return true;
}

#endif
