#pragma once
#include "tcpClt.h"

class tdsWatchDog
{
public:
	tdsWatchDog();
	void run();

	tcpClt m_tcpClt;
};

extern tdsWatchDog watchDog;

