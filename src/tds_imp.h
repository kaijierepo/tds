#pragma once
#include "tds.h"

class TDS_img : public iTDS {
public:
	string getVer() {
		return "1.0.0";
	}

	 bool run(string cmdline = "");

	// tds 数据服务功能
	 string call(string method, string param);

	// io 通信服务功能
	 bool sendToIoAddr(string ioAddr);
	 bool setIoAddrRecvCallback(fp_ioAddrRecv recvCallback);

	 void log(string text);

};
