/*
rpc handler
*/
#pragma once

#include "tdscore.h"
#include "db.h"
#include "tcpClt.h"
#include "ds.h"
#include "tdsSession.h"


class rpcHandler
{
public:

	rpcHandler();
	virtual ~rpcHandler();
	bool init();

	bool needLog(string method);

	//透传到io设备的命令
	bool handleDevRpcDispatch(string& strReq, json& jReq, RPC_RESP& rpcResp,std::shared_ptr<TDS_SESSION> pSession);

	bool isGB2312Pkt(string& req);

	//json rpc implementation
	void handleRpcCall(string& strReq, string& strResp, char*& binResp, int& iBinLen, bool bNeedLog, std::shared_ptr<TDS_SESSION> pSession);
	bool handleMethodCall_OSFunc(string method, json& params, RPC_RESP& rpcResp);
	bool handleMethodCall_video(string method, json& params, RPC_RESP& rpcResp);
	bool handleMethodCall_db(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_debugFunc(string method, json& params, RPC_RESP& rpcResp);
	bool handleMethodCall_IoMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_audioPlayer(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_edgeDev(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_gamePad(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_MoMng(string method, json& params, RPC_RESP& rpcResp,RPC_SESSION session);
	bool handleMethodCall_alarmMng(string method, json& params, RPC_RESP& rpcResp,RPC_SESSION session);
	bool handleMethodCall_userMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall(string method, json params, RPC_RESP& rpcResult, RPC_SESSION session);


	//tds data service function
	void rpc_input(json params,RPC_RESP& resp, RPC_SESSION session);
	string rpc_getTopoList(json params, string& error,RPC_SESSION session);
	void   rpc_getMoStatis(json params, RPC_RESP& resp , RPC_SESSION session);
	string rpc_getMoOnlineStatus(json params, string& error);
	string rpc_getMoStatusTable(json params, string& error);
	string rpc_getMoStatus(json params, string& error,RPC_SESSION session);
	void rpc_output(json params, RPC_RESP& resp, RPC_SESSION session);
	string rpc_db_select(json params, string& error, RPC_SESSION session);
	string rpc_getMpStatus(json params, string& error, RPC_SESSION session);
	string rpc_getconf(json params, string& error);
	string rpc_setconf(json params, string& error);
	string rpc_getconffile(json params, string& error);
	string rpc_setconffile(json params, string& error);
	string rpc_heartbeat(json params, string& error, RPC_SESSION session);
	string rpc_xiaot(json params, string& error);
	string rpc_logout(json params, string& error);
	string rpc_login(json params, string& error);

	////io service function
	void rpc_getIOTree(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getChanStatus(json params, RPC_RESP& resp);
	void rpc_getDevStatus(json params, RPC_RESP& resp);
	void rpc_getDevList(json params, RPC_RESP& resp);
	void rpc_getIoDevStatis(json params, RPC_RESP& resp);
	void rpc_getChanVal(json params, RPC_RESP& resp);
	string rpc_io_scanChannel(json params, string& error);
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

	
	string parseDataSelector(json params,TIME_SELECTOR& timeSelector, TAG_SELECTOR& tagSelector);//return "" if success
    string  ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession);

	database* m_DB;

	//upper level tds
	tcpClt m_DataCenterClt;

	void saveDataFromUrl(string& strUrl, SYSTEMTIME& stTime, string& strTag, string suffix);

	fp_rpcHandler m_pluginHandler;
};
extern rpcHandler rpcSrv;
