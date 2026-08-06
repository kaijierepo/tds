#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "mongoose.h"
#include "scriptEngine.h"
#include "common.h"
#include "uplinkConf.h"
using namespace std;

class MqttClt {
public:
	MqttClt();
	~MqttClt();

	struct mg_mgr mgr;  
	struct mg_connection* m_cltConn;

	bool run(UPLINK_CONF_MQTT conf);
	void stop();
	void onRecvMqttData(std::string topic, std::string data);
	void onTdsNotify(std::string method, std::string params);
	void onMqttConnected();
	void confUpdated();
	UPLINK_CONF_MQTT m_conf;

	void mqttPublish(std::string topic, std::string data);

	SCRIPT_RUN_INFO m_lastRunInfo_onSend;
	SCRIPT_RUN_INFO m_lastRunInfo_onRecv;

	bool m_bThreadRunning;
	bool m_bConnected;
	bool m_bConnectting;
	bool m_bStop;
	TIME m_lastConnectTime;
	std::string m_lastSubTopics;
	int m_socket;

	size_t m_sendBytes;
	size_t m_recvBytes;
	std::string m_lastActiveTime;
};

class MqttUplink
{
public:
	bool init(const std::vector<UPLINK_CONF_MQTT>& confs);
	bool run();
	void stop();
	bool reload(const std::vector<UPLINK_CONF_MQTT>& confs);
	MqttUplink();
	virtual ~MqttUplink();

	void mqttPublish(std::string topic, std::string data);
	void onTdsNotify(std::string method,std::string params);

	bool enableConnection(UPLINK_CONF_MQTT conf);
	bool disableConnection(std::string ip, int port);

	std::vector<UPLINK_CONF_MQTT> m_masterDSConf;
	std::vector<MqttClt*> m_mqttClts;

private:
	void loadConf(const std::vector<UPLINK_CONF_MQTT>& confs);
	void startClients();

	std::mutex m_mqttMutex;
};


extern MqttUplink mqttUplink;
