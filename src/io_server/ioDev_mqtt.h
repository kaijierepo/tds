#pragma once
#include "ioDev.h"
#include "ioDev_custom.h"

class ioDev_mqtt : public ioDev_custom
{
public:
	ioDev_mqtt();
	~ioDev_mqtt();

	bool run() override;
	void stop() override;
	void confUpdated() override;

	void onRecvMqttData(string topic, string data);

	bool m_bThreadRunning;
	bool m_bConnected;
	bool m_bStop;

	string m_lastSubTopics;
};


