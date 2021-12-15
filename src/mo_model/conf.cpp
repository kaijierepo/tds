#include "pch.h"
#include "conf.h"
#include  "common.hpp"

tdsConfig::tdsConfig()
{
	port = 0;
	httpPort = 0;
	debugMode = false;
	bConcurrentGateway = true;
	projectConfPath = "";
	dbPath = "";
	singleGenicamHost = false;
	enableDB = true;
	enableLog = true;
}

struct TDS_CONF_ITEM {
	string key;
	string val;
};

void tdsConfig::loadConf()
{
	//配置文件当中的值  如果有值，说明是命令行设置，命令行优先级最高
	string strConf;
	fs::readFile(fs::appPath() + "\\tds.ini", strConf);
	vector<string> confItems;
	str::split(confItems, strConf, "\r\n");

	//去掉注释
	for (int i = 0; i < confItems.size(); i++)
	{
		string& ci = confItems[i];
		int pos = ci.find("#");
		if (pos != string::npos)
		{
			ci = ci.substr(0, pos);
		}
	}
	//解析
	vector<TDS_CONF_ITEM> vecConf;
	for (int i = 0; i < confItems.size(); i++)
	{
		string& ci = confItems[i];
		TDS_CONF_ITEM tci;
		int pos = ci.find("=");
		if (pos != string::npos)
		{
			tci.key = ci.substr(0, pos);
			tci.val = ci.substr(pos + 1, ci.length() - pos - 1);

			//支持camelCase,下划线命名等多种命名方法
			tci.key = str::trim(tci.key, " ");
			str::removeChar(tci.key, '_');
			str::removeChar(tci.key, '-');
			tci.key = _strlwr((char*)tci.key.c_str());
			tci.val = str::trim(tci.val, " ");
			vecConf.push_back(tci);
		}
	}

	for (int i = 0; i < vecConf.size(); i++)
	{
		TDS_CONF_ITEM& tci = vecConf[i];
		if (tci.key == "confpath" && projectConfPath == "")
			projectConfPath = tci.val;
		else if (tci.key == "port" && port == 0)
			port = atoi(tci.val.c_str());
		else if (tci.key == "httpport" && httpPort == 0)
			httpPort = atoi(tci.val.c_str());
		else if (tci.key == "ui" && uiMode == "")
			uiMode = tci.val;
		else if ((tci.key == "loglevel") && logLevel == "")
			logLevel = tci.val;
		else if (tci.key == "title" && title == "")
			title = tci.val;
		else if (tci.key == "homepage" && homepage == "")
			homepage = tci.val;
		else if (tci.key == "singlegenicamhost")
			singleGenicamHost = tci.val == "1" ? true : false;
		else if (tci.key == "enablelog")
		{
			if (tci.val == "true" || tci.val == "1")
				enableLog = true;
			else if(tci.val == "false" || tci.val == "0")
				enableLog = false;
		}
		else if (tci.key == "enabledb")
		{
			if (tci.val == "true" || tci.val == "1")
				enableDB = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDB = false;
		}
		else if (tci.key == "enableaccessctrl")
		{
			if (tci.val == "true" || tci.val == "1")
				enableAccessCtrl = true;
			else if (tci.val == "false" || tci.val == "0")
				enableAccessCtrl = false;
		}
		else if (tci.key == "enablescript")
		{
			if (tci.val == "true" || tci.val == "1")
				enableScript = true;
			else if (tci.val == "false" || tci.val == "0")
				enableScript = false;
		}
		else if (tci.key == "uiTitle")
		{
			uiTitle = tci.val;
		}
		else if (tci.key == "fullscreen")
		{
			if (tci.val == "true" || tci.val == "1")
				fullscreen = true;
			else if (tci.val == "false" || tci.val == "0")
				fullscreen = false;
		}
	}

		//j = jsonConf["active_session"];
		//if (j != nullptr)
		//{
		//	for (int i = 0; i < j.size(); i++)
		//	{
		//		json jas = j[i];
		//		ACTIVE_TDS_SESSION ats;
		//		ats.ip = jas["ip"].get<string>();
		//		ats.port = jas["port"].get<int>();
		//		//ats.type = jas["type"].get<string>();
		//		vecActiveSession.push_back(ats);
		//	}
		//}


	//默认值
	if (projectConfPath == "")
		projectConfPath = fs::appPath() + "/conf";
	if (port == 0)
		port = 666;
	if (dbPath == "")
		dbPath = fs::appPath() + "\\db";
	if (title == "")
		title = "TDS";
	if (homepage == "")
		homepage = "http://localhost:666";
	if (uiTitle == "")
		uiTitle = "tdsUI";
}
