#include "pch.h"
#include "scriptEngine.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "rpcHandler.h"


#ifdef ENABLE_JERRY_SCRIPT

json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count)
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
		for (int i = 0; i < jVal.size(); i++) {
			json jItem = jVal[i];
			jerry_value_t array_value;
			jsonVal2jerryVal(jItem, array_value);
			jerry_set_property_by_index(jerryVal, i, array_value);
		}
	}
}


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

bool getScriptEngineObj(json& jObj, jerry_value_t engineObj)
{
	bool iteration_result = jerry_foreach_object_property(engineObj, jerryItem2JsonItem, &jObj);
	return false;
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

		LOG("[脚本日志]" + log);
		pEngine->m_vecOutput.push_back(log);
	}

	return jerry_create_undefined();
}


jerry_value_t func_input(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		string tag = jArgs[0].get<string>();
		json jVal = jArgs[1];
		MP* pmp = prj.GetMPByTag(tag);
		if (pmp)
		{
			json jResp,jErr;
			pmp->input(jVal);
		}
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
		string tag = jArgs[0].get<string>();
		json jVal = jArgs[1];
		MP* pmp = prj.GetMPByTag(tag);
		if (pmp)
		{
			json jResp, jErr;
			pmp->output(jVal, jResp, jErr);
		}
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

		RPC_RESP resp;
		rpcSrv.handleMethodCall(method, params, resp, pEngine->currentSession);
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
		sleep(milli);
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_getMp(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

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
	}
}

jerry_value_t func_sum(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

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
			sTag = OBJ::ResolveTag(sTag, pEngine->m_tagThis);
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
								jsonVal2jerryVal(jVal, ret);
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

#endif
