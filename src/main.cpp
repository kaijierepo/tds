#include "pch.h"
#include "prj.h"
#include "ds.h"
#include "ioSrv.h"
#include "cmdparser.hpp"
#include "video/remoteDesktopServer.h"
#include "cmdparser.hpp"
#include "logger.h"
#include "xiaot/xiaot.h"

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

*/

#include "ioDev_mqttBroker.h"
int main(int argc, char** argv)
{
	//use cmd line conf first ,or use tds.json 
	cli::Parser parser(argc, argv);
	parser.set_optional<int>("p", "port", 0, "Integers in all forms, e.g., unsigned int, long long, ..., are possible. Hexadecimal and Ocatl numbers parsed as well");
	parser.set_optional<bool>("d", "debug", false, "run in debug mode. heartbeat will be closed;more log will be added;");
	parser.set_optional<string>("l", "loglevel", "debug", "value can be detail,trace,debug,warn,error");
	parser.run_and_exit_if_error();
	ds.conf.port = parser.get<int>("p");
	ds.conf.debugMode = parser.get<bool>("d");
	string strLogLevel = parser.get<string>("l");
	logger.setLogLevel(strLogLevel);
	LOG("current log Level is:" + strLogLevel);

	//startup xiaot
	xiaot.init();

	//startup tds modules
	prj.loadConf();
	ds.run();  //data server
#ifdef ENABLE_FFMPEG
	//rds.run(); //remote desktop server
#endif
	ioSrv.run();
	while (1)
	{
		Sleep(1000);
	}
	return 0;
}






