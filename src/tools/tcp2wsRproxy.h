#pragma once
#include "webSrv.h"
#include "tcpClt.h"

class Tcp2wsRproxy {
public:
	Tcp2wsRproxy();
	string defaultConf();
	void run();
	ServiceInterface m_webSrv;
	map<DWORD, tcpClt*> m_connMap;


	string tcp_remote_ip; //        #tcp服务器ip
	int tcp_remote_port;  //    #tcp服务端口


	//#websocket服务参数
	int ws_local_port;//        #websocket服务端口
	int wss_local_port;//          #websocket secure服务端口
};

extern Tcp2wsRproxy* tcp2wsRproxy;