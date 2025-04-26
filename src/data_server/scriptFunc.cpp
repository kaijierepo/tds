#ifdef ENABLE_JERRY_SCRIPT
#include "scriptEngine.h"
#include "logger.h"
#include "tds.h"
#include "httplib.h"
#include <string>
#include <sstream>
#include <cstdint>
#include "scriptFunc.h"
#include "common.h"

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
	else {
		jerryVal = jerry_create_null();
	}
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

		if (pEngine->m_logImp) {
			pEngine->m_logImp(log, pEngine, logToTds);
		}
	}

	return jerry_create_undefined();
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

	//预处理打开参数
	string errorInfo;
	string portName = jArgs[0].get<string>();
	int baudRate = jArgs[1].get<int>();
	string parity = jArgs[2].get<string>();
	int byteSize = jArgs[3].get<int>();
	string stopBits = jArgs[4].get<string>();
	HANDLE hCom = nullptr;
	bool ret = false;
	string  strComPort = "\\\\.\\" + portName;
	COMMTIMEOUTS timeouts = { 0 };

	//打开串口（同步模式）
	hCom = CreateFileA(strComPort.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0, // 独占方式
		NULL,
		OPEN_EXISTING,// 打开而不是创建
		0,            // 同步模式（无 FILE_FLAG_OVERLAPPED）
		NULL);
	if (hCom == INVALID_HANDLE_VALUE)
	{
		errorInfo = sys::getLastError("CreateFile");
		goto OPEN_END;
	}

	//配置串口参数
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

	//设置超时时间
	timeouts.ReadIntervalTimeout = 50;         // 字符间超时（毫秒）
	timeouts.ReadTotalTimeoutConstant = 100;   // 固定超时
	timeouts.ReadTotalTimeoutMultiplier = 10;  // 每字节附加超时
	timeouts.WriteTotalTimeoutConstant = 2000;  // 最大阻塞 1000ms
	if (!SetCommTimeouts(hCom, &timeouts)) {
		printf("设置超时失败，错误代码: %d\n", GetLastError());
		CloseHandle(hCom);
		goto OPEN_END;
	}
	ret = true;

OPEN_END:
	if (ret) {
		LOG("[warn][串口   ]串口打开成功,串口号:%s,baudRate:%d,byteSize:%d,stopBits:%s,parity:%s,串口句柄:%p", portName.c_str(), baudRate, byteSize, stopBits.c_str(), parity.c_str(),hCom);
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
	DWORD dwError;
	bool ret = false;
	unsigned char buf[50000] = { 0 };
	int iLen = 0;
	BOOL bReadRet = 0;

	bReadRet = ReadFile(hCom, (LPVOID)(buf), 50000, (LPDWORD)&iLen, NULL);//阻塞读取
	dwError = GetLastError();
	if(dwError != 0)
		LOG("[warn]ReadFile Error %d", dwError);

READ_END:
	if (iLen > 0) {
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

jerry_value_t func_writeSerial(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 2) {
		jerry_value_t ret = jerry_create_boolean(false);
		return ret;
	}

	string sH = jArgs[0].get<string>();
	void* hCom = tJSEngine::stringToPointer(sH);
	json jData = jArgs[1];
	vector<unsigned char> vec;
	string sData;
	char* pData = nullptr;
	int len = 0;
	if (jData.is_array()) {
		for (int i = 0; i < jData.size(); i++) {
			unsigned char b = jData[i].get<unsigned char>();
			vec.push_back(b);
		}
		pData = (char*)vec.data();
		len = vec.size();
	}
	else if(jData.is_string()) {
		sData = jData.get<string>();
		pData = (char*)sData.c_str();
		len = sData.length();
	}
	else {
		jerry_value_t ret = jerry_create_boolean(false);
		return ret;
	}


	DWORD bytesWritten;
	if (WriteFile(
		hCom,                   // 串口句柄
		pData,                      // 数据缓冲区
		len,              // 数据长度
		&bytesWritten,             // 实际写入的字节数
		NULL                       // 同步模式设为 NULL
	)) {
		jerry_value_t ret = jerry_create_boolean(true);
		return ret;
	}
	else {
		jerry_value_t ret = jerry_create_boolean(false);
		return ret;
	}
}


jerry_value_t func_closeSerial(const jerry_call_info_t* call_info_p,
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
	if (hCom != nullptr) {
		CloseHandle(hCom);
	}
}


jerry_value_t func_arrayToStr(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}

	json jArr = jArgs[0];
	vector<char> charArray;
	charArray.resize(jArr.size() + 1);
	for (int i = 0; i < jArr.size(); i++) {
		unsigned char b = jArr[i].get<unsigned char>();
		char cb = *((char*)&b);
		charArray[i] = cb;
	}

	charArray[jArr.size()] = 0;
	string s = (char*) charArray.data();

	jerry_value_t ret = jerry_create_string((const jerry_char_t*)s.c_str());
	return ret;
}

jerry_value_t func_strToArray(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	json jArgs = engineArgsToJson(arguments, argument_count);
	if (jArgs.size() != 1) {
		jerry_value_t ret = jerry_create_null();
		return ret;
	}

	string s = jArgs[0];
	json jArr = json::array();
	for (int i = 0; i < s.length(); i++) {
		char cb = s[i];
		unsigned char ucb = *((unsigned char*)&cb);
		jArr.push_back(ucb);
	}
	jerry_value_t ret;
	jsonVal2jerryVal(jArr, ret);
	return ret;
}

bool initScriptFunc(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc)
{
	GLOBAL_FUNC gf;
	jerry_value_t& property_name = gf.property_name;
	jerry_value_t& property_func = gf.property_func;
	jerry_value_t set_result = 0;

	// log函数
	property_name = jerry_create_string((const jerry_char_t*)"log");
	property_func = jerry_create_external_function(func_log);
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

	//writeSerial
	property_name = jerry_create_string((const jerry_char_t*)"writeSerial");
	property_func = jerry_create_external_function(func_writeSerial);
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

	//openSerial
	property_name = jerry_create_string((const jerry_char_t*)"closeSerial");
	property_func = jerry_create_external_function(func_closeSerial);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);


	property_name = jerry_create_string((const jerry_char_t*)"arrayToStr");
	property_func = jerry_create_external_function(func_arrayToStr);
	set_result = jerry_set_property(global_object, property_name, property_func);
	if (jerry_value_is_error(set_result)) {
	}
	jerry_release_value(set_result);
	m_vecGlobalFunc.push_back(gf);


	property_name = jerry_create_string((const jerry_char_t*)"strToArray");
	property_func = jerry_create_external_function(func_strToArray);
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

#endif
