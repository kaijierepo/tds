#include "pch.h"
#include "conf.h"
#include  "common.h"

config conf;

config::config()
{
	bConcurrentGateway = true;
	path = fs::appPath() + "\\conf";
	dbPath = fs::appPath() + "\\db";
}
