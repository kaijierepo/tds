#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"

struct TDSP_SYNC_INFO {
	json jReq;
	json jResp;
	string strReq;
	semaphore respSignal;
	string strResp;
};


class ioDev_tdsp : public ioDev
{
public:
	ioDev_tdsp();
	~ioDev_tdsp();

	void syncDataToBindTag() override;

	void stop();
	void output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt, json& err, bool sync = true) override;
	void handleAlarmStatusData(json& alarmStatus);
	ioChannel* createChan(json& jVal, string addr);
	bool handle_AcqOrInput(json jResp);
	bool handleAsynResp(json jResp);
	bool onRecvPkt(json jPkt);
	bool getCurrentVal();
	bool sendData(unsigned char* pData, size_t iLen) override;
	bool handleNotify(json& jNotify);
	int getRpcId();
	void call(string method, json params, json sessionParams, json& result,json& error,  bool sync = true) override;
	bool startUpgradeProcess(string firmwareFileName);
	bool rpc_startUpgrade(string firmwareFileName,int pktLen, RPC_RESP& rpcResp);
	bool stopUpgrade() override;
	bool isConnected() override;

	json getAddr() override;
	void DoAcq();
	bool isViaAdaptor();
	void DoCycleTask() override;
	void onEvent_online() override;

	map<int, TDSP_SYNC_INFO*> m_mapSyncRPCInfo;
	mutex m_csSyncRPCInfo;
	bool getResponse;
	int m_iRpcId;
	mutex m_csRPCId;

	bool m_bAcqThreadRunning;

	string m_childTdsTag;
	int m_childTdsHttpPort;
	int m_childTdsHttpsPort;
};