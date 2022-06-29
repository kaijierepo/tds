#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"
#include "ioDiscoverer.h"


//并发问题
//设备上线操作ioDev列表和读取列表的并发问题,目前缺少有效的控制

class ioServer : public ioDev, public ITcpServerCallBack, public ITcpClientCallBack
{
public:
	ioServer();
	virtual ~ioServer();

	//主开关
	bool run() override;
	bool runAsCloud();
	bool runAsEdge();
	void stop() override;

	//组态信息
	bool toJson(json& conf, json opt = nullptr);
	bool loadConf();
	bool loadConfMerge(json& j);
	bool loadConfAppend(json& j);
	void saveConf();
	void clear(); //清空所有ioDev对象及其相关的工作线程

	//查询与管理
	void getAllSmartDev(vector<ioDev*>& aryDev);
	void getAllTDSPDev(vector<ioDev*>& aryDev);

	//在线组态
	void rpc_addDev(json& params, RPC_RESP& rpcResp);
	void rpc_deleteDev(json& params, RPC_RESP& rpcResp);
	void rpc_modifyDev(json& params, RPC_RESP& rpcResp);
	void rpc_disposeDev(json& params, RPC_RESP& rpcResp); //设置设备的管理状态

	//设备上下线
	void handleDevOnlineAsyn(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession);
	ioDev* handleDevOnline(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession);

	//Tcp通信
	tcpSrv* m_tcpSrv_tdsp; //665 tdsp协议
	tcpSrv* m_tcpSrv_rtu; //664 modbus RTU over tcp协议
	tcpSrv* m_tcpSrv_iq60; //
	void statusChange_tcpClt(tcpSessionClt* pTcpSessClt, bool bIsConn);
	void statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn);
	void OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);

	//传输层处理
	void handleAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);
	void onRecvPkt_ioDev(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool registerPkt = false);
	bool handleFirstRegPkt(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	bool OnRecvAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);

	//tdsRPC服务
	void rpc_getSessionStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION session);

	//IO会话
	map<void*,std::shared_ptr<TDS_SESSION>> m_IoSessions;
	mutex m_mutexIoSessions;

	ioDev* getIODev(string ioAddr,bool bChn = false) override;
	
	void updateTag2IOAddrBinding();//更新mo中的ioAddr绑定信息

	void refreshSerialIODev();

	bool getStatus(json& conf, string opt = "");
	string getTag(string ioAddr);

	//设备发现必须是某个父设备发现了子设备
	ioDev* onChildDevDiscovered(json childDevAddr, string type);
	ioDiscoverer  ioDiscoverService;


	bool m_stopCycleAcq; //全局周期采集开关，调试时使用，调试时全局关闭周期采集。方便手工发送数据并观察
};

extern ioServer ioSrv;