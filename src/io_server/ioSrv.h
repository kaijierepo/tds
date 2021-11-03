#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"
#include "ioGW_localSerial.h"
#include "ioDiscoverer.h"

class ioServer : public ioDev
{
public:
	ioServer();
	virtual ~ioServer();

	bool loadConf();
	void saveConf();

	void clear(); //清空所有ioDev对象及其相关的工作线程

	//bool loadStatus();
	//void saveStatus();
	void refreshSerialIODev();
	bool run() override;
	bool toJson(json& conf, string opt = "");
	string getTag(string ioAddr);

	//设备发现必须是某个父设备发现了子设备
	ioDev* onChildDevDiscovered(json childDevAddr, string type);

	ioDiscoverer  ioDiscoverService;
};

extern ioServer ioSrv;