#pragma once
#include <string>
#include "mongoose.h"
using namespace std;

struct MQTT_SRV_CONF {
    string ip;
	int port;
	string user;
	string pwd;
	string subTopics;
	int qos;
};

class MqttSrv
{
public:
	bool run();
	void stop();
	MqttSrv();
	virtual ~MqttSrv();
	
};


extern MqttSrv mqttSrv;