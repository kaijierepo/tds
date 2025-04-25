#ifdef ENABLE_JERRY_SCRIPT
#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "jerryscript.h"

using json = nlohmann::json;
using namespace std;

struct GLOBAL_FUNC {
	jerry_value_t property_name;
	jerry_value_t property_func;
};

extern json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count);
extern void jsonVal2jerryVal(json& jVal, jerry_value_t& jerryVal);
extern bool getScriptEngineObj(json& jObj, jerry_value_t engineObj);
extern void jerryVal2jsonVal(jerry_value_t jerryVal, json& jVal);
extern bool jerryItem2JsonItem(const jerry_value_t prop_name, const jerry_value_t prop_value, void* user_data_p);

bool initScriptFunc(jerry_value_t global_object, vector<GLOBAL_FUNC>& m_vecGlobalFunc);
#endif