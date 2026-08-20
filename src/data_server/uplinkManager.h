#ifndef TDS_DATA_SERVER_UPLINKMANAGER_H
#define TDS_DATA_SERVER_UPLINKMANAGER_H

/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu
*/

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "common/yyjson.h"
#include "uplinkConf.h"
#include "tdsRPC.h"

class UplinkManager {
public:
	UplinkManager() = default;

	bool init(const std::string& confPath);
	void run();

	bool handleRpc(std::string method, yyjson_val* params,
		RPC_RESP& rpcResp, RPC_SESSION& session);

private:
	void rpc_getUplink(RPC_RESP& rpcResp);
	void rpc_setUplink(yyjson_val* params, RPC_RESP& rpcResp);
	void rpc_enableUplink(yyjson_val* params, RPC_RESP& rpcResp);
	void rpc_disableUplink(yyjson_val* params, RPC_RESP& rpcResp);

	bool loadFromDisk();
	bool saveToDisk();
	void setUplinkEnabled(const std::string& ip, int port, bool enabled);
	void collectMqttStatus(yyjson_mut_doc* doc, yyjson_mut_val* arr);
	void collectTdspStatus(yyjson_mut_doc* doc, yyjson_mut_val* arr);
	void notifyMqttConfigChanged();

	void startTdspUplink();
	void startMqttUplink();

	// JSON ↔ conf 互转
	static std::shared_ptr<UPLINK_CONF> parseItem(yyjson_val* item);
	static yyjson_mut_val* itemToJson(yyjson_mut_doc* doc, const UPLINK_CONF& c);

	std::vector<std::shared_ptr<UPLINK_CONF>> m_uplinks;
	std::string m_confPath;
	std::mutex m_mutex;
};

extern UplinkManager uplinkMnger;

#endif /* TDS_DATA_SERVER_UPLINKMANAGER_H */
