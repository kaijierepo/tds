#include "pch.h"
#include "Tcp2wsRproxy.h"
#include "conf.h"
#include "common.hpp"

Tcp2wsRproxy* tcp2wsRproxy = nullptr;

string Tcp2wsRproxy::defaultConf()
{
	string s = R"(#tcp2ws 数据转发工具
#tcp服务参数
tcp_remote_ip=0        #tcp服务器ip
tcp_remote_port=0      #tcp服务端口


#websocket服务参数
ws_local_port=667         #websocket服务端口
wss_local_port=0          #websocket secure服务端口
)";

	s = str::replace(s, "\n", "\r\n");
	return s;
}

Tcp2wsRproxy::Tcp2wsRproxy()
{
}

void Tcp2wsRproxy::run()
{
	string confPath = fs::appPath() + "/tcp2ws.ini";
	if (!fs::fileExist(confPath))
	{
		string s = defaultConf();
		fs::writeFile(confPath, s);
	}

	TDS_INI tdsIni;
	tdsIni.load(confPath);

	
}
