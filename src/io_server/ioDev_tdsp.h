#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"


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

	void stop();
	bool handleAsynResp(json jResp);
	bool onRecvPkt(json jPkt);
	bool getCurrentVal();
	bool sendData(char* pData, int iLen);
	bool handleNotify(json& jNotify);
	int getRpcId();
	bool call(string method, json params,json& result,json& error,bool sync = true);

	json getAddr() override;
	void DoAcq();
	void DoCycleTask() override;

	map<int, TDSP_SYNC_INFO*> m_mapSyncRPCInfo;
	mutex m_csSyncRPCInfo;
	bool getResponse;
	int m_iRpcId;
};

extern void onRecvIQ60Pkt(char* pData, int iLen,std::shared_ptr<TDS_SESSION> pALC);