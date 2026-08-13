#include "uplinkManager.h"
#include "uplink_mqtt.h"
#include "tSockSrv.h"
#include "common.h"
#include "logger.h"
#include "tds.h"
#include <cstring>
#include <cstdio>

UplinkManager uplinkMnger;

// ====== JSON ↔ UPLINK_CONF 互转 ======

std::shared_ptr<UPLINK_CONF> UplinkManager::parseItem(yyjson_val* item) {
	yyjson_val* yy_proto = yyjson_obj_get(item, "proto");
	if (!yy_proto) return nullptr;

	std::string proto = yyjson_get_str(yy_proto);

	// 基类字段
	std::string ip, desc;
	int port = 0;
	bool enabled = true;

	yyjson_val* yy_ip = yyjson_obj_get(item, "ip");
	if (yy_ip) ip = yyjson_get_str(yy_ip);
	yyjson_val* yy_port = yyjson_obj_get(item, "port");
	if (yy_port) {
		if (yyjson_is_int(yy_port))
			port = yyjson_get_int(yy_port);
		else if (yyjson_is_str(yy_port))
			port = atoi(yyjson_get_str(yy_port));
	}
	yyjson_val* yy_desc = yyjson_obj_get(item, "desc");
	if (yy_desc) desc = yyjson_get_str(yy_desc);
	yyjson_val* yy_enabled = yyjson_obj_get(item, "enabled");
	if (yy_enabled && yyjson_is_bool(yy_enabled))
		enabled = yyjson_get_bool(yy_enabled);

	if (proto == "mqtt") {
		std::shared_ptr<UPLINK_CONF_MQTT> conf = std::make_shared<UPLINK_CONF_MQTT>();
		conf->ip = ip;
		conf->port = port;
		conf->desc = desc;
		conf->enabled = enabled;

		yyjson_val* v;
		if ((v = yyjson_obj_get(item, "user"))) conf->user = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "pwd"))) conf->pwd = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "clientID"))) conf->clientID = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "qos"))) conf->qos = yyjson_get_int(v);
		if ((v = yyjson_obj_get(item, "subTopics"))) conf->subTopics = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "pubTopics"))) conf->pubTopics = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "format"))) conf->format = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "connectScript"))) conf->connectScript = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "recvScript"))) conf->recvScript = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "sendScript"))) conf->sendScript = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "cycleScript"))) conf->cycleScript = yyjson_get_str(v);
		if ((v = yyjson_obj_get(item, "intervel"))) {
			if (yyjson_is_int(v))
				conf->intervel = yyjson_get_int(v);
			else if (yyjson_is_str(v))
				conf->intervel = atoi(yyjson_get_str(v));
		}
		return conf;
	}
	else if (proto == "tdsp") {
		std::shared_ptr<UPLINK_CONF_TDSP> conf = std::make_shared<UPLINK_CONF_TDSP>();
		conf->ip = ip;
		conf->port = port;
		conf->desc = desc;
		conf->enabled = enabled;
		return conf;
	}
	else if (proto == "opcua") {
		std::shared_ptr<UPLINK_CONF_OPCUA> conf = std::make_shared<UPLINK_CONF_OPCUA>();
		conf->ip = ip;
		conf->port = port;
		conf->desc = desc;
		conf->enabled = enabled;
		return conf;
	}

	return nullptr;
}

yyjson_mut_val* UplinkManager::itemToJson(yyjson_mut_doc* doc, const UPLINK_CONF& c) {
	yyjson_mut_val* obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, obj, "proto", c.protoName());
	yyjson_mut_obj_add_str(doc, obj, "ip", c.ip.c_str());
	yyjson_mut_obj_add_int(doc, obj, "port", c.port);
	if (!c.desc.empty())
		yyjson_mut_obj_add_str(doc, obj, "desc", c.desc.c_str());
	yyjson_mut_obj_add_bool(doc, obj, "enabled", c.enabled);

	const char* pn = c.protoName();
	if (strcmp(pn, "mqtt") == 0) {
		const UPLINK_CONF_MQTT& m = static_cast<const UPLINK_CONF_MQTT&>(c);
		if (!m.user.empty()) yyjson_mut_obj_add_str(doc, obj, "user", m.user.c_str());
		if (!m.pwd.empty()) yyjson_mut_obj_add_str(doc, obj, "pwd", m.pwd.c_str());
		if (!m.clientID.empty()) yyjson_mut_obj_add_str(doc, obj, "clientID", m.clientID.c_str());
		if (m.qos != 0) yyjson_mut_obj_add_int(doc, obj, "qos", m.qos);
		if (!m.subTopics.empty()) yyjson_mut_obj_add_str(doc, obj, "subTopics", m.subTopics.c_str());
		if (!m.pubTopics.empty()) yyjson_mut_obj_add_str(doc, obj, "pubTopics", m.pubTopics.c_str());
		if (!m.format.empty() && m.format != "default")
			yyjson_mut_obj_add_str(doc, obj, "format", m.format.c_str());
		if (!m.connectScript.empty()) yyjson_mut_obj_add_str(doc, obj, "connectScript", m.connectScript.c_str());
		if (!m.recvScript.empty()) yyjson_mut_obj_add_str(doc, obj, "recvScript", m.recvScript.c_str());
		if (!m.sendScript.empty()) yyjson_mut_obj_add_str(doc, obj, "sendScript", m.sendScript.c_str());
		if (!m.cycleScript.empty()) yyjson_mut_obj_add_str(doc, obj, "cycleScript", m.cycleScript.c_str());
		if (m.intervel != 0) yyjson_mut_obj_add_int(doc, obj, "intervel", m.intervel);
	}
	return obj;
}

// ====== 初始化 & 磁盘读写 ======

bool UplinkManager::init(const std::string& confPath) {
	m_confPath = confPath;
	return loadFromDisk();
}

bool UplinkManager::loadFromDisk() {
	m_uplinks.clear();

	std::string p = m_confPath + "/uplink.json";
	std::string s;
	if (!fs::readFile(p, s)) {
		LOG("[Uplink] %s not found, start with empty config", p.c_str());
		return true;
	}

	yyjson_doc* doc = yyjson_read(s.c_str(), s.size(), 0);
	if (!doc) {
		LOG("[Uplink] failed to parse %s, start with empty config", p.c_str());
		return true;
	}

	yyjson_val* root = yyjson_doc_get_root(doc);
	if (!yyjson_is_arr(root)) {
		yyjson_doc_free(doc);
		return true;
	}

	size_t max, idx;
	yyjson_val* item;
	yyjson_arr_foreach(root, idx, max, item) {
		std::shared_ptr<UPLINK_CONF> conf = parseItem(item);
		if (conf) m_uplinks.push_back(conf);
	}

	yyjson_doc_free(doc);
	LOG("[Uplink] loaded %s, %zu items", p.c_str(), m_uplinks.size());
	return true;
}

bool UplinkManager::saveToDisk() {
	yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* root = yyjson_mut_arr(doc);

	for (size_t i = 0; i < m_uplinks.size(); i++) {
		yyjson_mut_val* item = itemToJson(doc, *m_uplinks[i]);
		yyjson_mut_arr_append(root, item);
	}

	yyjson_mut_doc_set_root(doc, root);
	char* jsonStr = yyjson_mut_write(doc, 0, NULL);
	if (!jsonStr) {
		yyjson_mut_doc_free(doc);
		return false;
	}

	std::string p = m_confPath + "/uplink.json";
	bool ok = fs::writeFile(p, jsonStr,strlen(jsonStr));
	if (ok) LOG("[Uplink] saved %s", p.c_str());
	else     LOG("[Uplink] failed to save %s", p.c_str());

	free(jsonStr);
	yyjson_mut_doc_free(doc);
	return ok;
}

// ====== enabled 标记修改 ======

void UplinkManager::setUplinkEnabled(const std::string& targetIp, int targetPort, bool enabled) {
	std::lock_guard<std::mutex> lk(m_mutex);

	for (size_t i = 0; i < m_uplinks.size(); i++) {
		if (m_uplinks[i]->ip == targetIp && m_uplinks[i]->port == targetPort) {
			m_uplinks[i]->enabled = enabled;
			break;
		}
	}

	saveToDisk();
	LOG("[Uplink] set %s:%d enabled=%d", targetIp.c_str(), targetPort, enabled ? 1 : 0);
}

// ====== 运行入口 ======

void UplinkManager::run() {
	LOG("[Uplink] starting uplink services...");
	startTdspUplink();
	startMqttUplink();
}

void UplinkManager::startTdspUplink() {
	std::vector<std::string> tdspAddrs;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		for (size_t i = 0; i < m_uplinks.size(); i++) {
			if (!m_uplinks[i]->enabled) continue;
			if (strcmp(m_uplinks[i]->protoName(), "tdsp") != 0) continue;

			char addr[128];
			snprintf(addr, sizeof(addr), "%s:%d",
				m_uplinks[i]->ip.c_str(), m_uplinks[i]->port);
			tdspAddrs.push_back(addr);
		}
	}

	if (!tdspAddrs.empty()) {
		std::string childTdsIP = tds->conf->getStr("childTdsIP", "");
		sockSrv.addTdspUplinkClients(tdspAddrs, childTdsIP);
	}
}

void UplinkManager::startMqttUplink() {
	std::vector<UPLINK_CONF_MQTT> mqttConfs;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		for (size_t i = 0; i < m_uplinks.size(); i++) {
			if (!m_uplinks[i]->enabled) continue;
			if (strcmp(m_uplinks[i]->protoName(), "mqtt") != 0) continue;
			UPLINK_CONF_MQTT* m = dynamic_cast<UPLINK_CONF_MQTT*>(m_uplinks[i].get());
			if (m) mqttConfs.push_back(*m);
		}
	}

	mqttUplink.init(mqttConfs);
	mqttUplink.run();
}

void UplinkManager::notifyMqttConfigChanged() {
	std::vector<UPLINK_CONF_MQTT> mqttConfs;

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		for (size_t i = 0; i < m_uplinks.size(); i++) {
			if (!m_uplinks[i]->enabled) continue;
			if (strcmp(m_uplinks[i]->protoName(), "mqtt") != 0) continue;
			UPLINK_CONF_MQTT* m = dynamic_cast<UPLINK_CONF_MQTT*>(m_uplinks[i].get());
			if (m) mqttConfs.push_back(*m);
		}
	}

	mqttUplink.reload(mqttConfs);
}

// ====== MQTT 实时状态注入 ======

void UplinkManager::collectMqttStatus(yyjson_mut_doc* doc, yyjson_mut_val* arr) {
	if (!arr) return;

	size_t max, idx;
	yyjson_mut_val* item;
	yyjson_mut_arr_foreach(arr, idx, max, item) {
		yyjson_mut_val* yy_conf = yyjson_mut_obj_get(item, "conf");
		if (!yy_conf) continue;

		yyjson_mut_val* yy_proto = yyjson_mut_obj_get(yy_conf, "proto");
		if (!yy_proto) continue;
		const char* proto = yyjson_mut_get_str(yy_proto);
		if (!proto || strcmp(proto, "mqtt") != 0) continue;

		yyjson_mut_val* yy_ip = yyjson_mut_obj_get(yy_conf, "ip");
		yyjson_mut_val* yy_port = yyjson_mut_obj_get(yy_conf, "port");
		if (!yy_ip || !yy_port) continue;

		const char* ip = yyjson_mut_get_str(yy_ip);
		int port = yyjson_mut_is_int(yy_port) ? yyjson_mut_get_int(yy_port) : 0;
		if (!ip) continue;

		for (size_t j = 0; j < mqttUplink.m_mqttClts.size(); j++) {
			MqttClt* clt = mqttUplink.m_mqttClts[j];
			if (!clt) continue;
			if (clt->m_conf.ip == ip && clt->m_conf.port == port) {
				yyjson_mut_val* yy_status = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_bool(doc, yy_status, "connected",
					clt->m_bConnected);
				yyjson_mut_obj_add_bool(doc, yy_status, "connecting",
					clt->m_bConnectting);
				yyjson_mut_obj_add_str(doc, yy_status, "lastConnectTime",
					clt->m_lastConnectTime.toStr().c_str());
			yyjson_mut_obj_add_str(doc, yy_status, "clientID",
				clt->m_conf.clientID.c_str());
			yyjson_mut_obj_add_real(doc, yy_status, "sendBytes",
				(double)clt->m_sendBytes / 1024.0);
			yyjson_mut_obj_add_real(doc, yy_status, "recvBytes",
				(double)clt->m_recvBytes / 1024.0);
			yyjson_mut_obj_add_str(doc, yy_status, "lastActiveTime",
				clt->m_lastActiveTime.c_str());

				yyjson_mut_obj_add_val(doc, item, "status", yy_status);
				break;
			}
		}
	}
}

// ====== TDSP 状态注入 ======

void UplinkManager::collectTdspStatus(yyjson_mut_doc* doc, yyjson_mut_val* arr) {
	if (!arr) return;

	size_t max, idx;
	yyjson_mut_val* item;
	yyjson_mut_arr_foreach(arr, idx, max, item) {
		yyjson_mut_val* yy_conf = yyjson_mut_obj_get(item, "conf");
		if (!yy_conf) continue;

		yyjson_mut_val* yy_proto = yyjson_mut_obj_get(yy_conf, "proto");
		if (!yy_proto) continue;
		const char* proto = yyjson_mut_get_str(yy_proto);
		if (!proto || strcmp(proto, "tdsp") != 0) continue;

		yyjson_mut_val* yy_ip = yyjson_mut_obj_get(yy_conf, "ip");
		yyjson_mut_val* yy_port = yyjson_mut_obj_get(yy_conf, "port");
		if (!yy_ip || !yy_port) continue;

		const char* ip = yyjson_mut_get_str(yy_ip);
		int port = yyjson_mut_is_int(yy_port) ? yyjson_mut_get_int(yy_port) : 0;
		if (!ip) continue;

		for (size_t j = 0; j < sockSrv.m_tcpClt_ParentTds.size(); j++) {
			tcpClt* pClt = sockSrv.m_tcpClt_ParentTds[j];
			if (!pClt) continue;
			if (pClt->m_remoteIP == ip && pClt->m_remotePort == port) {
				yyjson_mut_val* yy_status = yyjson_mut_obj(doc);
				yyjson_mut_obj_add_bool(doc, yy_status, "connected",
					pClt->IsConnect());
			yyjson_mut_obj_add_real(doc, yy_status, "sendBytes",
				(double)pClt->m_session.iSendSucCount / 1024.0);
			yyjson_mut_obj_add_real(doc, yy_status, "recvBytes",
				(double)pClt->m_session.iRecvCount / 1024.0);
			yyjson_mut_obj_add_str(doc, yy_status, "lastActiveTime",
				pClt->m_session.stLastActive.c_str());
			yyjson_mut_obj_add_str(doc, yy_status, "lastConnectTime",
				pClt->lastConnTime.c_str());

			yyjson_mut_obj_add_val(doc, item, "status", yy_status);
			break;
			}
		}
	}
}

// ====== RPC 处理 ======

void UplinkManager::rpc_getUplink(RPC_RESP& rpcResp) {
	yyjson_mut_doc* resultDoc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* resultArr = yyjson_mut_arr(resultDoc);

	{
		std::lock_guard<std::mutex> lk(m_mutex);
		for (size_t i = 0; i < m_uplinks.size(); i++) {
			yyjson_mut_val* wrap = yyjson_mut_obj(resultDoc);

			yyjson_mut_val* confVal = itemToJson(resultDoc, *m_uplinks[i]);
			yyjson_mut_obj_add_val(resultDoc, wrap, "conf", confVal);

			yyjson_mut_obj_add_val(resultDoc, wrap, "status",
				yyjson_mut_obj(resultDoc));

			yyjson_mut_arr_append(resultArr, wrap);
		}
	}

	// 注入 MQTT 实时状态
	collectMqttStatus(resultDoc, resultArr);
	// 注入 TDSP 实时状态
	collectTdspStatus(resultDoc, resultArr);

	char* resultStr = yyjson_mut_val_write(resultArr, 0, NULL);
	if (resultStr) {
		rpcResp.result = resultStr;
		free(resultStr);
	} else {
		rpcResp.result = "[]";
	}

	yyjson_mut_doc_free(resultDoc);
}

void UplinkManager::rpc_setUplink(yyjson_val* params, RPC_RESP& rpcResp) {
	if (!yyjson_is_obj(params)) {
		rpcResp.result = "\"params must be an object with 'config' field\"";
		return;
	}

	yyjson_val* yy_config = yyjson_obj_get(params, "config");
	if (!yy_config || !yyjson_is_arr(yy_config)) {
		rpcResp.result = "\"missing or invalid 'config' field\"";
		return;
	}

	// 解析新配置
	std::vector<std::shared_ptr<UPLINK_CONF>> newUplinks;
	size_t max, idx;
	yyjson_val* item;
	yyjson_arr_foreach(yy_config, idx, max, item) {
		std::shared_ptr<UPLINK_CONF> conf = parseItem(item);
		if (conf) newUplinks.push_back(conf);
	}

	// 替换并持久化
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_uplinks = newUplinks;
	}

	if (!saveToDisk()) {
		rpcResp.result = "\"failed to save config to disk\"";
		return;
	}

	LOG("[Uplink] config updated via setUplink, notifying...");
	notifyMqttConfigChanged();

	rpcResp.result = "\"ok\"";
}

void UplinkManager::rpc_enableUplink(yyjson_val* params, RPC_RESP& rpcResp) {
	if (!yyjson_is_obj(params)) {
		rpcResp.result = "\"params must be an object with proto, ip, port\"";
		return;
	}

	yyjson_val* yy_proto = yyjson_obj_get(params, "proto");
	yyjson_val* yy_ip = yyjson_obj_get(params, "ip");
	yyjson_val* yy_port = yyjson_obj_get(params, "port");

	if (!yy_proto || !yy_ip || !yy_port) {
		rpcResp.result = "\"missing proto, ip or port\"";
		return;
	}

	std::string proto = yyjson_get_str(yy_proto);
	std::string targetIp = yyjson_get_str(yy_ip);
	int targetPort = yyjson_is_int(yy_port) ? yyjson_get_int(yy_port)
		: atoi(yyjson_get_str(yy_port));

	// 对 mqtt 启用连接
	if (proto == "mqtt") {
		UPLINK_CONF_MQTT conf;
		bool found = false;

		{
			std::lock_guard<std::mutex> lk(m_mutex);
			for (size_t i = 0; i < m_uplinks.size(); i++) {
				if (strcmp(m_uplinks[i]->protoName(), "mqtt") != 0) continue;
				if (m_uplinks[i]->ip == targetIp && m_uplinks[i]->port == targetPort) {
					UPLINK_CONF_MQTT* m = dynamic_cast<UPLINK_CONF_MQTT*>(m_uplinks[i].get());
					if (m) { conf = *m; found = true; }
					break;
				}
			}
		}

		if (!found) {
			rpcResp.result = "\"config not found for specified address\"";
			return;
		}

		mqttUplink.enableConnection(conf);
	}
	// 对 tdsp 启用连接
	else if (proto == "tdsp") {
		std::string childTdsIP = tds->conf->getStr("childTdsIP", "");

		{
			std::lock_guard<std::mutex> lk(m_mutex);
			bool found = false;
			for (size_t i = 0; i < m_uplinks.size(); i++) {
				if (strcmp(m_uplinks[i]->protoName(), "tdsp") != 0) continue;
				if (m_uplinks[i]->ip == targetIp && m_uplinks[i]->port == targetPort) {
					found = true;
					break;
				}
			}
			if (!found) {
				rpcResp.result = "\"config not found for specified address\"";
				return;
			}
		}

		tcpClt* pTcpClt = new tcpClt();
		std::string addr = targetIp + ":" + std::to_string(targetPort);
		pTcpClt->run(&sockSrv, addr, childTdsIP);
		sockSrv.m_tcpClt_ParentTds.push_back(pTcpClt);
	}

	setUplinkEnabled(targetIp, targetPort, true);
	LOG("[Uplink] enableUplink %s:%d", targetIp.c_str(), targetPort);
	rpcResp.result = "\"ok\"";
}

void UplinkManager::rpc_disableUplink(yyjson_val* params, RPC_RESP& rpcResp) {
	if (!yyjson_is_obj(params)) {
		rpcResp.result = "\"params must be an object with proto, ip, port\"";
		return;
	}

	yyjson_val* yy_proto = yyjson_obj_get(params, "proto");
	yyjson_val* yy_ip = yyjson_obj_get(params, "ip");
	yyjson_val* yy_port = yyjson_obj_get(params, "port");

	if (!yy_proto || !yy_ip || !yy_port) {
		rpcResp.result = "\"missing proto, ip or port\"";
		return;
	}

	std::string proto = yyjson_get_str(yy_proto);
	std::string targetIp = yyjson_get_str(yy_ip);
	int targetPort = yyjson_is_int(yy_port) ? yyjson_get_int(yy_port)
		: atoi(yyjson_get_str(yy_port));

	if (proto == "mqtt") {
		mqttUplink.disableConnection(targetIp, targetPort);
	}
	else if (proto == "tdsp") {
		std::vector<tcpClt*>& v = sockSrv.m_tcpClt_ParentTds;
		for (size_t i = 0; i < v.size(); i++) {
			if (v[i]->m_remoteIP == targetIp && v[i]->m_remotePort == targetPort) {
				v[i]->stop();
				delete v[i];
				v.erase(v.begin() + i);
				break;
			}
		}
	}
	setUplinkEnabled(targetIp, targetPort, false);

	LOG("[Uplink] disableUplink %s:%d", targetIp.c_str(), targetPort);
	rpcResp.result = "\"ok\"";
}

bool UplinkManager::handleRpc(std::string method, yyjson_val* params,
	RPC_RESP& rpcResp, RPC_SESSION& session) {
	if (method == "getUplink") {
		rpc_getUplink(rpcResp);
		return true;
	}
	if (method == "setUplink") {
		rpc_setUplink(params, rpcResp);
		return true;
	}
	if (method == "enableUplink") {
		rpc_enableUplink(params, rpcResp);
		return true;
	}
	if (method == "disableUplink") {
		rpc_disableUplink(params, rpcResp);
		return true;
	}
	return false;
}
