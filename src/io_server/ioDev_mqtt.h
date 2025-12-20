#pragma once
#include "ioDev.h"

class ioDev_mqtt : public ioDev
{
public:
	ioDev_mqtt();
	~ioDev_mqtt();

	bool run() override;
	void stop() override;

	bool m_bThreadRunning;
	bool m_bConnected;
	bool m_bStop;
};


