#ifndef TDS_IO_SERVER_IODEV_MQTT_H
#define TDS_IO_SERVER_IODEV_MQTT_H

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



#endif /* TDS_IO_SERVER_IODEV_MQTT_H */
