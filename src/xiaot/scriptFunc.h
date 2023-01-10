#include <string>
#include <map>
#include "json.hpp"
#ifdef ENABLE_JERRY_SCRIPT
#include "jerryscript.h"
#endif
#include "tdsSession.h"

using json = nlohmann::json;
using namespace std;

extern RPC_SESSION currentSession;

extern json engineArgsToJson(const jerry_value_t arguments[], const jerry_length_t argument_count);
extern void jsonVal2jerryVal(json& jVal, jerry_value_t& jerryVal);
extern void jerryVal2jsonVal(jerry_value_t jerryVal, json& jVal);

extern bool jerryItem2JsonItem(const jerry_value_t prop_name, const jerry_value_t prop_value, void* user_data_p);
extern bool getScriptEngineObj(json& jObj, jerry_value_t engineObj);

extern jerry_value_t func_log(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_output(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_input(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_call(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_sum(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_val(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_getMp(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t func_sleep(const jerry_call_info_t* call_info_p, const jerry_value_t arguments[], const jerry_length_t argument_count);
extern jerry_value_t backtrace_handler(const jerry_call_info_t* call_info_p, const jerry_value_t args_p[], const jerry_length_t args_count);