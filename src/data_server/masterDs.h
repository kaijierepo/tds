/*
masterDs
接受childDs的主动连接
主动同步childDs的配置
主动同步childDs的数据

*/
#pragma once
#include "tdscore.h"
#include "tcpSrv.h"
#include "tdsSession.h"


struct RPC_SYNC_INFO {
	json jReq;
	json jResp;
	string strReq;
	semaphore respSignal;
	string strResp;
};



class MasterDs : public ITcpServerCallBack
{
public:
	void statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess);
	void OnRecvData(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> childSession);
	bool handleAsynResp(json jResp, std::shared_ptr<TDS_SESSION> childSession);
	bool handleNotify(json jResp, std::shared_ptr<TDS_SESSION> childSession);
	void onRecvPkt(json& pkt, std::shared_ptr<TDS_SESSION> childSession);

	bool doChildTdsTransaction(string childTdsTag, json& req, RPC_RESP& rpcResp, bool sync);
	bool callChildTds(string childTds,string method, json params, json& rlt, json& err, bool sync = true);

	bool rpc_childTdsDispatch(json& req, RPC_RESP& rpcResp, bool sync = true);

public:
	bool run();
	void stop();

	void workingProc();

	MasterDs();
	virtual ~MasterDs();

	std::shared_ptr<TDS_SESSION> getSessionByTag(string tag);


	map<int, RPC_SYNC_INFO*> m_mapSyncRPCInfo;
	mutex m_csSyncRPCInfo;
	bool getResponse;

	map<void*, std::shared_ptr<TDS_SESSION>> m_vecChildTds;
	mutex m_mutexChildTdsList;

	tcpSrv* m_tcpSrv; 
	int m_masterTdsPort;
	int m_rpcId;
};

extern MasterDs* pMasterDs;