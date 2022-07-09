/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu 卢涛

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "pch.h"
#include "conf.h"
#include "cmdparser.hpp"
#include "video/remoteDesktopServer.h"
#include "cmdparser.hpp"
#include "logger.h"
#include "tds_imp.h"
#include "wke.h"
#include "tools/tcpHub.h"
#include "tools/tcpSwitch.h"
#include "tools/tcpReverseProxy.h"
#include "tools/tdsWatchDog.h"
#include "tools/rproxy.h"
#include "httplib.h"
#include "db.h"
#include "tools/tools.hpp"

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

代码不安全，未来需优化的地方，全局搜索 [unsafe]
需要改进的问题  全局搜索[问题]
*/

/*
TDS是一个数据服务
数据由数据的生产者ioDev(IO设备)提供给TDS,tdsClt(tds客户端)作为数据的使用者
ioDev虽然一般以tcpClient的方式连接到tds. 但相对于tds来说,设备被看作是服务端

*/


//exe模式下，都会有命令行窗口，通过设置 ui = chrome 或者 miniblink打开 浏览器窗口
//dll模式下，默认没有命名行窗口，通过设置 ui = console 来打开命令行窗口

#include "../tdspro/ioDev/ioDev_modbusSlave.h""

#ifndef _WINDLL
//命令行参数调用 ，js解释器等模式需要默认控制台，因此默认控制台不隐藏
//terminal模式等需要隐藏，使用其他方式隐藏
//#pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )//不显示默认控制台
int main(int argc, char** argv)
{
	MB_RTU_PKT a, b;
	b.crc_H = 0xAA;
	a = b;
	vector<MB_RTU_PKT> vec;
	vec.push_back(a);

	setThreadName("main thread");
	tds->conf = &tdsImp.tdsConf;
	
	string cmd;

	//use cmd line conf first ,or use tds.json 
	cli::Parser parser(argc, argv);
	parser.set_optional<string>("m", "mode", "",charCodec::utf8toAnsi(
"hub: tcp集线器模式，左侧数据将发往右侧所有连接;右侧数据将发往左侧所有连接\r\n\
      示例:  tds -m hub -sl 666 -sr 667\r\n\
   switch: tcp交换机模式\r\n\
   rproxy: 反向代理模式\r\n\
   tcp2com: tcp转串口模式;\r\n\
   js: javascript解释器模式;\r\n\
      示例:  tds -m tcp2com -com COM1 -tcpc 127.0.0.1:666"));

	parser.set_optional<int>("sl", "serverleft", 666, "");
	parser.set_optional<int>("sr", "serverright", 667, "");
	parser.set_optional<int>("gsd", "gensimudb", 0, _GB("生成db仿真数据; 输入生成条数"));
	parser.set_optional<string>("tc", "tdsconf", "", "tds config file");
	parser.set_optional<string>("com", "com", "COM1", "com port number in tcp2com mode");
	parser.set_optional<string>("tcpc", "tcpc", "", "tcp client in format XXX.XXX.XXX.XXX:XXXX");
	parser.set_optional<string>("tcps", "tcps", "", "tcp server in format XXXX");
	parser.set_optional<string>("sb", "serverbackend", "127.0.0.1:666", "backend server in reverse proxy mode");
	parser.set_optional<int>("pp", "proxyport", 667, "proxy server port in reverse proxy mode");
	parser.set_optional<int>("p", "port", 0, "Integers in all forms, e.g., unsigned int, long long, ..., are possible. Hexadecimal and Ocatl numbers parsed as well");
	parser.set_optional<bool>("d", "debug", false, "run in debug mode. heartbeat will be closed;more log will be added;");
	parser.set_optional<int>("baudRate", "baudRate", 19200, _GB("串口波特率"));
	parser.set_optional<int>("byteSize", "byteSize", 8, _GB("串口数据位"));
	parser.set_optional<string>("stopBits", "stopBits", "1", _GB("停止位"));
	parser.set_optional<string>("parity", "parity", "None", _GB("校验位"));
	parser.set_optional<string>("oldstr", "oldstr", "None", _GB("被替换的字符串"));
	parser.set_optional<string>("newstr", "newstr", "None", _GB("新字符串"));

	//保持无效值，使用配置文件当中的值
	parser.set_optional<string>("l", "loglevel", "", "value can be detail,trace,debug,warn,error");
	parser.run_and_exit_if_error();
	tds->conf->debugMode = parser.get<bool>("d");
	tds->conf->logLevel = parser.get<string>("l");

	//确认程序运行模式
	string mode = fs::appName();
	string cmdlineMode = parser.get<string>("m");
	if (cmdlineMode != "")
		mode = cmdlineMode;

	//设置当前路径为程序运行目录 tdsConf.loadConf();中的相对路径解析会用到当前路径
	tdsImp.setWorkingDir();

	//根据模式差异化加载配置
	tdsImp.tdsConf.mode = mode;
	tdsImp.tdsConf.m_confFileName = parser.get<string>("tc");
	tdsImp.tdsConf.m_confFileName = str::trimSuffix(tdsImp.tdsConf.m_confFileName, ".ini");
	tdsImp.tdsConf.loadConf();


	int simuRecCount = parser.get<int>("gsd");
	if (simuRecCount > 0)
	{
		tds->conf->mode = "cmd";
		LOG("正在向数据库写入" + to_string(simuRecCount) + "条数据...");
		SYSTEMTIME stT;
		GetLocalTime(&stT);
		json j;
		j["time"] = timeopt::st2str(stT);
		j["temp"] = 23.5;
		j["humidity"] = 65.1;
		j["pm25"] = 45.5;
		j["co2"] = 345.1;

		json jA;
		SYSTEMTIME ststart;
		GetLocalTime(&ststart);
		for (int i = 0; i < simuRecCount; i++)
		{
			db.Insert("devicedata", stT, j);
		}

		int tt = timeopt::CalcTimePassMilliSecond(ststart);
		LOG("写入" + to_string(simuRecCount) + "条仿真数据, 位号:devicedata, 耗时" + str::fromInt(tt) + "ms");
		return 0;
	}

	//专业版创建授权文件
	if (tds->createLicence)
	{
		tds->createLicence();
	}



	if (mode == "watchDog" || mode == "wd" || mode == "dog" || mode == "tdsd")
	{
		watchDog.run();
	}
	else if (mode == "js")
	{
		//doShell();
	}
	else if (mode == "hs" || mode == "httpServer" || mode == "httpserver") //httpServer
	{
		httplib::Server* httpSrv  = new httplib::Server;
		string webPath = fs::appPath();
		if (fs::fileExist(webPath))
		{
			string asc_path = charCodec::utf8toAnsi(webPath);
			httpSrv->set_mount_point("/", +asc_path.c_str());
			LOG("[keyinfo][HTTP服务器] 根目录: " + webPath);
		}

		LOG("[keyinfo][HTTP服务器] 端口: " + str::fromInt(tds->conf->httpPort));

		httpSrv->listen("0.0.0.0", tds->conf->httpPort);
	}
	else if (mode == "dog")
	{
		watchDog.run();
	}
	else if (mode == "hub")
	{
		tcpHub* tr = new tcpHub();
		tr->portLeft = parser.get<int>("sl");
		tr->portRight = parser.get<int>("sr");
		tr->run();
	}
	else if (mode == "switch")
	{
		tcpSwitch* tr = new tcpSwitch();
		tr->portLeft = parser.get<int>("sl");
		tr->portRight = parser.get<int>("sr");
		tr->run();
	}
	else if (mode == "rphttp")
	{
		rpProxy  = new RProxy();
		rpProxy->run();
	}
	else if (mode == "rptcp")
	{
		tcpReverseProxy* tr = new tcpReverseProxy();
		tr->realHost = parser.get<string>("sb");
		tr->proxyPort = parser.get<int>("pp");
		tr->run();
	}
	else if (mode == "tcp2com")
	{
		fp_toolRun ptr = tds->tools["tcp2com"];
		if (ptr) ptr();
	}
	else if (mode == "replace") {
		string path = parser.get<string>("path");
		string oldstr = parser.get<string>("oldstr");
		string newstr = parser.get<string>("newstr");
		Tools::replaceStrInFile(path, oldstr, newstr);
		return 0;
	}
	else if (mode == "gb2u8")
	{
		vector<string> fileList;
		fs::getFileList(fileList, fs::appPath());
		LOG("文件总数:" + str::fromInt(fileList.size()));
		try {
			for (int i = 0; i < fileList.size(); i++)
			{
				string p = fs::appPath() + "/" + fileList[i];
				if (p.find(".h") == string::npos && p.find(".cpp") == string::npos)
				{
					continue;
				}

				string gbData;
				fs::readFile(p, gbData);
				if (charCodec::hasGB2312(gbData))
				{
					string u8Data = charCodec::ansi2Utf8(gbData);
					fs::writeFile(p, u8Data);
					LOG("已转换:" + p);
				}
				else
				{
					LOG("未找到GB2312字符:" + p);
				}
			}
		}
		catch (exception& e)
		{
			string es = e.what();
			LOG("转换异常:" + es);
		}
		return 0;
	}
	else
	{
		//run tds
		logger.m_bSaveToFile = true;
		tds->run();
	}


	// 消息循环  
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}
#else
#endif // !_WINDLL

#define DllExport   extern "C" __declspec( dllexport )
DllExport i_tds* getTds() {
	return &tdsImp;
}


//如果tds主程序中不使用_strdup ， strspn 这两个函数
// /MT 编译的openssl的crypto.lib会出现   这两个函数unresolved错误
//可能openssl编译的时候没有指定需要链接的lib. 主程序使用则产生了lib链接。原因不明。后续研究
void forLink() {
	char* a = new char[100];
	memset(a, 0, 100);
	string b = "abc";
	a = _strdup(b.c_str());
	int pos = strspn(a, "b");
}





