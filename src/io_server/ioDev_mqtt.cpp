#include "pch.h"
#include "ioDev_mqtt.h"
#include "logger.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "mongoose.h"
#include "scriptManager.h"
#include "scriptEngine.h"
#include "scriptFunc.h"


#define EXIT_FAILURE -1
#define CLIENTID    "tds"
#define QOS         1
#define TIMEOUT     10000L



namespace ns_ioDev_mqtt {
    ioDev* createDev()
    {
        return new ioDev_mqtt();
    }
    class createReg {
    public:
        createReg() {
            mapDevCreateFunc["mqtt-server"] = createDev;
        };
    };
    createReg reg;
}

ioDev_mqtt::ioDev_mqtt()
{
    m_devType = "mqtt-server";
    m_level = "device";
    m_bThreadRunning = false;
	m_bConnected = false;
    m_bStop = false;
}

ioDev_mqtt::~ioDev_mqtt()
{

}

static void mqtt_fn(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_MQTT_OPEN) {
        LOG("MQTT connected");
        ioDev_mqtt* pDev = (ioDev_mqtt*)c->fn_data;
        pDev->m_bConnected = true;

        struct mg_mqtt_opts sub_opts;
        sub_opts.topic = mg_str("#");
        sub_opts.qos = 1;
        mg_mqtt_sub(c, &sub_opts);

        for (auto i : pDev->m_vecChildDev)
        {
            ioChannel* p = (ioChannel*)i;
            string strTopic = p->getIOAddrStr();

            struct mg_mqtt_opts sub_opts;
            sub_opts.topic = mg_str(strTopic.c_str());
            sub_opts.qos = QOS;
            mg_mqtt_sub(c, &sub_opts);
            LOG("[ioserver]Subscribing to topic %s\n using QoS%d\n\n", strTopic.c_str(), QOS);
        }
    }
    else if (ev == MG_EV_MQTT_MSG) {
        // 收到 MQTT 消息
        struct mg_mqtt_message* mm = (struct mg_mqtt_message*)ev_data;
        LOG(("Received message on topic [%s]: %s", mm->topic.ptr,mm->data.ptr));
    }
    else if (ev == MG_EV_CLOSE) {
        ioDev_mqtt* pDev = (ioDev_mqtt*)c->fn_data;
        if (pDev->m_bConnected) {
            pDev->m_bConnected = 0;
        }
    }
    else if (ev == MG_EV_ERROR) {
        // 错误事件
        MG_ERROR(("Error: %s", (char*)ev_data));
    }
}

void thread_mqtt_comm(void* p)
{ 
    ioDev_mqtt* pDev = (ioDev_mqtt*)p;
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
    opts.client_id = mg_str(CLIENTID);
    opts.topic = mg_str("#");

    // 建立 MQTT 连接
    string server = "mqtt://" + pDev->getIP() + ":" + to_string(pDev->getPort());
    c = mg_mqtt_connect(&mgr, server.c_str(), &opts, mqtt_fn,pDev);
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


bool ioDev_mqtt::run()
{
    thread t(thread_mqtt_comm, this);
    t.detach();
    return true;
}

void ioDev_mqtt::stop()
{
    m_bStop = true;
    while (m_bThreadRunning) {
        timeopt::sleepMilli(100);
    }
    return;
}

void ioDev_mqtt::onRecvMqttData(string topic, string data)
{
     string mqttData = "{"
         "\"topic\":\"" + topic + "\","
         "\"data\":\"" + data + "\""
         "}";
     
     setOnline();

     if (m_onRecvScript != "") {
         ScriptEngine se;

#ifdef TDS
         se.m_engineInitFuncList.push_back(initTdsFunc);
#endif

         json jMqttMsg = json::parse(mqttData);


         se.m_globalObj["MqttMsg"] = jMqttMsg;
         se.m_ioDevThis = this;

         SCRIPT_INFO si;
         scriptManager.getScript(m_onRecvScript, si);
         se.runScript(si, m_lastRunInfo_onRecv);

         if (se.m_sError != "") {
             string s = str::format("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", si.name.c_str(), se.m_sError.c_str(), getIOAddrStr().c_str());
             LOG(s);
         }
         else {

         }
     }
     return;
}
