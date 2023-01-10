#include "pch.h"
#include "ScriptEngine.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "obj.h"
#include "jerryscript-port.h"



#ifdef ENABLE_JERRY_SCRIPT

bool ScriptEngine::initGlobalFunc()
{
	GLOBAL_FUNC gf;
	jerry_value_t& property_name = gf.property_name;
	jerry_value_t& property_func = gf.property_func;
	// getMp函数
	property_name = jerry_create_string((const jerry_char_t*)"getMo");
	property_func = jerry_create_external_function(func_getMp);
	jerry_value_t set_result = jerry_set_property(global_object, property_name,property_func);
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

	return true;
}

void ScriptEngine::releaseGlobalFunc() {
	for (int i = 0; i < m_vecGlobalFunc.size(); i++) {
		GLOBAL_FUNC gf = m_vecGlobalFunc[i];
		jerry_release_value(gf.property_func);
		jerry_release_value(gf.property_name);
	}
	m_vecGlobalFunc.clear();
}

string ScriptEngine::getErrorDesc(jerry_error_t err) {
	if (err == JERRY_ERROR_NONE) {
		return "error none";
	}
	else if (err == JERRY_ERROR_COMMON) {
		return "error";
	}
	else if (err == JERRY_ERROR_EVAL) {
		return "eval error";
	}
	else if (err == JERRY_ERROR_RANGE) {
		return "range error";
	}
	else if (err == JERRY_ERROR_REFERENCE) {
		return "reference error";
	}
	else if (err == JERRY_ERROR_SYNTAX) {
		return "syntax error";
	}
	else if (err == JERRY_ERROR_TYPE) {
		return "type error";
	}
	else if (err == JERRY_ERROR_URI) {
		return "uri error";
	}
	else if (err == JERRY_ERROR_AGGREGATE) {
		return "aggregate error";
	}
	else {
		return "error none";
	}
}

void* context_alloc_fn(size_t size, void* cb_data)
{
	(void)cb_data;
	return malloc(size);
}

thread_local jerry_context_t* tls_context;
thread_local ScriptEngine* pEngine;
jerry_context_t* jerry_port_get_current_context(void)
{
	/* Returns the context assigned to the thread. */
	return tls_context;
}

bool ScriptEngine::runScript(string& script)
{
	vector<string> lines;
	//script = str::replace(script, "\r\n", "\n");
	//str::split(lines, script, "\n");
	lines.push_back(script);
	m_vecOutput.clear();
	try {
		pEngine = this;
		tls_context = jerry_create_context(512 * 1024,context_alloc_fn,NULL);;
		jerry_init(JERRY_INIT_EMPTY);
		global_object = jerry_get_global_object();

		initGlobalFunc();


		for (int i = 0; i < lines.size(); i++) {
			string line = lines[i];
			line = str::trim(line);
			if (line == "")
				continue;
			///* Run the demo script with 'eval' */
			jerry_value_t eval_ret = jerry_eval((jerry_char_t*)line.c_str(),
				line.length(),
				JERRY_PARSE_NO_OPTS);

			/* Check if there was any error (syntax or runtime) */
			bool run_ok = !jerry_value_is_error(eval_ret);

			if (run_ok)
			{
				m_jEvalRet = json::object();
				jerryVal2jsonVal(eval_ret, m_jEvalRet);
				jerry_release_value(eval_ret);
			}
			else
			{
				jerry_error_t error = jerry_get_error_type(eval_ret);
				string sErr = getErrorDesc(error);
				//m_vecOutput.push_back("脚本执行错误,第" + str::fromInt(i+1) +"行,错误类型:" + sErr);
				m_vecOutput.push_back("脚本执行错误,错误类型:" + sErr);
				jerry_release_value(eval_ret);
				break;
			}
		}

		

		releaseGlobalFunc();
		jerry_release_value(global_object);

		jerry_cleanup();
		free(tls_context);
	}
	catch (std::exception& e)
	{
		string s = e.what();
		m_vecOutput.push_back(s);
		return false;
	}
	return true;
}
#endif
