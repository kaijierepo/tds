#pragma once
#include <string>
#include "mongoose.h"
#include "scriptEngine.h"
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
};

class MqttClt {
public:
	MqttClt();
	~MqttClt();

	bool run(MASTER_SRV_CONF conf);
	void stop();
	void onRecvMqttData(string topic, string data);
	void onSendTdsNotify(string notify);
	void confUpdated();
	MASTER_SRV_CONF m_conf;

	SCRIPT_RUN_INFO m_lastRunInfo_onSend;
	SCRIPT_RUN_INFO m_lastRunInfo_onRecv;

	bool m_bThreadRunning;
	bool m_bConnected;
	bool m_bStop;
	string m_lastSubTopics;
};

class MqttSrv
{
public:
	bool run();
	void stop();
	MqttSrv();
	virtual ~MqttSrv();

	vector<MASTER_SRV_CONF> m_masterDSConf;
	vector<MqttClt*> m_mqttClts;
};


extern MqttSrv mqttSrv;