#pragma once
#include "tds.h"
#include "conf.h"
#include "mp.h"

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
	 bool call(string method, string param, string& result);
	 void setRpcHandler(fp_rpcHandler handler);
	 void rpcNotify(string method, string params="", string sessionId="");

	// io 通信服务功能
	 bool enableIoLog(string ioAddr, bool bEnable);
	 bool sendToIoAddr(string ioAddr, const char* p, int l);
	 bool connectDev(string ioAddr);
	 bool isOnline(string ioAddr);
	 bool lockIoAddr(string ioAddr) ;
	 bool unlockIoAddr(string ioAddr) ;
	 bool setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback);

	 // 视频功能
	 void startStream(string streamId, STREAM_INFO* si = NULL);
	 void pushStream(string streamId, char* pData, int len, STREAM_INFO* si = NULL);
	 void pullStream(string streamId, void* user, fp_onVideoStreamRecv onRecvStream, STREAM_INFO* si = NULL);
	 void log(const char* text);

	 tdsConfig tdsConf;
	 fp_procBeforeExit m_fpProcBeforeExit;

	 void registerMsgSinker(fp_msgSinker sinker);
	 void publishMsg(MODULE_BUS_MSG& msg);
	 vector<fp_msgSinker> m_msgSinkers;
};

extern void createConsole();
extern TDS_imp tdsImp; //tds instance;
extern iTDS* tds;