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
                    if(yyjson_is_int(yy_port))
                        conf.port = yyjson_get_int(yy_port);
                    else if (yyjson_is_str(yy_port)) {
                        conf.port = str::toInt(yyjson_get_str(yy_port));
                    }
                    yyjson_val* yy_user = yyjson_obj_get(item, "user");
                    if(yy_user)
                        conf.user = yyjson_get_str(yy_user);
                    yyjson_val* yy_pwd = yyjson_obj_get(item, "pwd");
                    if(yy_pwd)
                        conf.pwd = yyjson_get_str(yy_pwd);
                    yyjson_val* yy_qos = yyjson_obj_get(item, "qos");
                    if(yy_qos)
                        conf.qos = yyjson_get_int(yy_qos);
                    yyjson_val* yy_subTopics = yyjson_obj_get(item, "subTopics");
                    if (yy_subTopics)
                        conf.subTopics = yyjson_get_str(yy_subTopics);
                    yyjson_val* yy_recvScript = yyjson_obj_get(item, "recvScript");
                    if(yy_recvScript)
                        conf.recvScript = yyjson_get_str(yy_recvScript);
					yyjson_val* yy_sendScript = yyjson_obj_get(item, "sendScript");
                    if(yy_sendScript)
                        conf.sendScript = yyjson_get_str(yy_sendScript);
                    yyjson_val* yy_cycleScript = yyjson_obj_get(item, "cycleScript");
                    if(yy_cycleScript)
                        conf.cycleScript = yyjson_get_str(yy_cycleScript);
                    yyjson_val* yy_intervel = yyjson_obj_get(item, "intervel");
                    if (yyjson_is_int(yy_intervel))
                        conf.intervel = yyjson_get_int(yy_intervel);
                    else if (yyjson_is_str(yy_intervel)) {
                        conf.intervel = str::toInt(yyjson_get_str(yy_intervel));
                    }
                    yyjson_val* yy_clientID = yyjson_obj_get(item, "clientID");
                    if(yy_clientID)
                    conf.clientID = yyjson_get_str(yy_clientID);
					m_masterDSConf.push_back(conf);
				}
			}
		}
	}

	for(int i = 0; i < m_masterDSConf.size(); i++){
        LOG("[MQTT-DS]started,%s:%d,password:%s,qos:%d,subTopic:%s,sendScript:%s,recvScript:%s", m_masterDSConf[i].ip.c_str(), m_masterDSConf[i].port, m_masterDSConf[i].pwd.c_str(), m_masterDSConf[i].qos, m_masterDSConf[i].subTopics.c_str(), m_masterDSConf[i].sendScript.c_str(), m_masterDSConf[i].recvScript.c_str());
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
    m_bConnectting = false;
}

MqttClt::~MqttClt()
{

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
    opts.client_id = mg_str("tds");

    string server = "mqtt://" + pDev->m_conf.ip + ":" + to_string(pDev->m_conf.port);
    c = mg_mqtt_connect(&mgr, server.c_str(), &opts, mqtt_fn, pDev);
    pDev->m_lastConnectTime.setNow();
    if (c == NULL) {
        MG_ERROR(("Failed to create MQTT connection"));
        return;
    }
	pDev->m_bConnectting = true;

    while (!pDev->m_bStop) {
        while (pDev->m_bConnected) {
            mg_mgr_poll(&mgr, 500);
        }

        timeopt::sleepMilli(100);

        if (pDev->m_bConnected == false && 
            pDev->m_bConnectting == false &&
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
            if (pDev->m_conf.cycleScript != "") {
                scriptManager.runScript(pDev->m_conf.cycleScript, strparams, strResult, strOutput);
            }
        }        

        if(pDev->m_conf.intervel != 0)
            timeopt::sleepMilli(pDev->m_conf.intervel * 1000);

        timeopt::sleepMilli(100);
    }
}

bool MqttClt::run(MASTER_SRV_CONF conf)
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

void MqttSrv::mqttPublish(string topic, string data)
{
    for (int i = 0; i < m_mqttClts.size(); i++) {
        m_mqttClts[i]->mqttPublish(topic, data);
    }
}

void MqttSrv::onTdsNotify(string method, string params)
{
    for (int i = 0; i < m_mqttClts.size(); i++) {
        m_mqttClts[i]->mqttPublish(method, params);
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
    if (m_conf.sendScript != "") {
        //只处理数据更新通知
        if (method != "onDataUpdate") {
            return;
        }


        ScriptEngine se;

#ifdef TDS
        se.m_engineInitFuncList.push_back(initTdsFunc);
#endif


        json jTdsNotify = json::object();
        //jTdsNotify["topic"] = topic;
		jTdsNotify["method"] = method;
        jTdsNotify["params"] = params;


        se.m_globalObj["TdsNotify"] = jTdsNotify;
        se.m_globalObj["MqttPublish"] = json::object();
        se.m_ioDevThis = this;

        SCRIPT_INFO si;
        scriptManager.getScript(m_conf.recvScript, si);
        se.runScript(si, m_lastRunInfo_onRecv);

        if (se.m_sError != "") {
            string s = str::format("[warn][MQTT]run script error,script:%s,err:%s,addr=%s:%d", si.name.c_str(), se.m_sError.c_str(), m_conf.ip.c_str(), m_conf.port);
            LOG(s);
        }
        else {

        }
    }
    return;
}

void MqttClt::onMqttConnected()
{
    
    return;
}
