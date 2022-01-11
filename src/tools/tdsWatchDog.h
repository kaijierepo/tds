#pragma once
#include "tcpClt.h"

class tdsWatchDog
{
public:
	tdsWatchDog();
	void run();

	tcpClt m_tcpClt;
};


class tdsDogFeeder {
public:
	tdsDogFeeder() {};
	void run();

};

extern tdsWatchDog watchDog;
extern tdsDogFeeder dogFeeder;

