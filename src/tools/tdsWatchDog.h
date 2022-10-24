#pragma once
#include "tcpClt.h"
#include "udpSrv.h"

#define FOOD_PLATE_PORT 651
#define FOOD_FEEDER_PORT  652

class tdsWatchDog : public IUdpServerCallBack
{
public:
	tdsWatchDog();
	void run();
	string getFileVerInfo(string path);
	string getCurTdsVer();
	string getUpdateTdsVer();
	bool installService();
	bool uninstallService();
	bool isServiceInstalled();
	bool regSelfStart();
	bool unregSelfStart();
	bool isSelfStartReg();
	void OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port) override;
	udpServer m_foodPlate; //食物盘子
	SYSTEMTIME m_lastFeedTime;
	SYSTEMTIME m_lastUpdateCheckTime;
	string m_curVer;
	void log(string s);
};


class tdsDogFeeder {
public:
	tdsDogFeeder() {};
	void run();
	void sendFood();

	udpServer m_foodCart; //发食物的袋子
};

extern tdsWatchDog watchDog;
extern tdsDogFeeder dogFeeder;

