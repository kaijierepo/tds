#pragma once
#include <string>
using namespace std;

typedef void* (*fp_ioAddrRecv)(void* user, char* pData, int iLen);

struct iTDSConf {
	int port;
	bool debugMode;
	string projectConfPath;
	bool bConcurrentGateway;
	string dbPath;
	string dataCenterIp;
};


//interface of TDS
class iTDS {
public:
	virtual string getVer() = 0;
	virtual bool setEncodeing(string encoding) = 0; // utf8 or gb2312
	virtual bool run(string cmdline = "") = 0;

	// tds 数据服务功能
	virtual string call(string method, string param) = 0;

	// io 通信服务功能
	virtual bool sendToIoAddr(string ioAddr,char* p,int l) = 0;
	virtual bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback) = 0;

	// 通用服务功能
	virtual void log(char* text) = 0;

	//数据接口
	iTDSConf* conf;
};


#ifndef _TDS

typedef iTDS* (*fp_getTds)();

inline iTDS* getITDS() {
	HMODULE hMod = LoadLibrary("tds.dll");
	if (hMod)
	{
		fp_getTds pGetTds = (fp_getTds)GetProcAddress(hMod, "getTds");
		if (pGetTds)
		{
			return pGetTds();
		}
	}
	return NULL;
}

#endif

extern iTDS* tds;