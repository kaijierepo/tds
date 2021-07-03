#pragma once
#include <string>
using namespace std;
#include "json.hpp"

class tdsConfig
{
public:
	tdsConfig();
	int port;
	bool debugMode;

	void loadConf();

	string projectConfPath;
	bool bConcurrentGateway;
	string dbPath;
	string dataCenterIp;
	json jsonConf;
};

extern tdsConfig tdsConf;

