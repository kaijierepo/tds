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
#include "tools/tcp2com.h"

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

代码不安全，未来需优化的地方，全局搜索 [unsafe]

*/



#include "ioDev_mqttBroker.h"

class TDS_imp;
TDS_imp tdsImp; //tds instance;
iTDS* tds = &tdsImp;

//exe模式下，都会有命令行窗口，通过设置 ui_mode = chrome 或者 miniblink打开 浏览器窗口
//dll模式下，默认没有命名行窗口，通过设置 ui_mode = console 来打开命令行窗口


#ifndef _WINDLL
//#pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )//不显示默认控制台
int main(int argc, char** argv)
{
	//use cmd line conf first ,or use tds.json 
	cli::Parser parser(argc, argv);
	parser.set_optional<string>("m", "mode", "tds",charCodec::utf8toAnsi(
"hub: tcp集线器模式，左侧数据将发往右侧所有连接;右侧数据将发往左侧所有连接\r\n\
      示例:  tds -m hub -sl 666 -sr 667\r\n\
   switch: tcp交换机模式\r\n\
   rproxy: 反向代理模式\r\n\
   tcp2com: tcp转串口模式;\r\n\
      示例:  tds -m tcp2com -com COM1 -tcpc 127.0.0.1:666"));
	parser.set_optional<int>("sl", "serverleft", 666, "");
	parser.set_optional<int>("sr", "serverright", 667, "");
	parser.set_optional<string>("com", "com", "COM1", "com port number in tcp2com mode");
	parser.set_optional<string>("tcpc", "tcpc", "", "tcp client in format XXX.XXX.XXX.XXX:XXXX");
	parser.set_optional<string>("tcps", "tcps", "", "tcp server in format XXXX");
	parser.set_optional<string>("sb", "serverbackend", "127.0.0.1:666", "backend server in reverse proxy mode");
	parser.set_optional<int>("pp", "proxyport", 667, "proxy server port in reverse proxy mode");
	parser.set_optional<int>("p", "port", 0, "Integers in all forms, e.g., unsigned int, long long, ..., are possible. Hexadecimal and Ocatl numbers parsed as well");
	parser.set_optional<bool>("d", "debug", false, "run in debug mode. heartbeat will be closed;more log will be added;");
	parser.set_optional<string>("l", "loglevel", "debug", "value can be detail,trace,debug,warn,error");
	parser.set_optional<int>("baudRate", "baudRate", 19200, charCodec::utf8toAnsi("串口波特率"));
	parser.set_optional<int>("byteSize", "byteSize", 8, charCodec::utf8toAnsi("串口数据位"));
	parser.set_optional<string>("stopBits", "stopBits", "1", charCodec::utf8toAnsi("停止位"));
	parser.set_optional<string>("parity", "parity", "None", charCodec::utf8toAnsi("校验位"));
	parser.run_and_exit_if_error();
	tds->conf->port = parser.get<int>("p");
	tds->conf->debugMode = parser.get<bool>("d");
	tds->conf->logLevel = parser.get<string>("l");

	string mode = parser.get<string>("m");
	if (mode == "hub")
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
	else if (mode == "rproxy")
	{
		tcpReverseProxy* tr = new tcpReverseProxy();
		tr->realHost = parser.get<string>("sb");
		tr->proxyPort = parser.get<int>("pp");
		tr->run();
	}
	else if (mode == "tcp2com")
	{
		tcp2com* t2c =  new tcp2com();
		string tcpc = parser.get<string>("tcpc");
		string tcpc_ip;
		int tcpc_port;
		if (!str::parseIpPort(tcpc, tcpc_ip, tcpc_port))
		{
			LOG("参数错误");
			return 0;
		}
		t2c->m_strDestIp = tcpc_ip;
		t2c->m_iDestPort = tcpc_port;
		t2c->serial.m_baudRate = parser.get<int>("baudRate");
		t2c->serial.m_parity = parser.get<string>("parity");
		t2c->serial.m_byteSize = parser.get<int>("byteSize");
		t2c->serial.m_stopBits = parser.get<string>("stopBits");
		t2c->serial.m_portNum = parser.get<string>("com");
		t2c->run();
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
#endif // !_WINDLL

#define DllExport   extern "C" __declspec( dllexport )
DllExport iTDS* getTds() {
	return &tdsImp;
}






