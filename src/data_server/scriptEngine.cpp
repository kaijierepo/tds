#ifdef ENABLE_JERRY_SCRIPT
#include "ScriptEngine.h"
#include "jerryscript-port.h"
#include "scriptFunc.h"
#include "common.h"


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

thread_local ScriptEngine* pEngine;

ScriptEngine::ScriptEngine()
{
	m_initGlobalFunc = nullptr;
	m_ioDevThis = nullptr;
	m_logImp = nullptr;
	m_initIODevFunc = nullptr;
}

string jerryVal2Str(jerry_value_t jerryVal) {
	jerry_value_t string_value = jerry_json_stringify(jerryVal);
	jerry_size_t tSize = jerry_get_string_size(string_value);
	jerry_char_t* buffer = new jerry_char_t[tSize + 1];
	jerry_size_t copied_bytes = jerry_string_to_utf8_char_buffer(string_value, buffer, tSize);
	buffer[copied_bytes] = '\0';
	jerry_release_value(string_value);
	string s = (const char*)buffer;
	delete buffer;
	return s;
}

bool ScriptEngine::runScript(string& script, string user)
{
	m_script = script;
	m_user = user;
	//vector<string> lines;
	//script = str::replace(script, "\r\n", "\n");
	//str::split(lines, script, "\n");
	//lines.push_back(script);
	m_vecOutput.clear();
	bool runOk = false;
	try {
		TIME tStart = timeopt::now();
		pEngine = this;
		tls_context = jerry_create_context(512 * 1024,context_alloc_fn,NULL);;
		jerry_init(JERRY_INIT_EMPTY);
		global_object = jerry_get_global_object();

		if(m_initGlobalFunc)
			m_initGlobalFunc(global_object,m_vecGlobalFunc);

		if (m_initIODevFunc) {
			jerry_value_t ioDev = jerry_create_object();
			jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)"Dev");
			m_initIODevFunc(ioDev,m_ioDevThis);
			jerry_release_value(jerry_set_property(global_object, prop_name, ioDev));
			jerry_release_value(prop_name);
			jerry_release_value(ioDev);
		}

		if (!m_globalObj.is_null()) {
			for (auto& [key, value] : m_globalObj.items()) {
				jerry_value_t prop_name = jerry_create_string((const jerry_char_t*)key.c_str());
				jerry_value_t prop_value;
				jsonVal2jerryVal(value, prop_value);

				jerry_value_t set_result = jerry_set_property(global_object, prop_name, prop_value);
				if (jerry_value_is_error(set_result)) {
					jerry_error_t error = jerry_get_error_type(set_result);
					jerry_release_value(error);
				}
				jerry_release_value(set_result);
				jerry_release_value(prop_name);
				jerry_release_value(prop_value);
			}
		}

		///* Run the demo script with 'eval' */
		jerry_value_t eval_ret = jerry_eval((jerry_char_t*)script.c_str(),
			script.length(),
			JERRY_PARSE_NO_OPTS);

		/* Check if there was any error (syntax or runtime) */
		bool run_ok = !jerry_value_is_error(eval_ret);

		if (run_ok)
		{
			m_sEvalRet = jerryVal2Str(eval_ret);
			jerry_release_value(eval_ret);
			runOk = true;
		}
		else
		{
			jerry_error_t error = jerry_get_error_type(eval_ret);
			m_sError = getErrorDesc(error);
			//m_vecOutput.push_back("脚本执行错误,第" + str::fromInt(i+1) +"行,错误类型:" + sErr);
			m_vecOutput.push_back("脚本执行错误,错误类型:" + m_sError);
			jerry_release_value(eval_ret);
		}

		releaseGlobalFunc();
		jerry_release_value(global_object);

		jerry_cleanup();
		free(tls_context);

		int costMilli = timeopt::calcTimePassMilliSecond(tStart);
		m_vecOutput.push_back("执行耗时:" + str::fromInt(costMilli) + "ms");
	}
	catch (std::exception& e)
	{
		string s = e.what();
		m_vecOutput.push_back(s);
		return false;
	}
	return runOk;
}



#endif
