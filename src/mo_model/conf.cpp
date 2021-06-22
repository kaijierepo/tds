#include "pch.h"
#include "conf.h"
#include  "common.h"

tdsConfig tdsConf;

tdsConfig::tdsConfig()
{
	port = 0;
	debugMode = false;
	bConcurrentGateway = true;
	projectConfPath = fs::appPath() + "\\conf";
	dbPath = fs::appPath() + "\\db";
}
