/*
rpc handler
*/
#pragma once

#include "tdscore.h"
#include "db.h"
#include "tcpClt.h"
#include "ds.h"
#include "tdsSession.h"


enum RPC_ERROR {
	//json rpc 标准部分
	TDS_ERROR_CODE = -32603,

	//通用失败
	TEC_FAIL = -40000,
	//tds 部分
	TEC_TAG_NOT_EXIST = -40001,
	TEC_PARAM_MISSING = -40002,
	TEC_WRONG_PARAM_FMT = -40003,
	TEC_TIME_SELECTOR_FMT_ERROR = -40004,
	TEC_TAG_SELECTOR_FMT_ERROR = -40005,
	TEC_VAL_TYPE_ERROR = -40006,
	TEC_OUTPUT_EXECUTION_FAIL = -40007,

	//视频部分
	TEC_VIDEO_PARAM_NOT_VALID = -40101,

	//io 部分
	IO_DEV_NOT_FOUND = -41001,
};


class RPC_RESP {
public:
	char* binResp;
	int binLen;
	string textResp;

	RPC_RESP()
	{
		textResp = "";
		binLen = 0;
		binResp = NULL;
	}
};


class rpcHandler
{
public:

	rpcHandler();
	virtual ~rpcHandler();
	bool Init();

	bool needLog(string method);

	//json rpc implementation
	void handleRpcCall(string strJReq, RPC_RESP& resp, std::shared_ptr<TDS_SESSION>);
	string handleMethodCall(string method, json params, string& error, std::shared_ptr<TDS_SESSION> pSession);

	string getSessionStatus();

	//tds data service function
	string rpc_input(json params, string& error);
	string rpc_output(json params, string& error);
	string rpc_query(json params, string& error);
	string rpc_rt(json params, string& error);
	string rpc_getconf(json params, string& error);
	string rpc_setconf(json params, string& error);
	string rpc_getconffile(json params, string& error);
	string rpc_setconffile(json params, string& error);
	string rpc_heartbeat(json params, string& error, std::shared_ptr<TDS_SESSION> pSession);
	string rpc_xiaot(json params, string& error);

	////io service function
	string rpc_io_tree(json params, string& error);
	string rpc_io_scanChannel(json params, string& error, std::shared_ptr<TDS_SESSION> pSession);
	//serial function
	string rpc_openCom(json params, string& error);
	string rpc_getStreamInfo(json params, string& error);
	string rpc_com_list(json params, string& error);
	string rpc_closeCom(json params, string& error);

	//notification
	void notify(string method, json params);
	void Notify(string strTag, string& szNotify);

	//rpc error
	string RPCError(int code,string msg);
	string parseDataSelector(json params,TIME_SELECTOR& timeSelector, TAG_SELECTOR& tagSelector);//return "" if success
	


    string  ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession);

	database* m_DB;
	//关联的传输层服务器
	vector<CTLServer*> m_vecTLServer;
	//upper level tds
	tcpClt m_DataCenterClt;

	void saveDataFromUrl(string& strUrl, SYSTEMTIME& stTime, string& strTag, string suffix);

	fp_rpcHandler m_pluginHandler;
};
extern rpcHandler tdsSrv;
