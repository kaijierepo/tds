#include "pch.h"
#include "tdsConf.h"
#include  "common.h"
#include "logger.h"

tdsConfig tdsConf;

tdsConfig::tdsConfig()
{
	httpsPort = 666;
	httpPort = 667;
	httpsPort2 = 0;
	httpPort2 = 0;
	tdspPort = 665;
	mbPort.push_back(664);
	iq60Port = 663;
	fileUploadPort = 671;
	debugMode = false;
	bConcurrentGateway = true;
	confPath = "";
	currentPath = "";
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
	bCreateDumpWhenLogError = false;
	iotimeoutTdsp = 7000;
	iotimeoutModbusRtu = 5000;
	iotimeoutIQ60 = 5000;
	iotimeoutDLT645 = 8000;
	mode = "tds";
	bStopCycleAcq = false;
	showObjOnline = true;
	dbLanguage = "zh";

#ifdef TDS
	tds->conf = this;
#endif
}


void tdsConfig::generateDefaultAppConfFile()
{
	string s;
	s = defaultAppConf_tds();

	if (s != "")
	{
		s = str::replace(s, "\n", "\r\n");
		string confPath = fs::appPath() + "/appConf.ini";
		fs::writeFile(confPath, s);
	}
}


void tdsConfig::generateDefaultProjectConfFile(string confPath)
{
	string s;
	s = defaultProjectConf_tds();

	if (s != "")
	{
		s = str::replace(s, "\n", "\r\n");
		string iniPath = confPath + "/projectConf.ini";
		fs::writeFile(iniPath, s);
	}
}

string tdsConfig::defaultAppConf_tds()
{
	string s = R"(#TDS 配置文件
confPath=./conf
)";
	return s;
}


string tdsConfig::defaultProjectConf_tds()
{
	string s = R"(#项目配置文件
#系统配置
uiPath=./ui            #web根目录
curPath=../current     #实时路径
dbPath=../db           #数据库路径
logPath=../log         #日志目录
loglevel=debug         #日志级别 可选 none,error,warn,debug,trace  none不记录任何日志
fmsPath=../fms         #文件管理服务，提供文件上传下载预览等

#数据服务
httpsPort=0            #https服务端口666,同时支持websocket secure
httpPort=667           #http服务端口667,同时支持websocket
httpsMediaPort=668     #https流媒体服务端口668
httpMediaPort=669      #http流媒体服务端口669
tcpPort=670            #tcp服务端口
fileUploadPort=671	   #文件上传服务端口671
mediaSrvIP=            #流媒体服务地址,留空为本机

#IO服务
ioSrvIP =                 #IO服务绑定的本地地址。在多网卡服务器上，需要指定与设备通信的那个IP地址。留空为默认0.0.0.0
tdspPort = 665            #IO服务端口 默认665  TDSP协议   
mbPort = 664              #IO服务端口 默认664  Modbus-RTU over TCP 使用串转网网关连接Modbus总线。支持逗号分隔配置多个端口( mbPort=664,20022)，用于udp设备回包不是回给请求的udp端口，而是回给固定的udp端口的场景。
iq60Port = 663            #IO服务端口 默认663  IQ60物云协议  
mbTcpPort = 502           #IO服务端口 默认502  modbusTcp协议 
leakDetectPort = 8085     #IO服务端口 默认8085 漏点监测设备
iotimeoutTdsp=7000
iotimeoutModbusRtu=5000
iotimeoutIQ60=5000
iotimeoutDLT645=8000
tdspOnlineReq=0           #tdsp设备上线请求getDevInfo
tdspSingleTransaction=0   #tdsp设备请求不允许并发

#安全性
enableAccessCtrl = 0      #开启用户认证
tokenExpireTime = 60      #token失效时间，单位分钟
testToken=                #测试用Token

#服务级联 (主服务端口在主服务的tdspPort配置，默认665)
masterTds=                #主服务地址，多个主服务使用逗号分隔 例如：cloud1.liangtusoft.com:665,cloud2.liangtusoft.com:665

#文件服务
fsRoot=                   #文件服务的根目录。留空不启动文件服务

#短信服务(飞鸽)
smsApiUrl =			      #短信平台api地址
smsApiUser =              #短信平台api账号
smsApiKey =               #短信平台api秘钥

#阿里云
aliKeyID=              #阿里云AccessKeyID
aliKeySecret=          #阿里云AccessKeySecret
ddnsInterval=600       #ddns更新周期
ddnsDomainName=        #ddns更新域名。多个域名使用逗号分隔

#功能模块启用
enableHMR = 0          #启用http服务器热更新功能
authDownload = 0       #开启文件下载用户认证
)";
	return s;
}

string tdsConfig::defaultConf_tdb()
{
	string s = R"(#TDB 数据库配置
dbPath=./db            #数据库数据存储路径
uiPath=./ui            #管理后台web根目录
httpsPort=666          #https服务端口,同时支持websocket secure
httpPort=667           #http服务端口,同时支持websocket
loglevel=debug         #日志级别
)";
	return s;
}

string tdsConfig::defaultConf_rphttp()
{
	string s = R"(#HTTP 反向代理服务器配置
httpPort=80            #http服务代理端口
)";
	return s;
}

void tdsConfig::loadConf_httpServer(vector<KV_CONF_ITEM>& vecConf) {
	for (int i = 0; i < vecConf.size(); i++)
	{
		KV_CONF_ITEM& tci = vecConf[i];
		if (checkKey(tci.key, "httpPort"))
		{
			httpPort = atoi(tci.val.c_str());
		}
	}
}




void tdsConfig::loadConf_tds(map<string, string>& vecConf) {
	for (auto& iter: vecConf)
	{
		KV_CONF_ITEM tci;
		tci.key = iter.first;
		tci.val = iter.second;
		if (checkKey(tci.key, "confPath"))
		{
			confPath = tci.val;
			confPath = fs::toAbsolutePath(confPath);
		}
		else if (checkKey(tci.key, "curPath"))
		{
			currentPath = tci.val;
			currentPath = fs::toAbsolutePath(currentPath);
		}
		else if (checkKey(tci.key, "uiPath"))
		{
			uiPath = tci.val;
			uiPath = fs::toAbsolutePath(uiPath); 
		}
		else if (checkKey(tci.key, "dbPath")) {
			dbPath = tci.val.c_str();
			dbPath = fs::toAbsolutePath(dbPath);
		}
		else if (checkKey(tci.key, "logPath")) {
			logPath = tci.val.c_str();
			logPath = fs::toAbsolutePath(logPath);
		}
		else if (checkKey(tci.key, "fmsPath")) {
			fmsPath = tci.val.c_str();
			fmsPath = fs::toAbsolutePath(fmsPath);
		}
		else if (checkKey(tci.key, "dbLanguage")) {
			dbLanguage = tci.val.c_str();
		}
		else if (checkKey(tci.key, "mediaSrvIP"))
			mediaSrvIP = tci.val.c_str();
		else if (checkKey(tci.key, "testToken"))
			testToken = tci.val.c_str();
		else if (checkKey(tci.key, "tokenExpireTime"))
			tokenExpireTime = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "iotimeoutTdsp"))
			iotimeoutTdsp = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "iotimeoutModbusRtu"))
			iotimeoutModbusRtu = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "iotimeoutIQ60"))
			iotimeoutIQ60 = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "httpsPort"))
			httpsPort = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "httpPort"))
			httpPort = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "httpsPort2"))
			httpsPort2 = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "httpPort2"))
			httpPort2 = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "tdspPort"))
			tdspPort = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "mbPort")) {
			mbPort.clear();
			string s = str::replace(tci.val,"，", ",");
			vector<string> sPorts;
			str::split(sPorts,s, ",");
			for (int i = 0; i < sPorts.size(); i++) {
				string sp = sPorts[i];
				int p = atoi(sp.c_str());
				mbPort.push_back(p);
			}
		}
		else if (checkKey(tci.key, "iq60Port"))
			iq60Port = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "devRebootTime"))
			devRebootTime = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "devCommRebootTime"))
			devCommRebootTime = atoi(tci.val.c_str());
		else if (checkKey(tci.key, "ui") && uiMode == "")
			uiMode = tci.val;
		else if ((checkKey(tci.key, "logLevel")) && logLevel == "")
			logLevel = tci.val;
		else if (checkKey(tci.key, "title") && title == "")
			title = tci.val;
		else if (checkKey(tci.key, "homePage") && homepage == "")
			homepage = tci.val;
		else if (checkKey(tci.key, "singleGenicamHost"))
			singleGenicamHost = tci.val == "1" ? true : false;
		else if (checkKey(tci.key, "authDownload"))
		{
			if (tci.val == "true" || tci.val == "1")
				authDownload = true;
			else if (tci.val == "false" || tci.val == "0")
				authDownload = false;
		}
		else if (checkKey(tci.key, "stopCycleAcq"))
		{
			if (tci.val == "true" || tci.val == "1")
				bStopCycleAcq = true;
			else if (tci.val == "false" || tci.val == "0")
				bStopCycleAcq = false;
		}
		else if (checkKey(tci.key, "debugMode"))
		{
			if (tci.val == "true" || tci.val == "1")
				debugMode = true;
			else if (tci.val == "false" || tci.val == "0")
				debugMode = false;
		}
		else if (checkKey(tci.key, "createDumpWhenLogError"))
		{
			if (tci.val == "true" || tci.val == "1")
				bCreateDumpWhenLogError = true;
			else if (tci.val == "false" || tci.val == "0")
				bCreateDumpWhenLogError = false;
		}
		else if (checkKey(tci.key, "enableLog"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableLog = true;
			else if (tci.val == "false" || tci.val == "0")
				enableLog = false;
		}
		else if (checkKey(tci.key, "enableDevReboot"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableDevReboot = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDevReboot = false;
		}
		else if (checkKey(tci.key, "enableDevCommReboot"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableDevCommReboot = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDevCommReboot = false;
		}
		else if (checkKey(tci.key, "enableDB"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableDB = true;
			else if (tci.val == "false" || tci.val == "0")
				enableDB = false;
		}
		else if (checkKey(tci.key, "enableAccessCtrl"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableAccessCtrl = true;
			else if (tci.val == "false" || tci.val == "0")
				enableAccessCtrl = false;
		}
		else if (checkKey(tci.key, "enableScript"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableScript = true;
			else if (tci.val == "false" || tci.val == "0")
				enableScript = false;
		}
		else if (checkKey(tci.key, "enableGlobalAlarm"))
		{
			if (tci.val == "true" || tci.val == "1")
				enableGlobalAlarm = true;
			else if (tci.val == "false" || tci.val == "0")
				enableGlobalAlarm = false;
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
		else if (checkKey(tci.key, "fullScreen"))
		{
			if (tci.val == "true" || tci.val == "1")
				fullscreen = true;
			else if (tci.val == "false" || tci.val == "0")
				fullscreen = false;
		}
		else if (checkKey(tci.key, "smsApiUrl"))
		{
			smsApiUrl = tci.val;
		}
		else if (checkKey(tci.key, "smsApiUser"))
		{
			smsApiUser = tci.val;
		}
		else if (checkKey(tci.key, "smsApiKey"))
		{
			smsApiKey = tci.val;
		}
		else if (checkKey(tci.key, "showObjOnline"))
		{
			if (tci.val == "true" || tci.val == "1")
				showObjOnline = true;
			else if (tci.val == "false" || tci.val == "0")
				showObjOnline = false;
		}
		else if (checkKey(tci.key, "logEnable_innerRPCCall")) {
			if (tci.val == "true" || tci.val == "1")
				logEnable.innerRPCCall = true;
			else if (tci.val == "false" || tci.val == "0")
				logEnable.innerRPCCall = false;
		}
		else if (checkKey(tci.key, "logEnable_scriptEngine")) {
			if (tci.val == "true" || tci.val == "1")
				logEnable.scriptEngine = true;
			else if (tci.val == "false" || tci.val == "0")
				logEnable.scriptEngine = false;
		}
		else if (checkKey(tci.key, "fileUploadPort"))
		{
			fileUploadPort=atoi(tci.val.c_str());
		}
		//磁盘清理相关
		else if (checkKey(tci.key, "triggerStratgy"))
		{
			triggerStratgy = tci.val;
		}
		else if (checkKey(tci.key, "diskSpaceLeft"))
		{
			diskSpaceLeft = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "judgePeriod"))
		{
			judgePeriod = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "dataStorageMonths"))
		{
			dataStorageMonths = atoi(tci.val.c_str());
		}
		else if (checkKey(tci.key, "mediaStorageMonths"))
		{
			mediaStorageMonths = atoi(tci.val.c_str());
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
	if (confPath == "")
		confPath = fs::toAbsolutePath("../conf");
	if (currentPath == "")
		currentPath = fs::toAbsolutePath("../current");
	if(uiPath == "")
		uiPath = fs::toAbsolutePath("./ui");
	if (dbPath == "")
		dbPath = fs::toAbsolutePath("../db");
	if (logPath == "")
		logPath = fs::toAbsolutePath("../log");
	if (fmsPath == "")
		fmsPath = fs::toAbsolutePath("../fms");

	if (title == "")
		title = "TDS";
	if (homepage == "")
	{
		homepage = "http://localhost:" + str::fromInt(httpPort);
	}
	if (uiTitle == "")
		uiTitle = "tdsUI";
}

void tdsConfig::loadConf_rphttp(vector<KV_CONF_ITEM>& vecConf)
{
	for (int i = 0; i < vecConf.size(); i++)
	{
		KV_CONF_ITEM& tci = vecConf[i];
		if (checkKey(tci.key, "httpPort"))
		{
			httpPort = atoi(tci.val.c_str());
		}
	}
}

void tdsConfig::loadCurrentData()
{
	string confPath;
	string confFileName;
	confFileName = mode;
	confPath = currentPath + "/cur.ini";
	fs::createFolderOfPath(confPath);

	curIni.load(confPath);
}


void tdsConfig::loadConf()
{
	//load appConf.ini
	string appIni = fs::appPath() + "/" + fs::appName() + ".ini";
	if (fs::fileExist(appIni))
	{
		project_ini.load(appIni);
		loadConf_tds(project_ini.mapConf);
	}
	else
	{
		string appConfPath = fs::appPath() + "/appConf.ini";
		if (!fs::fileExist(appConfPath))
		{
			string s = str::format("[warn]配置文件%s不存在，创建默认配置", appConfPath.c_str());
			logger.logInternal(s, false);
			generateDefaultAppConfFile();
		}
		app_ini.load(appConfPath);
		loadConf_tds(app_ini.mapConf);

		//load projectConf.ini
		string projectConfPath = tds->conf->confPath + "/projectConf.ini";
		if (!fs::fileExist(projectConfPath))
		{
			string s = str::format("[warn]配置文件%s不存在，创建默认配置", projectConfPath.c_str());
			logger.logInternal(s, false);
			generateDefaultProjectConfFile(tds->conf->confPath);
		}
		project_ini.load(projectConfPath);
		loadConf_tds(project_ini.mapConf);
	}
}

json tdsConfig::toJson()
{
	json conf;
	conf["httpsPort"] = httpsPort;
	conf["httpPort"] = httpPort;
	conf["ioServerPort"] = tdspPort;
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
	if (toCheck == key)
		return true;
	return false;
}

int tdsConfig::getInt(string key, int iDef)
{
	return project_ini.getValInt(key, iDef);
}

string tdsConfig::getStr(string key, string sDef)
{
	return project_ini.getValStr(key, sDef);
}

bool tdsConfig::setStr(string key, string val)
{
	project_ini.setVal(key, val);
	return true;
}

bool tdsConfig::setInt(string key, int val)
{
	project_ini.setVal(key, val);
	return true;
}


int tdsConfig::getCurrentInt(string key, int iDef)
{
	return curIni.getValInt(key, iDef);
}

string tdsConfig::getCurrentStr(string key, string sDef)
{
	return curIni.getValStr(key, sDef);
}

bool tdsConfig::setCurrentStr(string key, string val)
{
	curIni.setVal(key, val);
	return true;
}

bool tdsConfig::setCurrentInt(string key, int val)
{
	curIni.setVal(key, val);
	return true;
}