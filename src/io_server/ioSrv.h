#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"
#include "ioGW_localSerial.h"
#include "ioSrv_serialDetection.h"

class ioServer : public ioDev
{
public:
	ioServer();
	virtual ~ioServer();

	bool loadConf();
	void saveConf();
	void refreshSerialIODev();
	bool run() override;
	bool toJson(json& conf, string opt = "");
	string getTag(string ioAddr);

	ioSrv_serialDetection  serialDetectionService;
};

extern ioServer ioSrv;