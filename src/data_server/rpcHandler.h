/*
rpc handler
*/
#pragma once
#include "tdb.h"
#include "tdsSession.h"
#include "obj.h"
#include "tSockSrv.h"

struct Mo_Attr_Params {
	json renameMap;
	TAG_SELECTOR tagSel;
	json jAttrSel;
	TAG_SELECTOR attrSel;
	std::string valFmt;
	bool bSelAttr;
	std::string columeLabel; //默认使用位号作为列名
	std::string moType;
	std::string rootTag;
	json jColumes;
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

	bool needLog(std::string method);

	void setLicenceStatus(json j);
	//rpc路由的命令  可以路由到tdsp设备，或者childTds
	bool handleRpcRoute(yyjson_val*, RPC_RESP& rpcResp,std::shared_ptr<TDS_SESSION> pSession);
	void logRPCRoute(std::string method, json& params, const RPC_RESP& rpcResp,RPC_SESSION& session);

	bool isGB2312Pkt(std::string& req);

	//json rpc implementation
	void handleRpcCallAsyn(std::string& strReq,std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl = true);
	bool parseSessionUser(yyjson_val* jReq,RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession);
	bool userAccessAuthentication(yyjson_val* jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession);
	bool isDebugMethod(std::string method);
	void handleRpcCall_single(yyjson_val* jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl);
	long long getTick();
	void handleRpcCall(std::string& strReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession,bool bAccessCtrl = true);
	bool handleMethodCall_OSFunc(std::string method, json& params, RPC_RESP& rpcResp);
	bool handleMethodCall_ptz_cloud(std::string method, MP* pmp, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_ptz_ioDev(std::string method, std::string tag, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_video(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_db(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	void rpc_getApiSessionStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_debugFunc(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_IoMng(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_audioPlayer(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_edgeDev(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_gamePad(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_utils(const std::string& method, yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_MoMng(std::string method, json& params, RPC_RESP& rpcResp,RPC_SESSION& session);
	std::vector<std::string> parseTagSel(json& tagSel,std::string& type);
	bool handleMethodCall_alarmMng(std::string method, json& params, RPC_RESP& rpcResp,RPC_SESSION& session);
	bool handleMethodCall_userMng(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall_unclassified(std::string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session);
	bool handleMethodCall(std::string method, json params, RPC_RESP& rpcResult, RPC_SESSION& session);
	bool handleMethodCall(std::string method, yyjson_val* params, RPC_RESP& rpcResult, RPC_SESSION& session);
	
	//参数处理
	bool parseParam_tag(json& params, RPC_RESP& rpcResult, RPC_SESSION& session,std::string& tag,std::string& rootTag);

	//object manage
	void rpc_input(json params,RPC_RESP& resp, RPC_SESSION& session);
	std::string rpc_getTopoList(json params, std::string& error,RPC_SESSION& session);
	void rpc_getMpStatis(json params, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_getObjStatis(json params, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_getMoStatis(json params, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_getDevStatis_old(json params, RPC_RESP& resp, RPC_SESSION& session);
	std::string rpc_getMoOnlineStatus(json params, std::string& error);
	std::string renameItem(std::string orgName, json& renameMap);
	void toMoAttr(Mo_Attr_Params& params, OBJ* pMo, nlohmann::ordered_json& attrInfo, RPC_SESSION& session);
	void rpc_moList2Attrlist(Mo_Attr_Params& params, std::vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_getMoAttr_list(json params, RPC_RESP& resp,RPC_SESSION& session);
	void rpc_moList2table(Mo_Attr_Params& params, std::vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_output(json params, RPC_RESP& resp, RPC_SESSION& session);
	std::string rpc_heartbeat(json params, std::string& error, RPC_SESSION& session);
	void rpc_onObjOnline(json params, RPC_RESP& rpcResp, RPC_SESSION& session);
	void rpc_onObjOffline(json params, RPC_RESP& rpcResp, RPC_SESSION& session);
	std::string getChildTdsRegPkt();

	//device manage
	void rpc_getDev(json params, RPC_RESP& resp, RPC_SESSION& session);
	void rpc_getChanStatus(json params, RPC_RESP& resp);
	void rpc_getChanVal(json params, RPC_RESP& resp);
	std::string rpc_io_scanChannel(json params, std::string& error);

	//serial function
	std::string rpc_openCom(json params, std::string& error);
	std::string rpc_com_list(json params, std::string& error);
	std::string rpc_closeCom(json params, std::string& error);

	//video stream
	void rpc_startStreamPush(json params, RPC_RESP& resp, RPC_SESSION& session);
	void sendStreamPusherRegPkt(std::shared_ptr<TDS_SESSION> p, std::string tag);
	json rpc_getStreamUrl(MP* pmp, std::string tag, bool isHttps, std::string hostname, int port);

	//notification
	//orgSession不为null表示来自于tds客户端，为null表示来自tds服务
	//notify为一般通知，没有订阅则不通知。订阅机制以后做
	//特殊通知，所有客户端都通知
	//普通通知，数值变化这些，数据量很多，订阅才通知
	void notify(std::string method, std::string notifyParams,bool specialNotify = false, std::shared_ptr<TDS_SESSION> orgSession = nullptr);
	
	//alarm
	json getAlarmStatis(std::string rootTag, RPC_SESSION& session);

	//辅助功能
	std::string rpc_getconf(json params, std::string& error);
	std::string rpc_setconf(json params, std::string& error);


    std::string  ResolveTdsRpcEvnVar(std::string strIn, std::shared_ptr<TDS_SESSION> pSession);

	json m_licenceStatus;
	mutex m_csLicenceStatus;

	void saveDataFromUrl(std::string& strUrl, TIME& stTime, std::string& strTag, std::string suffix);

	fp_rpcHandler m_pluginHandler;


	void cleanRpcSession();
	TIME m_lastCleanTime;
	std::mutex m_csRpcSessions;
	std::map<std::string, RPC_SESSION> m_mapRpcSessions; //无连接会话，根据remoteAddr来区分，失效时间1个小时
	std::mutex m_csCallStatis;
	std::map<std::string, int> m_mapCallStatis;
	void statisCall(std::string method);

	std::map<std::string, std::string> m_mapDisableMethod;
	map<std::string, TIME> m_mapPullerActive;

	bool apiAdaptorScript(std::string& strResult);
	bool apiAdaptorScript(json& jResult);

	void vlmAlarmCheck(std::string& sParams);
	void samAlarmCheck(std::string& sParams);
};
extern rpcHandler rpcSrv;

void jsonToList(json & data, std::vector<double>& p);
float CalDTWDist(const std::vector<double>& vecRef, const std::vector<double>& vecCur);
void onSockSrvCallback(char* pData, size_t iLen, std::shared_ptr<SOCK_SESSION> sockSess);
void onSockSrvStatusCallback(bool conn, std::shared_ptr<SOCK_SESSION> sockSess);
