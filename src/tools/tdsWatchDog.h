#pragma once
#include "tcpClt.h"
#include "udpSrv.h"

class tdsWatchDog : public IUdpServerCallBack
{
public:
	tdsWatchDog();
	void run();
	int OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port) override;
	udpServer m_foodPlate; //食物盘子
	SYSTEMTIME m_lastFeedTime;
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

