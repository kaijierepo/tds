// t-js.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "scriptEngine.h"
#include "scriptFunc.h"
#include "common.h"
#include "logger.h"

void script_logImp(string log, ScriptEngine* pEngine, bool logToHost) {
	log += "\r\n";
	printf(charCodec::utf8_to_gb(log).c_str());
}

int main()
{
	string sPath = fs::appPath() + "/tmain.js";
	if (fs::fileExist(sPath)) {
		string script;
		fs::readFile(sPath,script);
		ScriptEngine se;
		se.m_logImp = script_logImp;
		se.m_initGlobalFunc = initScriptFunc;
		se.runScript(script, "");
		for (int i = 0; i < se.m_vecOutput.size(); i++) {
			LOG(se.m_vecOutput[i]);
		}
	}
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
