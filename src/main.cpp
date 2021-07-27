#include "pch.h"
#include "conf.h"
#include "cmdparser.hpp"
#include "video/remoteDesktopServer.h"
#include "cmdparser.hpp"
#include "logger.h"
#include "tds_imp.h"
#include "wke.h"
#include "tools/tcpRoute.h"

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

代码不安全，未来需优化的地方，全局搜索 [unsafe]

*/



#include "ioDev_mqttBroker.h"

class TDS_imp;
TDS_imp tdsImp; //tds instance;
iTDS* tds = &tdsImp;


#ifndef _WINDLL
#pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )//不显示默认控制台
int main(int argc, char** argv)
{
	//use cmd line conf first ,or use tds.json 
	cli::Parser parser(argc, argv);
	parser.set_optional<string>("m", "mode", "tds", "tds mode; route mode;");
	parser.set_optional<int>("sl", "serverleft", 666, "");
	parser.set_optional<int>("sr", "serverright", 667, "");
	parser.set_optional<int>("p", "port", 0, "Integers in all forms, e.g., unsigned int, long long, ..., are possible. Hexadecimal and Ocatl numbers parsed as well");
	parser.set_optional<bool>("d", "debug", false, "run in debug mode. heartbeat will be closed;more log will be added;");
	parser.set_optional<string>("l", "loglevel", "debug", "value can be detail,trace,debug,warn,error");
	parser.run_and_exit_if_error();
	tds->conf->port = parser.get<int>("p");
	tds->conf->debugMode = parser.get<bool>("d");
	tds->conf->logLevel = parser.get<string>("l");

	string mode = parser.get<string>("m");
	if (mode == "route")
	{
		createConsole();
		tcpRoute* tr = new tcpRoute();
		tr->portLeft = parser.get<int>("sl");
		tr->portRight = parser.get<int>("sr");
		tr->run();
	}
	else
	{
		//run tds
		tds->run();
	}


	// 消息循环  
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}
#endif // !_WINDLL

#define DllExport   extern "C" __declspec( dllexport )
DllExport iTDS* getTds() {
	return &tdsImp;
}






