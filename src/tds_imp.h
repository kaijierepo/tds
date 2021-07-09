#pragma once
#include "tds.h"
#include "conf.h"

class TDS_imp : public iTDS {
public:
	TDS_imp();


	string getVer() {
		return "1.0.0";
	}

	bool setEncodeing(string encoding);

	 bool run(string cmdline = "");

	// tds 数据服务功能
	 string call(string method, string param);
	 void setRpcHandler(fp_rpcHandler handler);

	// io 通信服务功能
	 bool sendToIoAddr(string ioAddr, char* p, int l);
	 bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback);

	 void log(char* text);

	 tdsConfig tdsConf;
};
