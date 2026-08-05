#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "mongoose.h"
#include "scriptEngine.h"
#include "common.h"
using namespace std;

struct MASTER_SRV_CONF {
    std::string ip;
	int port;
	std::string user;
	std::string pwd;
	std::string subTopics;
	std::string pubTopics;
	int qos;
	std::string format;
	std::string connectScript;
	std::string recvScript;       
	std::string sendScript;		 
	std::string cycleScript;		 
	int intervel;			
	std::string clientID;
};

class MqttClt {
public:
	MqttClt();
	~MqttClt();

	struct mg_mgr mgr;  
	struct mg_connection* m_cltConn;

	bool run(MASTER_SRV_CONF conf);
	void stop();
	void onRecvMqttData(std::string topic, std::string data);
	void onTdsNotify(std::string method, std::string params);
	void onMqttConnected();
	void confUpdated();
	MASTER_SRV_CONF m_conf;

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
};

class MqttSrv
{
public:
	bool run();
	void stop();
	bool reload();
	MqttSrv();
	virtual ~MqttSrv();

	void mqttPublish(std::string topic, std::string data);
	void onTdsNotify(std::string method,std::string params);

	std::vector<MASTER_SRV_CONF> m_masterDSConf;
	std::vector<MqttClt*> m_mqttClts;

private:
	bool loadConfFromFile();
	void startWatch();
	void watchLoop();

	std::thread m_watchThread;
	std::atomic<bool> m_bWatchRunning{false};
	std::mutex m_mqttMutex;
	std::string m_lastLoadedContent;
};


extern MqttSrv mqttSrv;