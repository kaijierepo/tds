#ifndef TDS_FUNC_MODULE_TDSWATCHDOG_H
#define TDS_FUNC_MODULE_TDSWATCHDOG_H

#ifdef _WIN32
#include "tcpClt.h"
#include "udpSrv.h"
#include "kvIni.h"

#define FOOD_PLATE_PORT 651
#define FOOD_FEEDER_PORT  652

class tdsWatchDog : public ICallback_udpSrv
{
public:
	tdsWatchDog();
	void run();
	bool runProcess(string path);
	bool isProcessRun(string name);
	string getFileVerInfo(string path);
	string getCurTdsVer();
	string getUpdateTdsVer();
	//bool installService();
	//bool uninstallService();
	//bool isServiceInstalled();
	bool regSelfStart();
	bool unregSelfStart();
	bool isSelfStartReg();
	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession) override;
	TIME m_lastFeedTime;
	TIME m_lastUpdateCheckTime;
	string m_curVer;
	string m_tdsAddr;
	void log(string s);

	KV_INI m_conf;
};


extern tdsWatchDog watchDog;

#endif



#endif /* TDS_FUNC_MODULE_TDSWATCHDOG_H */
