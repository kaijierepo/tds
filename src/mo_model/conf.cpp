#include "pch.h"
#include "conf.h"
#include  "common.h"

tdsConfig tdsConf;

tdsConfig::tdsConfig()
{
	port = 0;
	debugMode = false;
	bConcurrentGateway = true;
	projectConfPath = "";
	dbPath = "";
}

void tdsConfig::loadConf()
{
	//配置文件当中的值  如果有值，说明是命令行设置，命令行优先级最高
	string strConf;
	fs::readFile(fs::appPath(), strConf);
	if (strConf != "")
	{
		jsonConf = json::parse(strConf);

		json j = jsonConf["project_path"];
		if (j != nullptr && projectConfPath=="")
			projectConfPath = j.get<string>();

		j = jsonConf["ui_port"];
		if (j != nullptr && port == 0)
			port = j.get<int>();
	}


	//默认值
	if (projectConfPath == "")
		projectConfPath = fs::appPath() + "\\conf";
	if (port == 0)
		port = 80;
	if (dbPath == "")
		dbPath = fs::appPath() + "\\db";
}
