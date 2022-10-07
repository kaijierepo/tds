#ifdef WEB_SERVER2
//使用httplib实现的webserver,由于httplib不支持websocket，暂时弃用
#pragma once
#include "tdsSession.h"


class WebServer2 {
public:
	WebServer2();
	~WebServer2();
	
};
#endif
