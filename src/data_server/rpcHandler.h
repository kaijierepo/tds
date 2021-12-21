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
	TEC_NO_STREAM_SRC = -40102,
	TEC_STREAM_ID_NOT_FOUND = -40103,

	//io 部分
	IO_DEV_NOT_FOUND = -41001,
};


class rpcHandler
{
public:

	rpcHandler();
	virtual ~rpcHandler();
	bool init();

	bool needLog(string method);

	bool handleDevRpcDispatch(string& strReq, json& jReq, std::shared_ptr<TDS_SESSION> pSession);

	bool isIoDevMethod(string method);

	//json rpc implementation
	void handleRpcCall(string strReq, string& strResp, char*& binResp, int& iBinLen, bool bNeedLog, std::shared_ptr<TDS_SESSION> pSession);
	bool handleMethodCall(string method, json params, RPC_RESP& rpcResult, std::shared_ptr<TDS_SESSION> pSession);

	string getSessionStatus();

	//tds data service function
	string rpc_input(json params, string& error);
	string rpc_getTopoList(json params, string& error, std::shared_ptr<TDS_SESSION> pSession = NULL);
	void rpc_getMoStatis(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_getMoStatusTable(json params, string& error, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_getMoStatus(json params, string& error, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_output(json params, string& error);
	string rpc_db_select(json params, string& error, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_getMpStatus(json params, string& error,std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_getUsers(json params, string& error);
	string rpc_getconf(json params, string& error);
	string rpc_setconf(json params, string& error);
	string rpc_getconffile(json params, string& error);
	string rpc_setconffile(json params, string& error);
	string rpc_heartbeat(json params, string& error, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_xiaot(json params, string& error);

	string rpc_logout(json params, string& error);

	string rpc_login(json params, string& error);

	////io service function
	string rpc_io_tree(json params, string& error);
	void rpc_getChanStatus(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession = NULL);
	void rpc_getDevStatus(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession = NULL);
	void rpc_getDevList(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession = NULL);
	void rpc_getIoDevStatis(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession = NULL);
	void rpc_getChanVal(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_io_scanChannel(json params, string& error, std::shared_ptr<TDS_SESSION> pSession = NULL);
	string rpc_setStream(json params, string& error);
	//serial function
	string rpc_openCom(json params, string& error);
	string rpc_getStreamInfo(json params, string& error);
	string rpc_com_list(json params, string& error);
	string rpc_closeCom(json params, string& error);

	//notification
	//orgSession不为null表示来自于tds客户端，为null表示来自tds服务
	void notify(string method, json params, std::shared_ptr<TDS_SESSION> orgSession = nullptr);
	void Notify(string strTag, string& szNotify);

	
	//rpc error
	string RPCError(int code,string msg);
	string parseDataSelector(json params,TIME_SELECTOR& timeSelector, TAG_SELECTOR& tagSelector);//return "" if success
	


    string  ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession);

	database* m_DB;

	//upper level tds
	tcpClt m_DataCenterClt;

	void saveDataFromUrl(string& strUrl, SYSTEMTIME& stTime, string& strTag, string suffix);

	fp_rpcHandler m_pluginHandler;
};
extern rpcHandler rpcSrv;
