#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"

class ioServer : public ioDev
{
public:
	ioServer();
	virtual ~ioServer();
	bool loadConf();
	void saveConf();
	bool run() override;

	string getTag(string ioPath);
};

extern ioServer ioSrv;