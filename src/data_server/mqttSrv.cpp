#include "mqttSrv.h"
#include <iostream>
#include <sstream>
#include <thread>
#include "common.h"
#include "tdsConf.h"
#include "scriptEngine.h"
#include "scriptFunc.h"
#include "scriptManager.h"

#ifdef TDS
#include "logger.h"
#else

#endif

MqttSrv mqttSrv;

MqttSrv::MqttSrv()
{

}

MqttSrv::~MqttSrv()
{
}

bool MqttSrv::run() {
	
	string s;
	string p = tds->conf->confPath + "/masterDS.json";
	if (fs::readFile(p, s)) {
		yyjson_doc *doc = yyjson_read(s.c_str(), s.size(), 0);
		if (doc) {
			yyjson_val* root = yyjson_doc_get_root(doc);
			size_t max, idx;
			yyjson_val* item;
			yyjson_arr_foreach(root, idx, max, item) { 
				yyjson_val* yy_proto = yyjson_obj_get(item, "proto");
				if (!yy_proto)
					continue;
				string proto = yyjson_get_str(yy_proto);
                if (proto == "mqtt") { 
                    MASTER_SRV_CONF conf;
					yyjson_val* yy_ip = yyjson_obj_get(item, "ip");
					conf.ip = yyjson_get_str(yy_ip);
                    yyjson_val* yy_port = yyjson_obj_get(item, "port");
                    conf.port = yyjson_get_int(yy_port);
                    yyjson_val* yy_pwd = yyjson_obj_get(item, "pwd");
                    conf.pwd = yyjson_get_str(yy_pwd);
                    yyjson_val* yy_qos = yyjson_obj_get(item, "qos");
                    conf.qos = yyjson_get_int(yy_qos);
                    yyjson_val* yy_subTopics = yyjson_obj_get(item, "subTopics");
                    conf.subTopics = yyjson_get_str(yy_subTopics);
                    yyjson_val* yy_recvScript = yyjson_obj_get(item, "recvScript");
                    conf.recvScript = yyjson_get_str(yy_recvScript);
					yyjson_val* yy_sendScript = yyjson_obj_get(item, "sendScript");
                    conf.sendScript = yyjson_get_str(yy_sendScript);
					m_masterDSConf.push_back(conf);
				}
			}
		}
	}

	for(int i = 0; i < m_masterDSConf.size(); i++){
        LOG("[北向MQTT]启动,%s:%d,密码:%s,qos:%d,订阅topic:%s,发送脚本:%s,接收脚本:%s", m_masterDSConf[i].ip.c_str(), m_masterDSConf[i].port, m_masterDSConf[i].pwd.c_str(), m_masterDSConf[i].qos, m_masterDSConf[i].subTopics.c_str(), m_masterDSConf[i].sendScript.c_str(), m_masterDSConf[i].recvScript.c_str());
        MqttClt* clt = new MqttClt();
        clt->run(m_masterDSConf[i]);
        m_mqttClts.push_back(clt);
	}
    return true;
}



MqttClt::MqttClt()
{
    m_bThreadRunning = false;
    m_bConnected = false;
    m_bStop = false;
}

MqttClt::~MqttClt()
{

}

static void mqtt_fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_MQTT_OPEN) {
        MqttClt* pDev = (MqttClt*)c->fn_data;
        pDev->m_bConnected = true;
        LOG("[北向MQTT]connected,%s:%d", pDev->m_conf.ip.c_str(),pDev->m_conf.port);
        vector<string> vecTopics;
        str::split(vecTopics, pDev->m_conf.subTopics, ",");
        for (int i = 0; i < vecTopics.size(); i++) {
            string topic = vecTopics[i];
            struct mg_mqtt_opts sub_opts;
            sub_opts.topic = mg_str(topic.c_str());
            sub_opts.qos = 0;
            mg_mqtt_sub(c, &sub_opts);
        }
        pDev->m_lastSubTopics = pDev->m_conf.subTopics;
        LOG("[北向MQTT]订阅topic:%s,qos:%d", pDev->m_conf.subTopics.c_str(), 0);
    }
    else if (ev == MG_EV_MQTT_MSG) {
        // 收到 MQTT 消息
        struct mg_mqtt_message* mm = (struct mg_mqtt_message*)ev_data;
        MqttClt* pDev = (MqttClt*)c->fn_data;
        string topic = str::fromBuff(mm->topic.ptr, mm->topic.len);
        string data = str::fromBuff(mm->data.ptr, mm->data.len);
        pDev->onRecvMqttData(topic, data);
    }
    else if (ev == MG_EV_CLOSE) {
        MqttClt* pDev = (MqttClt*)c->fn_data;
        LOG("[北向MQTT] disconnected,%s:%d", pDev->m_conf.ip.c_str(),pDev->m_conf.port);
        if (pDev->m_bConnected) {
            pDev->m_bConnected = 0;
        }
    }
    else if (ev == MG_EV_ERROR) {
        // 错误事件
        MG_ERROR(("Error: %s", (char*)ev_data));
    }
    else if (ev == MG_EV_POLL) {

    }
}

void thread_mqtt_client_comm(void* p)
{
    MqttClt* pDev = (MqttClt*)p;
    pDev->m_bThreadRunning = true;

    struct mg_mgr mgr;  // 事件管理器
    struct mg_connection* c;

    //mg_log_set(MG_LL_VERBOSE);
    mg_mgr_init(&mgr);  // 初始化事件管理器

    // 配置 MQTT 连接选项
    struct mg_mqtt_opts opts = { 0 };
    opts.clean = true;  // 清除会话
    opts.qos = 0;       // QoS 1 时无法与mosquitto建立连接，后续再研究
    opts.retain = 0;
    opts.keepalive = 60; // 保持连接时间（秒）
    opts.version = 4;
    opts.client_id = mg_str("tds");

    // 建立 MQTT 连接
    string server = "mqtt://" + pDev->m_conf.ip + ":" + to_string(pDev->m_conf.port);
    c = mg_mqtt_connect(&mgr, server.c_str(), &opts, mqtt_fn, pDev);
    if (c == NULL) {
        MG_ERROR(("Failed to create MQTT connection"));
        return;
    }

    // 事件循环
    for (;;) {
        mg_mgr_poll(&mgr, 500);  // 每秒轮询一次
        if (pDev->m_bStop) {
            break;
        }
    }

    mg_mgr_free(&mgr);

    pDev->m_bThreadRunning = false;
    pDev->m_bStop = false;
}


bool MqttClt::run(MASTER_SRV_CONF conf)
{
    m_conf = conf;
    thread t(thread_mqtt_client_comm, this);
    t.detach();
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
        LOG("[MQTT]订阅修改，老订阅:%s,新订阅:%s", m_lastSubTopics.c_str(), m_conf.subTopics.c_str());
        stop();
        run(m_conf);
    }
}

void MqttClt::onRecvMqttData(string topic, string data)
{
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
            string s = str::format("[warn][北向MQTT]脚本执行错误，脚本=%s,错误=%s,连接=%s:%d", si.name.c_str(), se.m_sError.c_str(), m_conf.ip.c_str(),m_conf.port);
            LOG(s);
        }
        else {

        }
    }
    return;
}

void MqttClt::onSendTdsNotify(string notify)
{
    if (m_conf.sendScript != "") {
        ScriptEngine se;

#ifdef TDS
        se.m_engineInitFuncList.push_back(initTdsFunc);
#endif


        json jTdsNotify = json::object();
        //jTdsNotify["topic"] = topic;
        jTdsNotify["data"] = notify;


        se.m_globalObj["TdsNotify"] = jTdsNotify;
        se.m_globalObj["MqttPublish"] = json::object();
        se.m_ioDevThis = this;

        SCRIPT_INFO si;
        scriptManager.getScript(m_conf.recvScript, si);
        se.runScript(si, m_lastRunInfo_onRecv);

        if (se.m_sError != "") {
            string s = str::format("[warn][北向MQTT]脚本执行错误，脚本=%s,错误=%s,连接=%s:%d", si.name.c_str(), se.m_sError.c_str(), m_conf.ip.c_str(), m_conf.port);
            LOG(s);
        }
        else {

        }
    }
    return;
}
