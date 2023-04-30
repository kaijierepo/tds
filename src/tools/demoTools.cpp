#include "pch.h"
#include "demoTools.h"
#include "logger.h"
#include "db.h"


void gen_testMoConf() {
	LOG("正在生成3万点测试配置,10个车间，每车间100台设备，每设备30数据点，配置文件 mo.json ......");
	string sMp = R"(
{
    "b": 0.0,
    "ioType": "i",
    "k": 0.1,
    "name": "",
    "saveMode": "always",
    "type": "mp",
    "unit": "°C",
    "valType": "float"
}
)";

	string sWorkshop = R"(
{
    "name": "",
    "type": "org"
}
)";

	string sDev = R"(
{
    "name": "",
    "type": "mo"
}
)";
	json jMp = json::parse(sMp);
	json jWorkshop = json::parse(sWorkshop);
	json jDev = json::parse(sDev);
	json jConf;
	json jConf_children = json::array();
	jConf["name"] = "3万点测试";

	for (int iWs = 0; iWs < 10; iWs++) {
		jWorkshop["name"] = "车间" + str::fromInt(iWs);
		json jWorkshop_children = json::array();
		for (int iDev = 0; iDev < 100; iDev++) {
			jDev["name"] = "设备" + str::fromInt(iDev);
			json jDev_children = json::array();
			for (int iMp = 0; iMp < 30; iMp++) {
				jMp["name"] = "监控点" + str::fromInt(iMp);
				jDev_children.push_back(jMp);
			}
			jDev["children"] = jDev_children;
			jWorkshop_children.push_back(jDev);
		}
		jWorkshop["children"] = jWorkshop_children;
		jConf_children.push_back(jWorkshop);
	}
	jConf["children"] = jConf_children;

	string sConf = jConf.dump();
	fs::writeFile(fs::appPath() + "/testConf/mo.json", sConf);
	LOG("测试配置mo.json生成成功");
}



void gen_testIoConf() {
	string sChan = R"(
{
	"addr": {
		"regOffset": 0,
		"regType": "Holding-Register"
	},
	"fmt": "uint16",
	"ioType": "io",
	"k": 0.1,
	"name": "",
	"nodeID": "daf4a8c5-59fd-4f16-a23f-a79786089383",
	"tagBind": "",
	"valType": "float"
}
)";

	string sDev = R"(
{
    "acqAlarm": false,
    "acqInterval": 10.0,
    "acqMode": "all",
    "addr": {
        "ip": "127.0.0.1",
        "port": 50001
    },
    "addrMode": "tcpClient",
    "channels": [],
    "enableAcq": true,
    "level": "device",
    "manageStatus": "managed",
    "nodeID": "3EC92551-4F86-41db-944A-415528BB0964",
    "type": "modbus-tcp-slave"
}
)";


	json jConf = json::array();
	json jDev = json::parse(sDev);
	json jChan = json::parse(sChan);

	for (int i = 0; i < 1000; i++) {
		jDev["addr"]["port"] = 50000 + i;
		json jDev_children = json::array();
		int iWsNo = i / 100;
		string sWsNo = "车间" + str::fromInt(iWsNo);
		int iDevNo = i % 100;
		string sDevNo = "设备" + str::fromInt(iDevNo);
		for (int j = 0; j < 30; j++) {
			string sMpNo = "监控点" + str::fromInt(j);
			jChan["addr"]["regOffset"] = j;
			jChan["nodeID"] = common::guid();
			jDev_children.push_back(jChan);
			jChan["tagBind"] = sMpNo;
		}
		jDev["tagBind"] = sWsNo + "." + sDevNo;
		jDev["nodeID"] = common::guid();
		jDev["channels"] = jDev_children;
		jConf.push_back(jDev);
	}

	string sConf = jConf.dump();
	fs::writeFile(fs::appPath() + "/testConf/io.json", sConf);
	LOG("测试配置io.json生成成功");
}


void gen_testdb() {
	int simuRecCount = 0;
	if (simuRecCount > 0)
	{
		tds->conf->mode = "cmd";
		LOG("正在向数据库写入" + to_string(simuRecCount) + "条数据...");
		TIME stT;
		timeopt::now(&stT);
		json j;
		j["time"] = timeopt::st2str(stT);
		j["temp"] = 23.5;
		j["humidity"] = 65.1;
		j["pm25"] = 45.5;
		j["co2"] = 345.1;

		json jA;
		TIME ststart;
		timeopt::now(&ststart);
		for (int i = 0; i < simuRecCount; i++)
		{
			db.Insert("devicedata", stT, j);
		}

		long long tt = timeopt::CalcTimePassMilliSecond(ststart);
		LOG("写入" + to_string(simuRecCount) + "条仿真数据, 位号:devicedata, 耗时" + str::fromInt(tt) + "ms");
	}
}