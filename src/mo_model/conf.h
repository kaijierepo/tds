#pragma once
#include <string>
using namespace std;

class config
{
public:
	config();

	string path;
	bool bConcurrentGateway;
	string dbPath;
	string dataCenterIp;
};

extern config conf;

