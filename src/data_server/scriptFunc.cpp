#ifdef ENABLE_JERRY_SCRIPT
#include "pch.h"
#include "scriptEngine.h"
#include "logger.h"
#include "tds.h"
#include "rpcHandler.h"
#include "prj.h"
#include "httplib.h"
#include "ioSrv.h"
#include "ioDev_custom.h"
#include <string>
#include <sstream>
#include <cstdint>

namespace tJSEngine {
	int parseStopBits(string s)
	{
		if (s == "1")
			return 0;
		else if (s == "1.5")
			return 1;
		else if (s == "2")
			return 2;
		return 0;
	}

	int parseParity(string s)
	{
		/* 0-4=None,Odd,Even,Mark,Space    */
		if (s == "None")
			return 0;
		else if (s == "Odd")
			return 1;
		else if (s == "Even")
			return 2;
		else if (s == "Mark")
			return 3;
		else if (s == "Space")
			return 4;
		return 0;
	}

	std::string pointerToString(void* ptr) {
		uintptr_t ptrVal = reinterpret_cast<uintptr_t>(ptr);
		std::ostringstream oss;
		oss << "0x" << std::hex << ptrVal;
		return oss.str();
	}

	void* stringToPointer(const std::string& str) {
		char* endPtr;
		uintptr_t ptrVal = std::strtoull(str.c_str(), &endPtr, 0);
		return reinterpret_cast<void*>(ptrVal);
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

json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count)
{
	json jArguments = json::array();
	for (size_t i = 0; i < argument_count; i++)
	{
		json j;
		jerryVal2jsonVal(arguments[i], j);
		jArguments.push_back(j);
	}

	return jArguments;
}

bool getScriptEngineObj(json& jObj, jerry_value_t engineObj)
{
	bool iteration_result = jerry_foreach_object_property(engineObj, jerryItem2JsonItem, &jObj);
	return false;
}

bool is_integer(double x) {
	if (std::isnan(x) || std::isinf(x)) {
		return false;
	}
	const double threshold = 9007199254740992.0; // 2^53
	double abs_x = std::fabs(x);
	if (abs_x >= threshold) {
		return true; // 超出精度范围后无法表示小数
	}
	return x == std::trunc(x);
}

void jerryVal2jsonVal(jerry_value_t jerryVal, json& jVal) {
	if (jerry_value_is_boolean(jerryVal))
	{
		jVal = jerry_value_to_boolean(jerryVal);
	}
	else if (jerry_value_is_bigint(jerryVal))
	{
		uint64_t digits[1];
		bool sign;
		jerry_get_bigint_digits(jerryVal, digits, 1, &sign);
		int intValue = (int)digits[0]; // Assuming it fits within an int
		if (sign)
			intValue = -intValue;
		jVal = intValue;
	}
	else if (jerry_value_is_number(jerryVal))
	{
		double dbVal = jerry_get_number_value(jerryVal);
		if (is_integer(dbVal)) {
			int iVal = dbVal;
			jVal = iVal;
		}
		else {
			jVal = dbVal;
		}
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
	else if (jerry_value_is_array(jerryVal))
	{
		jVal = json::array();
		jerry_length_t length = jerry_get_array_length(jerryVal);
		for (jerry_length_t i = 0; i < length; i++) {
			jerry_value_t element = jerry_get_property_by_index(jerryVal, i);
			json j;
			jerryVal2jsonVal(element, j);
			jVal.push_back(j);

			// 释放 element
			jerry_release_value(element);
		}
	}
	else if (jerry_value_is_object(jerryVal))
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
	else if (jVal.is_number_integer())
	{
		int val = jVal.get<int>();
		jerryVal = jerry_create_number(val);
	}
	else if (jVal.is_number_float())
		jerryVal = jerry_create_number(jVal.get<double>());
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



jerry_value_t func_setReturn(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json j = jArgs[0];
		pEngine->m_scriptRet = j;
	}

	jerry_value_t ret = jerry_create_undefined();
	return ret;
}

jerry_value_t func_log(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() > 0)
	{
		string log = jArgs[0].get<string>();

		bool logToTds = false;
		if (jArgs.size() > 1) {
			logToTds = jArgs[1].get<bool>();
		}
		if (logToTds) {
			LOG("[脚本日志]" + log);
		}
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
			OBJ* pObj = prj.queryObj(sTag,"zh");
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
			if (jArgs.size() == 2) {
				params["invalidAsZero"] = jArgs[1];
			}
			else if (jArgs.size() == 3) {
				params["time"] = jArgs[1];
				params["invalidAsZero"] = jArgs[2];
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
			string sTagOrg = tag.get<string>();
			string sTag = TAG::resolveTag(sTagOrg, pEngine->m_tagContext);
			OBJ* p = prj.queryObj(sTag);
			if (p == nullptr) {
				p = prj.queryObj(sTagOrg);//尝试将原始位号作为全局位号请求
				if(p)
					sTag = sTagOrg;
			}
			if (p) {
				if (jArgs.size() == 1) {
					json params;
					params["tag"] = sTag;
					params["getStatus"] = true;
					params["getConf"] = false;
					json err, rlt;
					tds->call("getMp", params, err, rlt, pEngine->currentSession);

					if (rlt != nullptr) {
						json jVal = rlt["val"];
						json jTime = rlt["time"];
						string info = "val(" + sTag + ") = " + jVal.dump();
						pEngine->m_vecOutput.push_back(info);
						string time = jTime.get<string>();
						TIME t;
						t.fromStr(time);
						pEngine->m_vecValRefTime[sTag] = t.toStr(true);
						jerry_value_t jerryVal;
						jsonVal2jerryVal(jVal, jerryVal);
						return jerryVal;
					}
					else {
						int errCode = err["code"].get<int>();
						string errMsg = err["message"].get<string>();
						string errInfo = str::format("函数val执行错误,错误码:%d,错误信息:%s", errCode, errMsg.c_str());
						pEngine->m_vecOutput.push_back(errInfo);
						if (tds->conf->logEnable.scriptEngine) {
							LOG("[脚本引擎]运行错误,错误信息:%s,\r\n环境位号:%s,脚本用户:%s\r\n脚本:%s", errInfo.c_str(), pEngine->m_tagContext.c_str(), pEngine->m_user.c_str(), pEngine->m_script.c_str());
						}
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

						if (p->m_level == "mp") {
							MP* pmp = (MP*)p;
							pEngine->m_vecValRefTime[sTag] = pmp->m_stDataLastUpdate.toStr(true);
						}

						string info = str::format("val(\"%s\",\"%s\",%s) = ", sTag.c_str(), sTime.c_str(), jParams["aggregate"].dump().c_str());
						if (rlt.is_array() && rlt.size() > 0) {
							json& jDe = rlt[0];
							json& jVal = jDe["val"];
							jerry_value_t ret;
							jsonVal2jerryVal(jVal, ret);
							info += jVal.dump();
							pEngine->m_vecOutput.push_back(info);
							return ret;
						}
						else {
							info += "null";
							pEngine->m_vecOutput.push_back(info);
						}
					}
				}
			}
			else {
				pEngine->m_vecOutput.push_back("无法找到指定的位号");
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


jerry_value_t func_db_insert(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json params = jArgs[0];
		if (params.is_object()) {
			json err, rlt;
			tds->call("db.insert", params, err, rlt, pEngine->currentSession);
			if (rlt != nullptr) {
				jerry_value_t jerryVal;
				jsonVal2jerryVal(rlt, jerryVal);
				return jerryVal;
			}
			else {
				int errCode = err["code"].get<int>();
				string errMsg = err["message"].get<string>();
				string errInfo = str::format("函数val执行错误,错误码:%d,错误信息:%s", errCode, errMsg.c_str());
				pEngine->m_vecOutput.push_back(errInfo);
				LOG("[脚本引擎]运行错误,错误信息:%s,\r\n环境位号:%s,脚本用户:%s\r\n脚本:%s", errInfo.c_str(), pEngine->m_tagContext.c_str(), pEngine->m_user.c_str(), pEngine->m_script.c_str());
			}
		}
	}
	else if (jArgs.size() == 3) {
		json params;
		params["tag"] = jArgs[0];
		params["time"] = jArgs[1];
		params["val"] = jArgs[2];
		json err, rlt;
		tds->call("db.insert", params, err, rlt, pEngine->currentSession);
		if (rlt != nullptr) {
			jerry_value_t jerryVal;
			jsonVal2jerryVal(rlt, jerryVal);
			return jerryVal;
		}
		else {
			int errCode = err["code"].get<int>();
			string errMsg = err["message"].get<string>();
			string errInfo = str::format("函数val执行错误,错误码:%d,错误信息:%s", errCode, errMsg.c_str());
			pEngine->m_vecOutput.push_back(errInfo);
			LOG("[脚本引擎]运行错误,错误信息:%s,\r\n环境位号:%s,脚本用户:%s\r\n脚本:%s", errInfo.c_str(), pEngine->m_tagContext.c_str(), pEngine->m_user.c_str(), pEngine->m_script.c_str());
		}
	}
	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_http_request(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json params = jArgs[0];
		if (params.is_object()) {

			string ip = params["hostname"];
			string addr = "http://" + ip;
			int port = params["port"].get<int>();
			string method = params["method"];
			httplib::Client cli(ip, port);
			string path = params["path"];
			string body;
			httplib::Headers headers;
			if (params.contains("body")) {
				body = params["body"];
			}
			if (params.contains("headers")) {
				json jHeaders = params["headers"];
				for (auto it = jHeaders.begin(); it != jHeaders.end(); ++it) {
					std::pair<string, string> p = { it.key(),it.value() };
					headers.insert(p);
				}
			}
			if (method == "GET") {
				httplib::Result rlt = cli.Get(path, headers);
				if (rlt != nullptr) {
					jerry_value_t ret = jerry_create_object();
					jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"body");
					jerry_value_t prop_val = jerry_create_string((const jerry_char_t*)rlt->body.c_str());
					jerry_release_value(jerry_set_property(ret, prop_name, prop_val));
					jerry_release_value(prop_name);
					jerry_release_value(prop_val);
					return ret;
				}
			}
			else if (method == "POST") {
				httplib::Result rlt = cli.Post(path, headers,body, "application/json");
				if (rlt != nullptr) {
					jerry_value_t ret = jerry_create_object();
					jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"body");
					jerry_value_t prop_val = jerry_create_string((const jerry_char_t*)rlt->body.c_str());
					jerry_release_value(jerry_set_property(ret, prop_name, prop_val));
					jerry_release_value(prop_name);
					jerry_release_value(prop_val);
					return ret;
				}
			}
		}
	}
	
	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_json_stringify(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json params = jArgs[0];
		string s = params.dump();
		jerry_value_t ret = jerry_create_string((const jerry_char_t*)s.c_str());
		return ret;
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_json_parse(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json params = jArgs[0];
		string s = params.get<string>();
		json j = json::parse(s);
		jerry_value_t ret;
		jsonVal2jerryVal(j, ret);
		return ret;
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_str_toHexStr(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json numArr = jArgs[0];
		if (numArr.is_array()) {
			vector<unsigned char> arr;
			for (int i = 0; i < numArr.size(); i++) {
				json j = numArr[i];
				unsigned char b = j.get<unsigned char>();
				arr.push_back(b);
			}
			string s;
			for (size_t i = 0; i < arr.size(); i++)
			{
				string sb = str::format("%02X ", (unsigned char)arr[i]);
				s += sb;
			}
			json jRet = s;
			jerry_value_t ret;
			jsonVal2jerryVal(jRet, ret);
			return ret;
		}
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_ioDev_setOnline(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	ioDev* temp = (ioDev*)(pEngine->m_ioDevThis);;
	temp->setOnline();
	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_ioDev_setDevVar(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 2)
	{
		json varName = jArgs[0];
		if (!varName.is_string()) {
			return jerry_create_null();
		}
		string sName = varName.get<string>();
		json params = jArgs[1];

		jerry_value_t dev = call_info_p->this_value;
		json jDev;
		jerryVal2jsonVal(dev, jDev);
		if (!jDev.is_object())
			return jerry_create_boolean(false);
		if (jDev["confNodeId"] == nullptr)
			return jerry_create_boolean(false);
		string confNodeId = jDev["confNodeId"];
		ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
		if (!p)
			return jerry_create_boolean(false);

		p->m_mapDevVar[sName] = params;
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_ioDev_onRecvData(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	jerry_value_t dev = call_info_p->this_value;
	json jDev;
	jerryVal2jsonVal(dev, jDev);

	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1)
		return jerry_create_null();
	
	json data = jArgs[0];

	if (!data.is_array())
		return jerry_create_null();

	vector<unsigned char> vecData;
	for (auto& i : data) {
		if (i.is_number_integer()) {
			unsigned char b = i.get<int>();
			vecData.push_back(b);
		}
	}

	if (jDev.is_object() && jDev["confNodeId"] != nullptr) {
		string confNodeId = jDev["confNodeId"];
		ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
		if (p) {
			p->onRecvData(vecData.data(), vecData.size());
		}
	}

	return jerry_create_null();
}


jerry_value_t func_ioDev_doTransaction(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	jerry_value_t dev = call_info_p->this_value;
	json jDev;
	jerryVal2jsonVal(dev, jDev);

	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1)
		return jerry_create_null();

	json req = jArgs[0];

	vector<uint8_t> vecReq;
	if (req.is_array()) {
		for (auto& i : req) {
			if (i.is_number_integer()) {
				uint8_t b = i.get<int>();
				vecReq.push_back(b);
			}
		}
	}
	else if(req.is_string()) {
		string s = req.get<string>();
		vecReq = str::toBytes(s);
	}
	else {
		pEngine->m_sError = "错误的请求参数格式，必须是数组或者字符串";
		return jerry_create_null();
	}


	vector<uint8_t> vecResp;
	if (jDev.is_object() && jDev["confNodeId"] != nullptr) {
		string confNodeId = jDev["confNodeId"];
		ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
		if (p && p->m_devType == "custom-device") {
			ioDev_custom* pc = (ioDev_custom*)p;
			pc->doTransaction(vecReq,vecResp);
		}
	}

	if (vecResp.size() > 0) {
		json j = json::array();
		for (int i = 0; i < vecResp.size(); i++) {
			j.push_back(vecResp[i]);
		}
		jerry_value_t jrr;
		jsonVal2jerryVal(j, jrr);
		return jrr;
	}

	return jerry_create_null();
}

jerry_value_t func_ioDev_getDevVar(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() == 1)
	{
		json varName = jArgs[0];
		if (!varName.is_string()) {
			return jerry_create_null();
		}
		string sName = varName.get<string>();

		jerry_value_t dev = call_info_p->this_value;
		json jDev;
		jerryVal2jsonVal(dev, jDev);
		if (!jDev.is_object())
			return jerry_create_boolean(false);
		if (jDev["confNodeId"] == nullptr)
			return jerry_create_boolean(false);
		string confNodeId = jDev["confNodeId"];
		ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
		if (!p)
			return jerry_create_boolean(false);

		json val = p->m_mapDevVar[sName];
		jerry_value_t jerryVal;
		jsonVal2jerryVal(val, jerryVal);
		return jerryVal;
	}

	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_ioDev_setOffline(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	jerry_value_t dev = call_info_p->this_value;
	json jDev;
	jerryVal2jsonVal(dev, jDev);
	if (!jDev.is_object())
		return jerry_create_boolean(false);
	if (jDev["confNodeId"] == nullptr)
		return jerry_create_boolean(false);
	string confNodeId = jDev["confNodeId"];
	ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
	if (!p)
		return jerry_create_boolean(false);
	p->setOffline();
	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_ioDev_input(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	if (jArgs.size() >= 2)
	{
		jerry_value_t dev = call_info_p->this_value;
		json jDev;
		jerryVal2jsonVal(dev, jDev);
		if (!jDev.is_object())
			return jerry_create_boolean(false);
		if(jDev["confNodeId"] == nullptr)
			return jerry_create_boolean(false);
		string confNodeId = jDev["confNodeId"];
		ioDev* p = ioSrv.getIODevByNodeID(confNodeId);
		if(!p)
			return jerry_create_boolean(false);

		json jVal = jArgs[0];
		json addr = jArgs[1];
		string chanAddr = addr.get<string>();
		bool bRet = p->input(jVal, chanAddr);
		return jerry_create_boolean(bRet);
	}

	jerry_value_t ret = jerry_create_boolean(false);
	return ret;
}


void TIMEToJerryTime(TIME& t, jerry_value_t& time) {
	jerry_value_t prop_name, prop_value, set_result;
	uint64_t intVal;

	prop_name = jerry_create_string((const jerry_char_t*)"year");
	intVal = t.wYear;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"month");
	intVal = t.wMonth;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"day");
	intVal = t.wDay;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"hour");
	intVal = t.wHour;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"minute");
	intVal = t.wMinute;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"second");
	intVal = t.wSecond;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"millisecond");
	intVal = t.wMilliseconds;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(time, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);
}


jerry_value_t func_fromStr(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}
	if (!jArgs[0].is_string()){
		jerry_value_t ret = jerry_create_null();
		return ret;
	}

	jerry_value_t time = call_info_p->this_value;
	string strTime = jArgs[0].get<string>();
	TIME t;
	t.fromStr(strTime);
	TIMEToJerryTime(t, time);

	jerry_value_t ret = jerry_create_null();
	return ret;
}

jerry_value_t func_toUnixTime(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	jerry_value_t time = call_info_p->this_value;

	json jTime;
	jerryVal2jsonVal(time, jTime);
	TIME t;
	t.wYear = jTime["year"].get<int>();
	t.wMonth = jTime["month"].get<int>();
	t.wDay = jTime["day"].get<int>();
	t.wHour = jTime["hour"].get<int>();
	t.wMinute = jTime["minute"].get<int>();
	t.wSecond = jTime["second"].get<int>();
	t.wMilliseconds = jTime["millisecond"].get<int>();
	time_t tt = t.toUnixTime();
	uint64_t us64time = tt;
	jerry_value_t jv_unix_time = jerry_create_bigint(&us64time, 1, false);

	return jv_unix_time;
}


jerry_value_t func_toStr(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	jerry_value_t time = call_info_p->this_value;
	
	json jTime;
	jerryVal2jsonVal(time, jTime);
	TIME t;
	t.wYear = jTime["year"].get<int>();
	t.wMonth = jTime["month"].get<int>();
	t.wDay = jTime["day"].get<int>();
	t.wHour = jTime["hour"].get<int>();
	t.wMinute = jTime["minute"].get<int>();
	t.wSecond = jTime["second"].get<int>();
	t.wMilliseconds = jTime["millisecond"].get<int>();
	string sTime = t.toStr();
	jerry_value_t jv_str_time = jerry_create_string((const jerry_char_t*)sTime.c_str());

	return jv_str_time;
}

jerry_value_t func_increaseSeconds(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}
	if (!jArgs[0].is_number_integer()) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}


	jerry_value_t time = call_info_p->this_value;
	json jTime;
	jerryVal2jsonVal(time, jTime);
	TIME t;
	t.wYear = jTime["year"].get<int>();
	t.wMonth = jTime["month"].get<int>();
	t.wDay = jTime["day"].get<int>();
	t.wHour = jTime["hour"].get<int>();
	t.wMinute = jTime["minute"].get<int>();
	t.wSecond = jTime["second"].get<int>();
	t.wMilliseconds = jTime["millisecond"].get<int>();
	time_t unixTime = t.toUnixTime();
	unixTime += jArgs[0].get<int>();
	t.fromUnixTime(unixTime);
	TIMEToJerryTime(t, time);
	
	jerry_value_t ret = jerry_create_null();
	return ret;
}


jerry_value_t func_time(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);

	TIME t = timeopt::now();

	uint64_t intVal = 0;
	jerry_value_t timeObj = jerry_create_object();
	jerry_value_t prop_name, prop_value, set_result;

	prop_name = jerry_create_string((const jerry_char_t*)"year");
	intVal = t.wYear;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"month");
	intVal = t.wMonth;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);
	
	prop_name = jerry_create_string((const jerry_char_t*)"day");
	intVal = t.wDay;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"hour");
	intVal = t.wHour;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);


	prop_name = jerry_create_string((const jerry_char_t*)"minute");
	intVal = t.wMinute;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);


	prop_name = jerry_create_string((const jerry_char_t*)"second");
	intVal = t.wSecond;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"millisecond");
	intVal = t.wMilliseconds;
	prop_value = jerry_create_bigint(&intVal, 1, false);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);


	prop_name = jerry_create_string((const jerry_char_t*)"toStr");
	prop_value = jerry_create_external_function(func_toStr);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"fromStr");
	prop_value = jerry_create_external_function(func_fromStr);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);


	prop_name = jerry_create_string((const jerry_char_t*)"increaseSeconds");
	prop_value = jerry_create_external_function(func_increaseSeconds);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	prop_name = jerry_create_string((const jerry_char_t*)"toUnixTime");
	prop_value = jerry_create_external_function(func_toUnixTime);
	set_result = jerry_set_property(timeObj, prop_name, prop_value);
	jerry_release_value(set_result);
	jerry_release_value(prop_name);
	jerry_release_value(prop_value);

	return timeObj;
}

//openSerial(string portName, int baudRate, string parity, int byteSize, int stopBits)
jerry_value_t func_openSerial(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 5) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}

	string errorInfo;

	string portName = jArgs[0].get<string>();
	int baudRate = jArgs[1].get<int>();
	string parity = jArgs[2].get<string>();
	int byteSize = jArgs[3].get<int>();
	string stopBits = jArgs[4].get<string>();

	HANDLE hCom = nullptr;

	bool ret = false;
	string  strComPort = "\\\\.\\" + portName;

	hCom = CreateFile(strComPort.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, // 独占方式
		NULL,
		OPEN_EXISTING,// 打开而不是创建
		FILE_FLAG_OVERLAPPED,
		NULL);

	if (hCom == INVALID_HANDLE_VALUE)
	{
		errorInfo = sys::getLastError("CreateFile");
		goto OPEN_END;
	}

	COMSTAT comstat;
	DWORD dwError;
	ClearCommError(hCom, &dwError, &comstat);

	//dcb.StopBits = 0, 1, 2对应的是1bit, 1.5bits, 2bits.
	//dcb.ByteSize = 6, 7, 8时   dcb.StopBits不能为1
	//dcb.ByteSize = 5时   dcb.StopBits不能为2
	DCB dcb;
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	GetCommState(hCom, &dcb);
	dcb.BaudRate = baudRate;
	dcb.ByteSize = byteSize;
	dcb.Parity = tJSEngine::parseParity(parity);
	dcb.StopBits = tJSEngine::parseStopBits(stopBits);
	if (!SetCommState(hCom, &dcb))
	{
		errorInfo = sys::getLastError("SetCommState");
		CloseHandle(hCom);
		hCom = nullptr;
		goto OPEN_END;
	}

	SetupComm(hCom, 1024, 1024);

	COMMTIMEOUTS CommTimeouts;
	ZeroMemory(&CommTimeouts, sizeof(CommTimeouts));
	CommTimeouts.ReadIntervalTimeout = 200;
	CommTimeouts.ReadTotalTimeoutMultiplier = 0;
	CommTimeouts.ReadTotalTimeoutConstant = 2000;
	CommTimeouts.WriteTotalTimeoutMultiplier = 0;
	CommTimeouts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts(hCom, &CommTimeouts);

	PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	SetCommMask(hCom, EV_RXCHAR);
	ret = true;

OPEN_END:
	if (ret) {
		LOG("[warn][串口   ]串口打开成功,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s", portName.c_str(), baudRate, byteSize, stopBits.c_str(), parity.c_str());
	}
	else
		LOG("[warn][串口   ]串口打开失败,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s,错误信息:%s", portName.c_str(), baudRate, byteSize, stopBits.c_str(), parity.c_str(), errorInfo.c_str());
	
	if (ret) {
		string sHandle = tJSEngine::pointerToString(hCom);
		jerry_value_t ret = jerry_create_string((const jerry_char_t*)sHandle.c_str());
		return ret;
	}
	else {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}
}

//readSerial(string handle)
jerry_value_t func_readSerial(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}

	string sH = jArgs[0].get<string>();
	void* hCom = tJSEngine::stringToPointer(sH);
	COMSTAT comstat;
	DWORD dwError;
	bool ret = false;
	unsigned char buf[500 * 1000] = { 0 };
	int iLen = 0;
	BOOL bReadRet = 0;

	OVERLAPPED ovWaitEvent;
	ovWaitEvent.hEvent = CreateEvent(
		NULL,   // default security attributes 
		TRUE,   // manual-reset event 
		FALSE,  // not signaled 
		NULL    // no name
	);

	OVERLAPPED m_ovRead;
	m_ovRead.hEvent = CreateEvent(
		NULL,   // default security attributes 
		TRUE,   // manual-reset event 
		FALSE,  // not signaled 
		NULL    // no name
	);

	DWORD dwEvtMask = 0;
	//等待用SetCommMask()函数设置的串口事件发生，共有9种事件可被监视：
	//EV_BREAK，EV_CTS，EV_DSR，EV_ERR，EV_RING，EV_RLSD，EV_RXCHAR，
	//EV_RXFLAG，EV_TXEMPTY；当其中一个事件发生或错误发生时，函数将
	//OVERLAPPED结构中的事件置为有信号状态，并将事件掩码填充到dwMask参数中
	//在openCom函数里面设置了EV_RXCHAR事件

	//如果异步操作不能立即完成的话,函数返回FALSE,并且调用GetLastError()函
	//数分析错误原因后返回ERROR_IO_PENDING,指示异步操作正在后台进行.这种情
	//况下,在函数返回之前系统设置OVERLAPPED结构中的事件为无信号状态
	if (WaitCommEvent(hCom, &dwEvtMask, &ovWaitEvent))
	{
	}
	else
	{
		DWORD dwRet = GetLastError();
		if (ERROR_IO_PENDING == dwRet)
		{
			DWORD dwBytesRead = 0;
			//https://docs.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult
			//bWait=TRUE等待层叠读取操作完成
			//CloseHandle关闭m_hCom可以使得阻塞的函数返回
			BOOL bResult = GetOverlappedResult(hCom, &ovWaitEvent, &dwBytesRead, TRUE); // 阻塞  Block
			if (bResult) {

			}
			else {
				ret = false;
				goto READ_END;
			}
		}
		else if (ERROR_ACCESS_DENIED == dwRet)
		{
			//usb 串口 虚拟串口等，在串口被打开的情况下删除了设备，拔出了usb线等，进入到这里
			LOG("[error]hardware serial is deleted,check your hardware connection!");
			ret = false;
			goto READ_END;
		}
		else {
			ret = false;
			goto READ_END;
		}
	}
	ClearCommError(hCom, &dwError, &comstat);

	if (comstat.cbInQue == 0) {
		ret = false;
		goto READ_END;
	}

	assert(comstat.cbInQue < 500 * 1000);

	bReadRet = ReadFile(hCom, (LPVOID)(buf), comstat.cbInQue, (LPDWORD)&iLen, &m_ovRead);//该操作立即返回，因为缓冲区已经有数据
	if (!bReadRet) {
		ret = false;
		goto READ_END;
	}
	if (iLen == 0) {
		ret = false;
		goto READ_END;
	}

READ_END:
	CloseHandle(ovWaitEvent.hEvent);
	CloseHandle(m_ovRead.hEvent);

	if (ret) {
		json j = json::array();
		for (int i = 0; i < iLen; i++) {
			j.push_back(buf[i]);
		}
		jerry_value_t jrr;
		jsonVal2jerryVal(j, jrr);
		return jrr;
	}
	else {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}
}


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

	// setReturn函数
	property_name = jerry_create_string((const jerry_char_t*)"setReturn");
	property_func = jerry_create_external_function(func_setReturn);
	set_result = jerry_set_property(global_object, property_name, property_func);
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


	property_name = jerry_create_string((const jerry_char_t*)"time");
	property_func = jerry_create_external_function(func_time);
	set_result = jerry_set_property(global_object, property_name, property_func);
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	//readSerial
	property_name = jerry_create_string((const jerry_char_t*)"readSerial");
	property_func = jerry_create_external_function(func_readSerial);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);

	//openSerial
	property_name = jerry_create_string((const jerry_char_t*)"openSerial");
	property_func = jerry_create_external_function(func_openSerial);
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


	//db对象
	{
		jerry_value_t obj = jerry_create_object();
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"db");

		jerry_value_t obj_prop_name = jerry_create_string((const jerry_char_t*)"insert");
		jerry_value_t obj_prop_func = jerry_create_external_function(func_db_insert);
		jerry_release_value(jerry_set_property(obj, obj_prop_name, obj_prop_func));
		jerry_release_value(obj_prop_name);
		jerry_release_value(obj_prop_func);

		jerry_release_value(jerry_set_property(global_object, prop_name, obj));
		jerry_release_value(prop_name);
		jerry_release_value(obj);
	}

	//http对象
	{
		jerry_value_t obj = jerry_create_object();
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"http");

		jerry_value_t obj_prop_name = jerry_create_string((const jerry_char_t*)"request");
		jerry_value_t obj_prop_func = jerry_create_external_function(func_http_request);
		jerry_release_value(jerry_set_property(obj, obj_prop_name, obj_prop_func));
		jerry_release_value(obj_prop_name);
		jerry_release_value(obj_prop_func);

		jerry_release_value(jerry_set_property(global_object, prop_name, obj));
		jerry_release_value(prop_name);
		jerry_release_value(obj);
	}

	//JSON对象
	{
		jerry_value_t obj = jerry_create_object();
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"JSON");

		jerry_value_t obj_prop_name = jerry_create_string((const jerry_char_t*)"stringify");
		jerry_value_t obj_prop_func = jerry_create_external_function(func_json_stringify);
		jerry_release_value(jerry_set_property(obj, obj_prop_name, obj_prop_func));
		jerry_release_value(obj_prop_name);
		jerry_release_value(obj_prop_func);

		 obj_prop_name = jerry_create_string((const jerry_char_t*)"parse");
		 obj_prop_func = jerry_create_external_function(func_json_parse);
		jerry_release_value(jerry_set_property(obj, obj_prop_name, obj_prop_func));
		jerry_release_value(obj_prop_name);
		jerry_release_value(obj_prop_func);

		jerry_release_value(jerry_set_property(global_object, prop_name, obj));
		jerry_release_value(prop_name);
		jerry_release_value(obj);
	}

	//STR对象
	{
		jerry_value_t obj = jerry_create_object();
		jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"STR");

		jerry_value_t obj_prop_name = jerry_create_string((const jerry_char_t*)"toHexStr");
		jerry_value_t obj_prop_func = jerry_create_external_function(func_str_toHexStr);
		jerry_release_value(jerry_set_property(obj, obj_prop_name, obj_prop_func));
		jerry_release_value(obj_prop_name);
		jerry_release_value(obj_prop_func);

		jerry_release_value(jerry_set_property(global_object, prop_name, obj));
		jerry_release_value(prop_name);
		jerry_release_value(obj);
	}

	return true;
}

//pDev不直接使用ioDev是因为的伤损也使用了对应的jerry的内容,不能依赖于ioDev的类
bool initIODevFunc(jerry_value_t obj,void* pDev) {
	ioDev* pDevTemp = (ioDev*)pDev;
	jerry_value_t n = jerry_create_string((const jerry_char_t*)"input");
	jerry_value_t v = jerry_create_external_function(func_ioDev_input);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"addr");
	jsonVal2jerryVal(pDevTemp->m_jDevAddr, v);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"confNodeId");
	v = jerry_create_string((const jerry_char_t*)pDevTemp->m_confNodeId.c_str());
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"setOnline");
	v = jerry_create_external_function(func_ioDev_setOnline);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"setOffline");
	v = jerry_create_external_function(func_ioDev_setOffline);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"setDevVar");
	v = jerry_create_external_function(func_ioDev_setDevVar);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"getDevVar");
	v = jerry_create_external_function(func_ioDev_getDevVar);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"onRecvData");
	v = jerry_create_external_function(func_ioDev_onRecvData);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	n = jerry_create_string((const jerry_char_t*)"doTransaction");
	v = jerry_create_external_function(func_ioDev_doTransaction);
	jerry_release_value(jerry_set_property(obj, n, v));
	jerry_release_value(n);
	jerry_release_value(v);

	if (pDevTemp->m_vecChildDev.size() > 0) {
		n = jerry_create_string((const jerry_char_t*)"children");
		jerry_value_t vecChild = jerry_create_array((uint32_t)pDevTemp->m_vecChildDev.size());
		for (size_t i = 0; i < pDevTemp->m_vecChildDev.size(); i++) {
			ioDev* pChildDev = pDevTemp->m_vecChildDev[i];
			jerry_value_t jerryChildDev = jerry_create_object();
			initIODevFunc(jerryChildDev, pChildDev);
			jerry_set_property_by_index(vecChild, (uint32_t)i, jerryChildDev);
			jerry_release_value(jerryChildDev);
		}
		jerry_release_value(jerry_set_property(obj, n, vecChild));
		jerry_release_value(n);
		jerry_release_value(vecChild);
	}

	return true;
}
#endif
