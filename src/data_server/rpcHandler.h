/*
rpc handler
*/
#pragma once
#include "db.h"
#include "tdsSession.h"
#include "obj.h"

struct Mo_Attr_Params {
	json renameMap;
	TAG_SELECTOR tagSel;
	json jAttrSel;
	TAG_SELECTOR attrSel;
	string valFmt;
	bool bSelAttr;
	string columeLabel; //默认使用位号作为列名
	string moType;
	string rootTag;
	Mo_Attr_Params() {
		bSelAttr = false;
		columeLabel = "tag";
		valFmt = "val";
		moType = "obj";
	}

};

//一些设计考虑
//参数使用RPC_RESP而没有使用json对象，主要为了考虑和外部集成，外部不需要json.hpp
//目前看来外部集成需求很少，未来改成直接使用json对象应该更加方便
class rpcHandler
{
public:

	rpcHandler();
	virtual ~rpcHandler();
	bool init();

	bool needLog(string method);


	//rpc路由的命令  可以路由到tdsp设备，或者childTds
	bool handleRpcRoute(string& strReq, json& jReq, RPC_RESP& rpcResp,std::shared_ptr<TDS_SESSION> pSession);
	void logRPCRoute(string method, json& params, RPC_SESSION& session);

	bool isGB2312Pkt(string& req);

	//json rpc implementation
	void handleRpcCallAsyn(string& strReq,std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl = true, bool bEdgeDevMode = false);
	void handleRpcCall(string& strReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession,bool bAccessCtrl = true,bool bEdgeDevMode = false);
	bool handleMethodCall_OSFunc(string method, json& params, RPC_RESP& rpcResp);
	bool handleMethodCall_ptz_cloud(string method, MP* pmp, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_ptz_ioDev(string method, string tag, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_video(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_db(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_debugFunc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_IoMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_audioPlayer(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_edgeDev(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_gamePad(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_MoMng(string method, json& params, RPC_RESP& rpcResp,RPC_SESSION session);
	bool handleMethodCall_alarmMng(string method, json& params, RPC_RESP& rpcResp,RPC_SESSION session);
	bool handleMethodCall_userMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall_unclassified(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session);
	bool handleMethodCall(string method, json params, RPC_RESP& rpcResult, RPC_SESSION session);

	//参数处理
	bool parseParam_tag(json& params, RPC_RESP& rpcResult, RPC_SESSION session,string& tag,string& rootTag);

	//object manage
	void rpc_input(json params,RPC_RESP& resp, RPC_SESSION session);
	string rpc_getTopoList(json params, string& error,RPC_SESSION session);
	void rpc_getMoStatis(json params, RPC_RESP& resp, RPC_SESSION session);
	string rpc_getMoOnlineStatus(json params, string& error);
	string rename(string orgName, json& renameMap);
	void rpc_moList2Attrlist(Mo_Attr_Params& params, vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getMoAttr_list(json params, RPC_RESP& resp,RPC_SESSION session);
	void rpc_moList2table(Mo_Attr_Params& params, vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION session);
	void rpc_output(json params, RPC_RESP& resp, RPC_SESSION session);
	string rpc_getMpStatus(json params, string& error, RPC_SESSION session,bool bValOnly =false);
	string rpc_heartbeat(json params, string& error, RPC_SESSION session);

	//device manage
	void rpc_getDev(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getDevStatis(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_getChanStatus(json params, RPC_RESP& resp);
	void rpc_getChanVal(json params, RPC_RESP& resp);
	string rpc_io_scanChannel(json params, string& error);


	//serial function
	string rpc_openCom(json params, string& error);
	string rpc_com_list(json params, string& error);
	string rpc_closeCom(json params, string& error);

	//genicam steam function
#ifdef ENABLE_GENICAM
	string rpc_getStreamInfo(json params, string& error);
	string rpc_setStream(json params, string& error);
#endif

	//notification
	//orgSession不为null表示来自于tds客户端，为null表示来自tds服务
	void notify(string method, json params, std::shared_ptr<TDS_SESSION> orgSession = nullptr);

	//alarm
	json getAlarmStatis(string rootTag, RPC_SESSION session);

	//辅助功能
	string rpc_getconf(json params, string& error);
	string rpc_setconf(json params, string& error);
	string rpc_getconffile(json params, string& error);
	string rpc_setconffile(json params, string& error);

	json rpc_getStreamUrl(MP* pmp, string tag, bool isHttps, string hostname, int port);

    string  ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession);

	json m_licenceStatus;
	mutex m_csLicenceStatus;

	void saveDataFromUrl(string& strUrl, TIME& stTime, string& strTag, string suffix);

	fp_rpcHandler m_pluginHandler;
};
extern rpcHandler rpcSrv;
