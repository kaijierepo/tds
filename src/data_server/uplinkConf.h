#pragma once
#include <string>
#include <vector>
#include <memory>

// 上行配置基类
struct UPLINK_CONF {
	std::string ip;
	int port = 0;
	std::string desc;
	bool enabled = true;

	virtual ~UPLINK_CONF() = default;
	virtual const char* protoName() const = 0;
};

// MQTT 上行配置
struct UPLINK_CONF_MQTT : public UPLINK_CONF {
	std::string user;
	std::string pwd;
	std::string clientID;
	int qos = 0;
	std::string subTopics;
	std::string pubTopics;
	std::string format = "default";
	std::string connectScript;
	std::string recvScript;
	std::string sendScript;
	std::string cycleScript;
	int intervel = 0;

	const char* protoName() const override { return "mqtt"; }
};

// TDSP 上行配置（仅基类字段）
struct UPLINK_CONF_TDSP : public UPLINK_CONF {
	const char* protoName() const override { return "tdsp"; }
};

// OPC UA 上行配置（仅基类字段）
struct UPLINK_CONF_OPCUA : public UPLINK_CONF {
	const char* protoName() const override { return "opcua"; }
};
