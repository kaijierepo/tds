#include "pch.h"
#include "scriptHost.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "mo.h"

scriptHost sHost;


static jerry_value_t func_getMo(const jerry_call_info_t* call_info_p,
	const jerry_value_t arguments[],
	const jerry_length_t argument_count)
{
	/* No arguments are used in this example */
	/* Print out a static string */
	printf("Print handler was called\n");

	/* Return an "undefined" value to the JavaScript engine */
	return jerry_create_undefined();
}


bool scriptHost::init()
{
	jerry_init(JERRY_INIT_EMPTY);
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
	init();

	thread t(scriptThread, this);
	t.detach();

	thread t1(scriptThread1, this);
	t1.detach();
	return false;
}

void scriptHost::loopExe()
{
	
	jerry_value_t global_object = jerry_get_global_object();
	LOG("sHost global object %d", global_object);

	Sleep(100000);
	jerry_value_t property_name_getMo = jerry_create_string((const jerry_char_t*)"getMo");
	/* Create a function from a native C method (this function will be called from JS) */
	jerry_value_t property_value_func = jerry_create_external_function(func_getMo);


	/* Add the "print" property with the function value to the "global" object */
	jerry_value_t set_result = jerry_set_property(global_object, property_name_getMo, property_value_func);
	/* Check if there was no error when adding the property (in this case it should never happen) */
	if (jerry_value_is_error(set_result)) {
		printf("Failed to add the 'print' property\n");
	}
	/* Release all jerry_value_t-s */
	jerry_release_value(set_result);


	while (1)
	{
		Sleep(1000);

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
			jerry_release_value(eval_ret);

			if (run_ok)
			{
				bool bRunSuccess = jerry_value_to_boolean(eval_ret);
			}
			else
			{
			}
		}
	}

	jerry_release_value(property_value_func);
	jerry_release_value(property_name_getMo);
	jerry_release_value(global_object);
}


