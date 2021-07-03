/*
tds server
*/
#pragma once

#include "tdscore.h"
#include "db.h"
#include "tcpClt.h"
#include "ds.h"
#include "tdsSession.h"

enum TDS_ERROR_CODE{
	TEC_TAG_NOT_EXIST = -40001,
	TEC_PARAM_MISSING = -40002,
	TEC_WRONG_PARAM_FMT = -40003,
	TEC_TIME_SELECTOR_FMT_ERROR = -40004,
	TEC_TAG_SELECTOR_FMT_ERROR = - 40005,
	TEC_VAL_TYPE_ERROR = - 40006,
	TEC_OUTPUT_EXECUTION_FAIL = 40007,
};


class tdsServer : public CALServer,public ITcpClientCallBack
{
public:

	tdsServer();
	virtual ~tdsServer();
	bool Init();

	//json rpc implementation
	//tds core function
	void handleRpcCall(string strJReq, string& strJResp, std::shared_ptr<TDS_SESSION>);
	string rpc_input(json params);
	string rpc_output(json params);
	string rpc_query(json params);
	string rpc_rt(json params);
	string rpc_getconf(json params);
	string rpc_setconf(json params);
	string rpc_getconffile(json params);
	string rpc_setconffile(json params);
	string rpc_heartbeat(json params);
	string rpc_xiaot(json params);

	//serial function
	string rpc_openCom(json params);
	string rpc_com_list(json params);
	string rpc_closeCom(json params);

	//rpc error
	string RPCError(int code,string msg);
	string parseDataSelector(json params,TIME_SELECTOR& timeSelector, TAG_SELECTOR& tagSelector);//return "" if success
	

	void Notify(string strTag, string& szNotify);
    string  ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession);
	string handleMethodCall(string method, json params);
	database* m_DB;
	//关联的传输层服务器
	vector<CTLServer*> m_vecTLServer;
	//upper level tds
	CTCPClient m_DataCenterClt;
	virtual void ConnStatusChange(ConnInfo* connInfo, bool bIsConn);
	virtual void OnRecvData_TCPClient(char* pData, int iLen, ConnInfo* connInfo);

	
	vector<shared_ptr<TDS_SESSION>> GetAllSession();
	void saveDataFromUrl(string& strUrl, SYSTEMTIME& stTime, string& strTag, string suffix);
};
extern tdsServer tdsSrv;
