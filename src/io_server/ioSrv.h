#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"
#include "ioGW_localSerial.h"

class ioServer : public ioDev
{
public:
	ioServer();
	virtual ~ioServer();
	bool loadConf();
	void saveConf();
	bool run() override;

	string getTag(string ioAddr);
};

extern ioServer ioSrv;