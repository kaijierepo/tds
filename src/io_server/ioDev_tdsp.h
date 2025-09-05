#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"

struct TDSP_SYNC_INFO {
	string strReq;
	semaphore respSignal;
	string strResp;
};

class ioDev_tdsp : public ioDev
{
public:
	ioDev_tdsp();
	~ioDev_tdsp();

	void syncDataToMasterTds(yyjson_val* val,yyjson_doc* doc);
	bool isTdsp() override { return true; }
	void stop() override;
	void output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt, json& err, bool sync = true) override;
	void handleAlarmStatusData(yyjson_val* alarmStatus);
	ioChannel* createChan(yyjson_val* jVal, string addr);
	bool handle_AcqOrInput(yyjson_val* chanData, yyjson_doc* doc);
	bool handleDevRequest(yyjson_val* jRequest, yyjson_doc* doc);
	bool handleSyncResp(yyjson_val* jResp, yyjson_val* yyv_id, yyjson_doc* doc);
	bool handleAsynResp(yyjson_val* jResp, yyjson_doc* doc);
	//bool onRecvPkt(json jPkt);
	bool onRecvPkt(yyjson_val* jPkt, yyjson_doc* doc) override;
	virtual bool onRecvData(unsigned char* pData, size_t iLen) override;
	bool getCurrentVal();
	bool sendData(unsigned char* pData, size_t iLen) override;
	bool handleNotify(yyjson_val* jNotify, yyjson_doc* doc);
	bool handleNotify(json& jNotify);
	int getRpcId();
	bool isSingleTransaction();
	void call(string method, json params, json sessionParams, json& result,json& error,  bool sync = true) override;
	bool startUpgradeProcess(string firmwareFileName);
	bool rpc_startUpgrade(string firmwareFileName,int pktLen, RPC_RESP& rpcResp);
	bool stopUpgrade() override;
	bool isConnected() override;

	json getAddr() override;
	void DoAcq();
	void DoCycleTask() override;
	void DoCycleTaskSync() override;
	void onEvent_online() override;

	virtual void onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo) override;

	void translateToDevPkt(string& tdspPkt,vector<unsigned char>& devPkt);
	void translateToTdspPkt(char* devPkt, int len, string& tdspPkt);

	map<int, TDSP_SYNC_INFO*> m_mapSyncRPCInfo;
	mutex m_csSyncRPCInfo;
	bool getResponse;
	int m_iRpcId;
	mutex m_csRPCId;

	string m_childTdsTag;
	int m_childTdsHttpPort;
	int m_childTdsHttpsPort;
	udpServer translatorClient;

};