#include "pch.h"
#include "ioDev_mqttBroker.h"
#include "logger.h"
#include "ioChan.h"

#ifdef ENABLE_MQTT
#pragma comment(lib, "mqtt.lib")
#endif

#define EXIT_FAILURE -1
#define CLIENTID    "tds ioserver"
#define QOS         1
#define TIMEOUT     10000L

#ifdef ENABLE_MQTT
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void* context, MQTTClient_deliveryToken dt)
{
    LOG("Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

int onRecvMqttMsg(void* context, char* topicName, int topicLen, MQTTClient_message* message)
{
    ioDev_mqttBroker* p = (ioDev_mqttBroker*) context;
    LOG("Message arrived\n");
    LOG("     topic: %s\n", topicName);
    LOG("   message: %.*s\n", message->payloadlen, (char*)message->payload);
    string msg = (char*)message->payload;
    ioChannel* pDc = p->GetDataChannel(topicName);

    json j = atof(msg.c_str());
    if(pDc)
     pDc->inputVal(j);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);

    return 1;
}

void connlost(void* context, char* cause)
{
    LOG("\nConnection lost\n");
    LOG("     cause: %s\n", cause);
}
#endif

ioDev_mqttBroker::ioDev_mqttBroker()
{
}

ioDev_mqttBroker::~ioDev_mqttBroker()
{
#ifdef ENABLE_MQTT
    int rc = 0;
    for (auto i : m_vecChild)
    {
        ioChannel* p = (ioChannel*)i;
        string strTopic = p->m_addr;
        if ((rc = MQTTClient_unsubscribe(m_mqttClt, strTopic.c_str())) != MQTTCLIENT_SUCCESS)
        {
            LOG("Failed to unsubscribe, return code %d\n", rc);
        }
    }

    if ( (rc=MQTTClient_disconnect(m_mqttClt, 10000)) != MQTTCLIENT_SUCCESS)
    {
        LOG("Failed to disconnect, return code %d\n", rc);
    }

    MQTTClient_destroy(&m_mqttClt);
#endif
}


bool ioDev_mqttBroker::run()
{
#ifdef ENABLE_MQTT
    MQTTClient_global_init(NULL);
    int rc;
    string serverAddr = "tcp://" + m_addr;
    if ((rc = MQTTClient_create(&m_mqttClt, serverAddr.c_str(), CLIENTID,
        MQTTCLIENT_PERSISTENCE_NONE, NULL)) != MQTTCLIENT_SUCCESS)
    {
        LOG("Failed to create client, return code %d\n", rc);
        return false;
    }

    if ((rc = MQTTClient_setCallbacks(m_mqttClt, this, connlost, onRecvMqttMsg, delivered)) != MQTTCLIENT_SUCCESS)
    {
        LOG("Failed to set callbacks, return code %d\n", rc);
        return false;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    if ((rc = MQTTClient_connect(m_mqttClt, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        LOG("Failed to connect, return code %d\n", rc);
        return false;
    }


    for (auto i : m_vecChild)
    {
        ioChannel* p = (ioChannel*)i;
        string strTopic = p->m_addr;
        LOG("[ioserver]Subscribing to topic %s\n using QoS%d\n\n", strTopic.c_str(), QOS);
        if ((rc = MQTTClient_subscribe(m_mqttClt, strTopic.c_str(), QOS)) != MQTTCLIENT_SUCCESS)
        {
            LOG("Failed to subscribe, return code %d\n", rc);
        }
    }

    for (auto i : m_mapBatchDataLink)
    {
        string strTopic = i.first;
        LOG("[ioserver]Subscribing to topic %s\n using QoS%d\n\n", strTopic.c_str(), QOS);
        if ((rc = MQTTClient_subscribe(m_mqttClt, strTopic.c_str(), QOS)) != MQTTCLIENT_SUCCESS)
        {
            LOG("Failed to subscribe, return code %d\n", rc);
        }
    }
#endif
    return true;
}
