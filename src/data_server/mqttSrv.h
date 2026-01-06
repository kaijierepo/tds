#pragma once
#include <string>
#include "mongoose.h"
#include "scriptEngine.h"
#include "common.h"
using namespace std;

struct MASTER_SRV_CONF {
    string ip;
	int port;
	string user;
	string pwd;
	string subTopics;
	int qos;
	string recvScript;       //数据接收脚本
	string sendScript;		 //数据发送脚本
	string cycleScript;		 //周期发送脚本
	int intervel;			 //周期发送间隔
	string clientID;
};

class MqttClt {
public:
	MqttClt();
	~MqttClt();

	struct mg_mgr mgr;  
	struct mg_connection* c;

	bool run(MASTER_SRV_CONF conf);
	void stop();
	void onRecvMqttData(string topic, string data);
	void onTdsNotify(string method, string params);
	void onMqttConnected();
	void confUpdated();
	MASTER_SRV_CONF m_conf;

	void mqttPublish(string topic, string data);

	SCRIPT_RUN_INFO m_lastRunInfo_onSend;
	SCRIPT_RUN_INFO m_lastRunInfo_onRecv;

	bool m_bThreadRunning;
	bool m_bConnected;
	bool m_bConnectting;
	bool m_bStop;
	TIME m_lastConnectTime;
	string m_lastSubTopics;
};

class MqttSrv
{
public:
	bool run();
	void stop();
	MqttSrv();
	virtual ~MqttSrv();

	void mqttPublish(string topic, string data);
	void onTdsNotify(string method,string params);

	vector<MASTER_SRV_CONF> m_masterDSConf;
	vector<MqttClt*> m_mqttClts;
};


extern MqttSrv mqttSrv;