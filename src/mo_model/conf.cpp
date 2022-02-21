#include "pch.h"
#include "conf.h"
#include  "common.hpp"
#include "logger.h"

tdsConfig::tdsConfig()
{
	port = 0;
	httpPort = 0;
	ioServerPort = 0;
	debugMode = false;
	bConcurrentGateway = true;
	projectConfPath = "";
	dbPath = "";
	singleGenicamHost = false;
	enableDB = true;
	enableLog = true;
	enableDevReboot = false;
	enableDevCommReboot = false;
	devRebootTime = 15 * 60;
	devCommRebootTime = 5 * 60;
	enableGlobalAlarm = true;
	tcpKeepAliveIO = 60 * 60;
	tcpKeepAliveDS = 30;
}


void tdsConfig::generateDefaultConfFile(string m)
{
	string s;
	if (m == "tds")
	{
		s = defaultConf_tds();
	}
	else if (m == "tcp2com")
	{
		s = defaultConf_tcp2com();
	}
	else if (m == "hs" || m == "httpServer")
	{
		s = defaultConf_httpServer();
	}

	s = str::replace(s, "\n", "\r\n");
	string confPath = fs::appPath() + "/" + mode + ".ini";
	fs::writeFile(confPath, s);
}

string tdsConfig::defaultConf_tcp2com()
{
	string s = R"(#TDS 配置文件
#串口转tcp透传网关配置
com=COM1
baudRate=19200
byteSize=8
stopBits=1
parity=None            #None Even Odd
mode=tcpclient         #tcpclient  tcpServer
remoteIP=127.0.0.1     #client模式下的服务器IP
remotePort=664         #client模式下的服务器端口
localPort=663          #server模式下的服务端口
registerPktStr={"method":"devRegister","ioAddr":"RS485Gateway_0001"}
)";
	return s;
}

string tdsConfig::defaultConf_httpServer() 
{
	string s = R"(#HTTP服务器配置
httpPort=80            #http服务端口
)";
	return s;
}


string tdsConfig::defaultConf_tds()
{
	string s = R"(#TDS 配置文件
#基础配置
confpath=./conf        #配置路径
port=666               #数据服务端口websocket协议
httpPort=667           #http服务端口
ioServerPort=665       #io通信服务端口
loglevel=debug         #日志级别

#功能模块启用
enableDB = 1           #启用数据库
enableLog = 1          #启用日志记录
authDownload = 0       #开启文件下载用户认证
enableAccessCtrl = 0   #开启用户认证
enableScript = 0       #启用脚本功能

#IO服务功能
enableDevReboot=1      #启用设备重启功能      
devRebootTime=180      #设备无通信重启时间

#桌面软件模式
ui=console             #ui模式  console:命令行模式   chrome:浏览器模式
)";
	return s;
}

void tdsConfig::loadConf_httpServer(vector<TDS_CONF_ITEM>& vecConf) {
	for (int i = 0; i < vecConf.size(); i++)
	{
		TDS_CONF_ITEM& tci = vecConf[i];
		if (checkKey(tci.key, "httpPort"))
		{
			httpPort = atoi(tci.val.c_str());
		}
	}
}


void tdsConfig::loadConf_tcp2com(vector<TDS_CONF_ITEM>& vecConf) {
	for (int i = 0; i < vecConf.size(); i++)
	{
		TDS_CONF_ITEM& tci = vecConf[i];
		if(checkKey(tci.key,"mode"))
		{
			conf_tcp2com.mode = tci.val;
		}
		else if (checkKey(tci.key, "com"))
		{
			conf_tcp2com.com = tci.val;
		}
		else if (checkKey(tci.key, "baudRate"))
		{
			conf_tcp2com.baudRate = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "byteSize"))
		{
			conf_tcp2com.byteSize = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "stopBits"))
		{
			conf_tcp2com.stopBits = tci.val;
		}
		else if (checkKey(tci.key, "parity"))
		{
			conf_tcp2com.parity = tci.val;
		}
		else if (checkKey(tci.key, "remoteIP"))
		{
			conf_tcp2com.remoteIP = tci.val;
		}
		else if (checkKey(tci.key, "remotePort"))
		{
			conf_tcp2com.remotePort = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "localPort"))
		{
			conf_tcp2com.localPort = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "registerPktStr"))
		{
			conf_tcp2com.registerPktStr = tci.val;
		}
	}
}

void tdsConfig::loadConf_tds(vector<TDS_CONF_ITEM>& vecConf) {
	for (int i = 0; i < vecConf.size(); i++)
	{
		TDS_CONF_ITEM& tci = vecConf[i];
		if (tci.key == "confpath" && projectConfPath == "")
		{
			projectConfPath = tci.val;
			projectConfPath = fs::toAbsolutePath(projectConfPath);
		}
		else if (checkKey(tci.key, "tcpkeepaliveio"))
			tcpKeepAliveIO = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "tcpkeepaliveds"))
			tcpKeepAliveDS = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "port") && port == 0)
			port = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "httpport") && httpPort == 0)
			httpPort = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "ioserverport") && ioServerPort == 0)
			ioServerPort = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "devreboottime"))
			devRebootTime = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "devcommreboottime"))
			devCommRebootTime = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "ui") && uiMode == "")
			uiMode = tci.val;
		else if ((checkKey(tci.key, "loglevel")) && logLevel == "")
			logLevel = tci.val;
		else if (checkKey(tci.key, "title") && title == "")
			title = tci.val;
		else if (checkKey(tci.key, "homepage") && homepage == "")
			homepage = tci.val;
		else if (checkKey(tci.key, "singlegenicamhost"))
			singleGenicamHost = tci.val == "1" ? true : false;
		else if (checkKey(tci.key, "authdownload"))
		{
			if (tci.val == "true" || tci.val == "1")
				authDownload = true;
			else if (tci.val == "false" || tci.val == "0")
				authDownload = false;
		}
		else if (checkKey(tci.key, "enablelog"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableLog = true;
			else if (tci.val == "false" || tci.val == "0")
				enableLog = false;
		}
		else if (checkKey(tci.key, "enabledevreboot"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableDevReboot = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDevReboot = false;
		}
		else if (checkKey(tci.key, "enabledevcommreboot"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableDevCommReboot = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDevCommReboot = false;
		}
		else if (checkKey(tci.key, "enabledb"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableDB = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDB = false;
		}
		else if (checkKey(tci.key, "enableaccessctrl"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableAccessCtrl = true;
			else if (tci.val == "false" || tci.val == "0")
				enableAccessCtrl = false;
		}
		else if (checkKey(tci.key, "enablescript"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableScript = true;
			else if (tci.val == "false" || tci.val == "0")
				enableScript = false;
		}
		else if (checkKey(tci.key, "enableglobalalarm"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableGlobalAlarm = true;
			else if (tci.val == "false" || tci.val == "0")
				enableGlobalAlarm = false;
		}
		else if (checkKey(tci.key, "edge"))
		{
			if (tci.val == "true" || tci.val == "1")
				edge = true;
			else if (tci.val == "false" || tci.val == "0")
				edge = false;
		}
		else if (checkKey(tci.key, "cloudIP"))
		{
			cloudIP = tci.val;
		}
		else if (checkKey(tci.key, "deviceID"))
		{
			deviceID = tci.val;
		}
		else if (checkKey(tci.key, "cloudPort"))
		{
			cloudPort = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "fullscreen"))
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
	if (httpPort == 0)
		httpPort = 667;
	if (ioServerPort == 0)
		ioServerPort = 665;
	if (dbPath == "")
		dbPath = fs::appPath() + "\\db";
	if (title == "")
		title = "TDS";
	if (homepage == "")
	{
		homepage = "http://localhost:" + str::fromInt(httpPort);
	}
	if (uiTitle == "")
		uiTitle = "tdsUI";

	//关键配置信息
	if (enableDevReboot)
		LOG("[TDS参数   ]启用设备自动重启机制,重启周期" + str::fromInt(devRebootTime) + "秒");
	if (enableDevCommReboot)
		LOG("[TDS参数   ]启用设备通信模块自动重启机制,重启周期" + str::fromInt(devCommRebootTime) + "秒");
}

void tdsConfig::loadConf()
{
	string confPath = fs::appPath() + "/" + mode + ".ini";
	if (!fs::fileExist(confPath))
	{
		generateDefaultConfFile(mode);
	}

	//配置文件当中的值  如果有值，说明是命令行设置，命令行优先级最高
	string strConf;
	fs::readFile(confPath, strConf);
	vector<string> confItems;
	str::split(confItems, strConf, "\n");

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

			tci.key = str::trim(tci.key, " ");
			tci.val = str::trim(tci.val, " ");
			tci.val = str::trim(tci.val, "\r");
			vecConf.push_back(tci);
		}
	}

	if (mode == "tds")
	{
		loadConf_tds(vecConf);
	}
	else if (mode == "tcp2com")
	{
		loadConf_tcp2com(vecConf);
	}
	else if (mode == "hs" || mode == "httpServer")
	{
		loadConf_httpServer(vecConf);
	}
}

json tdsConfig::toJson()
{
	json conf;
	conf["port"] = port;
	conf["httpPort"] = httpPort;
	conf["ioServerPort"] = ioServerPort;
	conf["debugMode"] = debugMode;
	conf["enableDB"] = enableDB;
	conf["enableLog"] = enableLog;
	conf["enableDevReboot"] = enableDevReboot;
	conf["enableDevCommReboot"] = enableDevCommReboot;
	conf["devRebootTime"] = devRebootTime;
	conf["devCommRebootTime"] = devCommRebootTime;
	conf["enableGlobalAlarm"] = enableGlobalAlarm;
	conf["tcpKeepAliveIO"] = tcpKeepAliveIO;
	conf["tcpKeepAliveDS"] = tcpKeepAliveDS;

	return conf;
}

bool tdsConfig::checkKey(string toCheck, string key)
{
	toCheck = normalizationKey(toCheck);
	key = normalizationKey(key);
	if (toCheck == key)
		return true;
	return false;
}

string tdsConfig::normalizationKey(string key)
{
	str::removeChar(key, '_');
	str::removeChar(key, '-');
	key = _strlwr((char*)key.c_str());
	return key;
}
