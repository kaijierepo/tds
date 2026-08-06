#include "uplink_mqtt.h"
#include <iostream>
#include <sstream>
#include <thread>
#include "common.h"
#include "tdsConf.h"
#include "scriptEngine.h"
#include "scriptFunc.h"
#include "scriptManager.h"
#include "prj.h"
#include "mp.h"

#ifdef TDS
#include "logger.h"
#else

#endif

MqttUplink mqttUplink;

MqttUplink::MqttUplink()
{

}

MqttUplink::~MqttUplink()
{
	stop();
}

void MqttUplink::loadConf(const std::vector<UPLINK_CONF_MQTT>& confs) {
	m_masterDSConf.clear();

	for (size_t i = 0; i < confs.size(); i++) {
		if (confs[i].enabled) {
			m_masterDSConf.push_back(confs[i]);
		}
	}
}

bool MqttUplink::init(const std::vector<UPLINK_CONF_MQTT>& confs)
{
	loadConf(confs); return m_masterDSConf.size() > 0;
}

bool MqttUplink::run() {
	std::lock_guard<std::mutex> lk(m_mqttMutex);

	if (m_masterDSConf.size() == 0) {
		LOG("[MQTT-DS] no uplink mqtt config, skip MQTT startup");
		return false;
	}

	startClients();

	return true;
}

void MqttUplink::startClients() {
	for(int i = 0; i < m_masterDSConf.size(); i++){
		LOG("[MQTT-DS]started,%s:%d,password:%s,qos:%d,subTopic:%s,pubTopic:%s,format:%s,sendScript:%s,recvScript:%s", m_masterDSConf[i].ip.c_str(), m_masterDSConf[i].port, m_masterDSConf[i].pwd.c_str(), m_masterDSConf[i].qos, m_masterDSConf[i].subTopics.c_str(), m_masterDSConf[i].pubTopics.c_str(), m_masterDSConf[i].format.c_str(), m_masterDSConf[i].sendScript.c_str(), m_masterDSConf[i].recvScript.c_str());
		MqttClt* clt = new MqttClt();
		clt->run(m_masterDSConf[i]);
		m_mqttClts.push_back(clt);
	}
}

bool MqttUplink::reload(const std::vector<UPLINK_CONF_MQTT>& confs) {
	std::lock_guard<std::mutex> lk(m_mqttMutex);
	LOG("[MQTT-DS] reloading config...");

	// 1. 停止所有旧的 MQTT 客户端连接
	for (int i = 0; i < m_mqttClts.size(); i++) {
		if (m_mqttClts[i]) {
			m_mqttClts[i]->stop();
			delete m_mqttClts[i];
		}
	}
	m_mqttClts.clear();

	// 2. 重新加载配置
	loadConf(confs); if (m_masterDSConf.empty()) {
		LOG("[MQTT-DS] reload: no enabled mqtt configs");
		return false;
	}

	// 3. 启动新的 MQTT 客户端连接
	for (int i = 0; i < m_masterDSConf.size(); i++) {
		LOG("[MQTT-DS]reloaded,%s:%d,password:%s,qos:%d,subTopic:%s,pubTopic:%s,format:%s,sendScript:%s,recvScript:%s", m_masterDSConf[i].ip.c_str(), m_masterDSConf[i].port, m_masterDSConf[i].pwd.c_str(), m_masterDSConf[i].qos, m_masterDSConf[i].subTopics.c_str(), m_masterDSConf[i].pubTopics.c_str(), m_masterDSConf[i].format.c_str(), m_masterDSConf[i].sendScript.c_str(), m_masterDSConf[i].recvScript.c_str());
		MqttClt* clt = new MqttClt();
		clt->run(m_masterDSConf[i]);
		m_mqttClts.push_back(clt);
	}

	LOG("[MQTT-DS] reload completed, %d connections restarted", m_mqttClts.size());
	return true;
}

void MqttUplink::stop() {
	std::lock_guard<std::mutex> lk(m_mqttMutex);
	for (int i = 0; i < m_mqttClts.size(); i++) {
		if (m_mqttClts[i]) {
			m_mqttClts[i]->stop();
			delete m_mqttClts[i];
		}
	}
	m_mqttClts.clear();
}

bool MqttUplink::enableConnection(UPLINK_CONF_MQTT conf) {
	std::lock_guard<std::mutex> lk(m_mqttMutex);

	for (int i = 0; i < m_mqttClts.size(); i++) {
		if (m_mqttClts[i] && m_mqttClts[i]->m_conf.ip == conf.ip
			&& m_mqttClts[i]->m_conf.port == conf.port) {
			LOG("[MQTT-DS] connection %s:%d already running", conf.ip.c_str(), conf.port);
			return true;
		}
	}

	MqttClt* clt = new MqttClt();
	clt->run(conf);
	m_mqttClts.push_back(clt);
	LOG("[MQTT-DS] enabled connection %s:%d", conf.ip.c_str(), conf.port);
	return true;
}

bool MqttUplink::disableConnection(std::string ip, int port) {
	std::lock_guard<std::mutex> lk(m_mqttMutex);

	for (int i = 0; i < m_mqttClts.size(); i++) {
		if (m_mqttClts[i] && m_mqttClts[i]->m_conf.ip == ip
			&& m_mqttClts[i]->m_conf.port == port) {
			m_mqttClts[i]->stop();
			delete m_mqttClts[i];
			m_mqttClts.erase(m_mqttClts.begin() + i);
			LOG("[MQTT-DS] disabled connection %s:%d", ip.c_str(), port);
			return true;
		}
	}

	LOG("[MQTT-DS] disableConnection: %s:%d not found", ip.c_str(), port);
	return false;
}



MqttClt::MqttClt()
{
    m_bThreadRunning = false;
    m_bConnected = false;
    m_bStop = false;
    m_bConnectting = false;
	m_sendBytes = 0;
	m_recvBytes = 0;
}

MqttClt::~MqttClt()
{

}

static string getNowStr() {
	TIME t;
	t.setNow();
	char buff[50] = { 0 };
	sprintf(buff, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
		t.wYear, t.wMonth, t.wDay,
		t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	return buff;
}

static void mqtt_fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_MQTT_OPEN) {
        MqttClt* pDev = (MqttClt*)c->fn_data;
        pDev->m_bConnected = true;
        pDev->m_bConnectting = false;
        LOG("[MQTT-DS]connected,%s:%d", pDev->m_conf.ip.c_str(),pDev->m_conf.port);
        vector<string> vecTopics;
        str::split(vecTopics, pDev->m_conf.subTopics, ",");
        for (int i = 0; i < vecTopics.size(); i++) {
            string topic = vecTopics[i];
            struct mg_mqtt_opts sub_opts;
            sub_opts.user = mg_str(pDev->m_conf.user.c_str());
            sub_opts.pass = mg_str(pDev->m_conf.pwd.c_str());
            sub_opts.topic = mg_str(topic.c_str());
            sub_opts.qos = 0;
            mg_mqtt_sub(c, &sub_opts);
        }
        pDev->m_lastSubTopics = pDev->m_conf.subTopics;
        LOG("[MQTT-DS]sub topic:%s,qos:%d", pDev->m_conf.subTopics.c_str(), 0);
		pDev->onMqttConnected();
    }
    else if (ev == MG_EV_MQTT_MSG) {
        struct mg_mqtt_message* mm = (struct mg_mqtt_message*)ev_data;
        MqttClt* pDev = (MqttClt*)c->fn_data;
        string topic = str::fromBuff(mm->topic.ptr, mm->topic.len);
        string data = str::fromBuff(mm->data.ptr, mm->data.len);
		pDev->m_recvBytes += mm->topic.len + mm->data.len;
		pDev->m_lastActiveTime = getNowStr();
        pDev->onRecvMqttData(topic, data);
    }
    else if (ev == MG_EV_CLOSE) {
        MqttClt* pDev = (MqttClt*)c->fn_data;
        pDev->m_bConnectting = false;
        if (pDev->m_bConnected) {
            LOG("[MQTT-DS] disconnected,%s:%d", pDev->m_conf.ip.c_str(), pDev->m_conf.port);
            pDev->m_bConnected = 0;
        }
    }
    else if (ev == MG_EV_ERROR) {
        MqttClt* pDev = (MqttClt*)c->fn_data;
        pDev->m_bConnectting = false;
    }
    else if (ev == MG_EV_WAKEUP) {
        MqttClt* pClt = (MqttClt*)c->fn_data;


        struct mg_str* data = (struct mg_str*)ev_data;
        string s = str::fromBuff(data->ptr, data->len);
        size_t pos = s.find("\n\n");

        if (pos != string::npos) {
            string topic = s.substr(0, pos);
            string message = s.substr(pos + 2);
            struct mg_mqtt_opts opts = { 0 };
            opts.topic = mg_str(topic.c_str());
            opts.message = mg_str(message.c_str());
            opts.qos = pClt->m_conf.qos;       
            opts.retain = 0;  

            mg_mqtt_pub(c, &opts);
			pClt->m_sendBytes += topic.size() + message.size();
			pClt->m_lastActiveTime = getNowStr();
        }
    }
    else if (ev == MG_EV_POLL) {
    }
}

void thread_mqtt_client_comm(void* p)
{
    MqttClt* pDev = (MqttClt*)p;
    pDev->m_bThreadRunning = true;

    mg_mgr& mgr = pDev->mgr;  
    mg_connection*& c = pDev->m_cltConn;

    //mg_log_set(MG_LL_VERBOSE);
    mg_mgr_init(&mgr);  


    struct mg_mqtt_opts opts = { 0 };
    opts.clean = true;  
    opts.qos = 0;       
    opts.retain = 0;
    opts.keepalive = 60; 
    opts.version = 4;
    opts.user = mg_str(pDev->m_conf.user.c_str());
    opts.pass = mg_str(pDev->m_conf.pwd.c_str());
    opts.client_id = mg_str(pDev->m_conf.clientID.empty() ? "tds" : pDev->m_conf.clientID.c_str());

    string server = "mqtt://" + pDev->m_conf.ip + ":" + to_string(pDev->m_conf.port);
    c = mg_mqtt_connect(&mgr, server.c_str(), &opts, mqtt_fn, pDev);
    pDev->m_lastConnectTime.setNow();
    if (c == NULL) {
        MG_ERROR(("Failed to create MQTT connection"));
        return;
    }
	pDev->m_bConnectting = true;

    while (!pDev->m_bStop) {
        mg_mgr_poll(&mgr, 500);

        if (!pDev->m_bConnected &&
            !pDev->m_bConnectting &&
            timeopt::calcTimePassMilliSecond(pDev->m_lastConnectTime) > 5000)
        {
            c = mg_mqtt_connect(&mgr, server.c_str(), &opts, mqtt_fn, pDev);
            pDev->m_lastConnectTime.setNow();
            if (c) {
                pDev->m_bConnectting = true;
            }
        }
    }

    mg_mgr_free(&mgr);

    pDev->m_bThreadRunning = false;
}

void thread_mqtt_script(void* p) {
    MqttClt* pDev = (MqttClt*)p;

    while (1) {
        if (pDev->m_bStop) {
            break;
        }

        if (pDev->m_bThreadRunning && pDev->m_bConnected && pDev->m_conf.intervel != 0) {
            //执行脚本
            string strparams = "";
            string strResult = "";
            string strOutput = "";
            if (pDev->m_conf.format == "default") {
                map<string, MP*> mapAllMP;
                prj.getMpList(mapAllMP);

                json output = json::array();
                for (auto& it : mapAllMP) {
                    MP* pmp = it.second;
                    //if (pmp->m_valType == "video") continue;
                    json item;
                    item["tag"] = pmp->getTag();
                    item["val"] = !pmp->m_curVal.empty() ? json::parse(pmp->m_curVal) : json(nullptr);
                    item["time"] = pmp->m_stDataLastUpdate.toStr(true);
                    output.push_back(item);
                }

                pDev->mqttPublish(pDev->m_conf.pubTopics, output.dump());
            }
            else if (pDev->m_conf.cycleScript != "") {
                scriptManager.runScript(pDev->m_conf.cycleScript, strparams, strResult, strOutput);
            }
        }        

        if(pDev->m_conf.intervel != 0)
            timeopt::sleepMilli(pDev->m_conf.intervel * 1000);

        timeopt::sleepMilli(100);
    }
}

bool MqttClt::run(UPLINK_CONF_MQTT conf)
{
    m_conf = conf;
    m_bStop = false;

    thread t(thread_mqtt_client_comm, this);
    t.detach();

    thread t2(thread_mqtt_script, this);
    t2.detach();

    return true;
}

void MqttClt::stop()
{
    m_bStop = true;
    while (m_bThreadRunning) {
        timeopt::sleepMilli(100);
    }
    return;
}

void MqttClt::confUpdated()
{
    if (m_lastSubTopics != m_conf.subTopics) {
        LOG("[MQTT]订阅修改，老主题 %s,新主题 %s", m_lastSubTopics.c_str(), m_conf.subTopics.c_str());
        stop();
        run(m_conf);
    }
}

void MqttUplink::mqttPublish(string topic, string data)
{
    for (int i = 0; i < m_mqttClts.size(); i++) {
        m_mqttClts[i]->mqttPublish(topic, data);
    }
}

void MqttUplink::onTdsNotify(string method, string params)
{
    for (int i = 0; i < m_mqttClts.size(); i++) {
        m_mqttClts[i]->onTdsNotify(method, params);
    }
}

void MqttClt::mqttPublish(string topic, string data)
{
    //string s = topic + "\n\n" + data;
    //mg_wakeup(&mgr, c->id, s.c_str(), (int)s.length());  // Respond to parent

    if (m_bConnected) {
        struct mg_mqtt_opts opts = { 0 };
        opts.topic = mg_str(topic.c_str());
        opts.message = mg_str(data.c_str());
        opts.user = mg_str(m_conf.user.c_str());
        opts.pass = mg_str(m_conf.pwd.c_str());
        opts.qos = m_conf.qos;
        opts.retain = 0;

        mg_mqtt_pub(m_cltConn, &opts);
		m_sendBytes += topic.size() + data.size();
		m_lastActiveTime = getNowStr();
    }
}

void MqttClt::onRecvMqttData(string topic, string data)
{
    if (m_conf.format == "default") {
        return;
    }
    if (m_conf.recvScript != "") {
        ScriptEngine se;

#ifdef TDS
        se.m_engineInitFuncList.push_back(initTdsFunc);
#endif


        json jMqttMsg = json::object();
        jMqttMsg["topic"] = topic;
        jMqttMsg["data"] = data;


        se.m_globalObj["MqttMsg"] = jMqttMsg;
        se.m_ioDevThis = this;

        SCRIPT_INFO si;
        scriptManager.getScript(m_conf.recvScript, si);
        se.runScript(si, m_lastRunInfo_onRecv);

        if (se.m_sError != "") {
            string s = str::format("[warn][MQTT]run script error,script:%s,err:%s,addr=%s:%d", si.name.c_str(), se.m_sError.c_str(), m_conf.ip.c_str(),m_conf.port);
            LOG(s);
        }
        else {

        }
    }
    return;
}

void MqttClt::onTdsNotify(string method,string params)
{
    if (m_conf.format == "default") {
        mqttPublish(m_conf.pubTopics, params);
        return;
    }

    // 自定义格式：由"数据发送脚本"负责上送，不再走默认格式的上送逻辑
    if (m_conf.sendScript != "") {
        string strResult, strOutput;
        scriptManager.runScript(m_conf.sendScript, params, strResult, strOutput);
    }
}

void MqttClt::onMqttConnected()
{
    if (m_conf.format == "default") {
        map<string, MP*> mapAllMP;
        prj.getMpList(mapAllMP);

        json output = json::array();
        for (auto& it : mapAllMP) {
            MP* pmp = it.second;
            //if (pmp->m_valType == "video") continue;
            json item;
            item["tag"] = pmp->getTag();
            item["val"] = !pmp->m_curVal.empty() ? json::parse(pmp->m_curVal) : json(nullptr);
            item["time"] = pmp->m_stDataLastUpdate.toStr(true);
            output.push_back(item);
        }

        mqttPublish(m_conf.pubTopics, output.dump());
        return;
    }
    if (m_conf.connectScript != "") {
        string strResult, strOutput;
        scriptManager.runScript(m_conf.connectScript, "", strResult, strOutput);
    }
}
