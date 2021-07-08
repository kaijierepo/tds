#include "pch.h"
#include "conf.h"
#include "cmdparser.hpp"
#include "video/remoteDesktopServer.h"
#include "cmdparser.hpp"
#include "logger.h"
#include "tds_imp.h"

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

int main(int argc, char** argv)
{
	//use cmd line conf first ,or use tds.json 
	cli::Parser parser(argc, argv);
	parser.set_optional<int>("p", "port", 0, "Integers in all forms, e.g., unsigned int, long long, ..., are possible. Hexadecimal and Ocatl numbers parsed as well");
	parser.set_optional<bool>("d", "debug", false, "run in debug mode. heartbeat will be closed;more log will be added;");
	parser.set_optional<string>("l", "loglevel", "debug", "value can be detail,trace,debug,warn,error");
	parser.run_and_exit_if_error();
	tds->conf->port = parser.get<int>("p");
	tds->conf->debugMode = parser.get<bool>("d");
	string strLogLevel = parser.get<string>("l");
	logger.setLogLevel(strLogLevel);
	LOG("current log Level is:" + strLogLevel);

	tds->run();

	while (1)
	{
		Sleep(1000);
	}
	return 0;
}


#define DllExport   extern "C" __declspec( dllexport )
DllExport iTDS* getTds() {
	return &tdsImp;
}






