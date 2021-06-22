#pragma once
#include <string>
using namespace std;

class tdsConfig
{
public:
	tdsConfig();
	int port;
	bool debugMode;

	string projectConfPath;
	bool bConcurrentGateway;
	string dbPath;
	string dataCenterIp;
};

extern tdsConfig tdsConf;

