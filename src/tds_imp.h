#pragma once
#include "tds.h"
#include "conf.h"

class TDS_imp : public iTDS {
public:
	TDS_imp();


	string getVer() {
		return "1.0.0";
	}

	bool setEncodeing(string encoding);//接口字符串传递使用的字符编码
	string getUIMode();
	bool setWorkingDir();
	bool run(string cmdline = "");
	bool setProcBeforeExit(fp_procBeforeExit callback);

	// tds 数据服务功能
	 string call(string method, string param,string& error);
	 void setRpcHandler(fp_rpcHandler handler);
	 void rpcNotify(string method, string params="", string sessionId="");

	// io 通信服务功能
	 bool sendToIoAddr(string ioAddr, const char* p, int l);
	 bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback);

	 // 视频功能
	 void registerVideoTag(string tag, fp_startStream startStream, void*& mp, STREAM_INFO* si = NULL);
	 void pushStream(void* mp, char* pData, int len, STREAM_TYPE st, STREAM_INFO* si = NULL);

	 void log(const char* text);

	 tdsConfig tdsConf;
	 fp_procBeforeExit m_fpProcBeforeExit;
};

extern void createConsole();
extern TDS_imp tdsImp; //tds instance;
extern iTDS* tds;