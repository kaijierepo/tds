#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"
#include "ioGW_localSerial.h"
#include "ioDiscoverer.h"


//并发问题
//设备上线操作ioDev列表和读取列表的并发问题,目前缺少有效的控制

class ioServer : public ioDev
{
public:
	ioServer();
	virtual ~ioServer();

	bool loadConf();
	void saveConf();

	ioDev* getIODev(string ioAddr) override;

	void clear(); //清空所有ioDev对象及其相关的工作线程

	//bool loadStatus();
	//void saveStatus();
	void refreshSerialIODev();
	bool run() override;
	void stop() override;
	bool toJson(json& conf, string opt = "");
	bool getStatus(json& conf, string opt = "");
	string getTag(string ioAddr);

	//设备发现必须是某个父设备发现了子设备
	ioDev* onChildDevDiscovered(json childDevAddr, string type);

	ioDiscoverer  ioDiscoverService;


};

extern ioServer ioSrv;