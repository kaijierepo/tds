#include "pch.h"
#include "conf.h"
#include  "common.h"

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
	fs::readFile(fs::appPath() + "\\tds.json", strConf);
	if (strConf != "")
	{
		jsonConf = json::parse(strConf);

		json j = jsonConf["project_path"];
		if (j != nullptr && projectConfPath=="")
			projectConfPath = j.get<string>();

		j = jsonConf["ui_port"];
		if (j != nullptr && port == 0)
			port = j.get<int>();

		j = jsonConf["log_level"];
		if (j != nullptr && logLevel == "")
			logLevel = j.get<string>();

		j = jsonConf["title"];
		if (j != nullptr && title == "")
			title = j.get<string>();

		j = jsonConf["homepage"];
		if (j != nullptr && homepage == "")
			homepage = j.get<string>();

		j = jsonConf["ui_mode"];
		if (j != nullptr && uiMode == "")
			uiMode = j.get<string>();
	}


	//默认值
	if (projectConfPath == "")
		projectConfPath = fs::appPath() + "\\conf";
	if (port == 0)
		port = 80;
	if (dbPath == "")
		dbPath = fs::appPath() + "\\db";
	if (title == "")
		title = "TDS";
	if (homepage == "")
		homepage = "http://localhost:666";
}
