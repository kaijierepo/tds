#pragma once
#include "ioDev.h"
#include <string>
#include "udpSrv.h"
#include "json.hpp"

class CHAN_TEMPLATE {
public:
	string name;
	string label;
	json channels;
	json toJson();
};

namespace IO_PROTO {
	const string leakDetect = "iop_leakDetect";
	const string MODBUS_RTU = "modbusRTU";
	const string TDSRPC = "tdsRPC";
	const string IQ60 = "iq60";
};

namespace DEV_TYPE {
	namespace DEV {
		const string tdsp_device = "tdsp-device";
	}
	const string child_tds = "childTds";
};

#define DEV_TYPE_iq60 "iq60-gateway"
#define DEV_TYPE_tdsp "tdsp-device"
#define DEV_TYPE_modbus_tcp_slave "modbus-tcp-slave"
#define DEV_TYPE_rs485_gateway "rs485-gateway"
#define DEV_TYPE_local_serial "local-serial"
#define DEV_TYPE_leak_detect "leak-detect"


namespace DEV_SUB_TYPE {
	namespace ethernetIP {
		const string control_logix = "control-logix";
	}
}

inline string getDevSubTypeLabel(string devSubType) {
	if (devSubType == "childTds") {
		return "子服务";
	}
	else if (devSubType == "control-logix") {
		return "ControlLogix PLC";
	}
	else {
		return "";
	}
}

inline string getDevTypeLabel(string devType) {
	if (mapDevTypeLabel.find(devType) != mapDevTypeLabel.end()) {
		return mapDevTypeLabel[devType];
	}
	else {
		return "未知类型";
	}
}

struct CHILD_TDS_INFO {
	string tag;
	int httpPort;
	int httpsPort;
	string ip;
};


struct DEV_STATIS {
	string rootTag;
	size_t iOnline;
	size_t iOffline;
	size_t iTotal;
	size_t iInservice;
	size_t iSpare;
	size_t iChan;

	DEV_STATIS() {
		 iOnline =0;
		 iOffline = 0;
		 iTotal = 0;
		 iInservice = 0;
		 iSpare = 0;
		 iChan = 0;
	}

	json toJson() {
		json jStatis;
		jStatis["online"] = iOnline;
		jStatis["offline"] = iOffline;
		jStatis["inService"] = iInservice;
		jStatis["spare"] = iSpare;
		jStatis["channel"] = iChan;
		jStatis["total"] = iTotal;
		return jStatis;
	}
};

class ioHandler_mbRtu : public ITcpServerCallBack, public IUdpServerCallBack {
	void OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) override;
	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession) override;
};


//并发问题
//设备上线操作ioDev列表和读取列表的并发问题,目前缺少有效的控制

class ioServer : public i_ioServer, public ioDev, public ITcpServerCallBack
{
public:
	ioServer();
	virtual ~ioServer();

	//主开关
	bool run() override;
	bool runAsCloud();
	void stop() override;

	//组态信息
	bool toJson(json& conf, json opt = nullptr);
	bool loadConf();
	bool loadConfMerge(json& j);
	bool loadConfAppend(json& j);
	void saveConf();
	void clear(); //清空所有ioDev对象及其相关的工作线程
	size_t getBindedChanCount() override;

	size_t getChanCount();

	//统计信息
	int m_totalPtCount;

	//通道模版配置
	json getDevTemplate(string devTplType);
	bool loadChanTemplate();
	void saveChanTemplate();
	map<string, CHAN_TEMPLATE> m_mapChanTempalte;

	//查询与管理
	void queryDev(DEV_QUERIER devQuerier, DEV_STATIS& devStatis, vector<ioDev*>& filterRlt);
	void getAllSmartDev(vector<ioDev*>& aryDev);
	void getAllTDSPDev(vector<ioDev*>& aryDev);
	ioDev* getOwnerChildTdsDev(string tag);
	bool getOwnerChildTdsInfo(string tag,CHILD_TDS_INFO& info);

	//在线组态
	void rpc_addDev(json& params, RPC_RESP& rpcResp,RPC_SESSION sesion);
	void rpc_searchDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_deleteDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_modifyDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_disposeDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion); //设置设备的管理状态
	void rpc_startDevUpgrade(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_stopDevUpgrade(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_uploadDevFirmware(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_startDevUpgradeProc(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_stopDevUpgradeProc(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_getChanTemplate(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);
	void rpc_setChanTemplate(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion);

	//设备上下线
	void handleDevOnlineAsyn(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession);
	ioDev* handleDevOnline(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession);

	//Tcp通信
	map<int, string> m_mapPort2DevType;
	tcpSrv* m_tcpSrv_tdsp; //665 tdsp协议
	vector<tcpSrv*> m_tcpSrv_rtu; //664 modbus RTU over tcp协议
	vector<udpServer*> m_udpSrv_rtu; //664 modbus RTU over udp协议
	tcpSrv* m_tcpSrv_mbTcp; //502 modbus tcp协议
	tcpSrv* m_tcpSrv_iq60; //
	tcpSrv* m_tcpSrv_leakDetect; //
	udpServer* m_udpSrv_tdsp;   //665 tdsp ，adaptor
	string m_strAdpIp;
	int m_iAdpPort;

	void statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn);


	ioHandler_mbRtu ioHandler_mbRtu_udp;


	//通信分层处理
	//传输层处理
	void OnRecvData_TCP(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;
	//void OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) override;
	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession) override;

	//应用层字节流组包 与 首发包处理
	bool handleFirstRegPkt(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	bool OnRecvAppLayerData(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);
	void handleUDPTdspPkt(yyjson_val* pkt, yyjson_doc* doc, string strIP, int port, string ioSessionAddr);

	//应用层数据包处理
	void onRecvPkt_tdsp(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void onRecvPkt_iq60(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void onRecvPkt_mbRtu(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void onRecvPkt_leakDetect(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void onRecvPkt_mbTcp(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);

	//tdsRPC服务
	void rpc_getSessionStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	//IO会话
	shared_ptr<TDS_SESSION> getTDSSession(tcpSession* pTcpSess);
	shared_ptr<TDS_SESSION> getTDSSession(string remoteIP, int remotePort);
	shared_ptr<TDS_SESSION> getTDSSession(string remoteAddr);
	shared_ptr<TDS_SESSION> getTDSSession(tcpSessionClt* pTcpSess);
	std::shared_ptr<TDS_SESSION> getStreamPusher(string tag);
	vector<std::shared_ptr<TDS_SESSION>> getStreamPushers(string tag);
	map<void*,std::shared_ptr<TDS_SESSION>> m_IoSessions;
	mutex m_mutexIoSessions;

	ioDev* getIODev(string ioAddr,bool bChn = false,bool ignorePort = false) override;
	
	void updateTag2IOAddrBinding();//更新mo中的ioAddr绑定信息
	void updateAllChanVal();

	bool getStatus(json& conf, string opt = "");
	string getTag(string ioAddr);

	//设备发现必须是某个父设备发现了子设备
	ioDev* onChildDevDiscovered(json childDevAddr, string ioSessionAddr,string type, string subType = "");

	bool m_stopCycleAcq; //全局周期采集开关，调试时使用，调试时全局关闭周期采集。方便手工发送数据并观察
	bool m_tdspOnlineReq;
	bool m_tdspSingleTransaction; //tdsp设备同一时刻只能发起一个请求
	int m_serialSendDelayAfterRecv;
	bool m_bPingThreadStart;
	string m_ioSrvIP; 
	string m_ioSrvIPAsClient;//ioSrv作为客户端跟设备通信时绑定的本地ip
	bool m_bDisableIOHandle;  //用于测试性能，检查是否io处理对性能消耗比较大
};

extern ioServer ioSrv;