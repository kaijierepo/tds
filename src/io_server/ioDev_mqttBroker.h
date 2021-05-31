#pragma once
#include "ioDev.h"
#include "MQTTClient.h"

class ioDev_mqttBroker : public ioDev
{
public:
	ioDev_mqttBroker();
	~ioDev_mqttBroker();

	bool run() override;

	MQTTClient m_mqttClt;
};


