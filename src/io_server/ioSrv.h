#pragma once
#include "ioDev.h"
#include <string>
#include "tdscore.h"
#include "ioGW_localSerial.h"
#include "ioDiscoverer.h"


//并发问题
//设备上线操作ioDev列表和读取列表的并发问题,目前缺少有效的控制

class ioServer : public ioDev
{
public:
	ioServer();
	virtual ~ioServer();

	tcpSrv* m_tcpSrv_tdsp; //665 tdsp协议
	tcpSrv* m_tcpSrv_rtu; //664 modbus RTU over tcp协议
	tcpSrv* m_tcpSrv_iq60; //

	bool loadConf();
	void saveConf();

	ioDev* handleDevOnline(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession);

	void rpc_addDev(json& params,RPC_RESP& rpcResp);
	void rpc_deleteDev(json& params,RPC_RESP& rpcResp);
	void rpc_modifyDev(json& params,RPC_RESP& rpcResp);
	void rpc_disposeDev(json& params, RPC_RESP& rpcResp); //设置设备的管理状态

	ioDev* getIODev(string ioAddr) override;
	ioDev* getIODevByTag(string tag);
	
	void updateTag2IOAddrBinding();//更新mo中的ioAddr绑定信息

	void clear(); //清空所有ioDev对象及其相关的工作线程

	//bool loadStatus();
	//void saveStatus();
	void refreshSerialIODev();
	bool runAsCloud();
	bool runAsEdge();
	void stop() override;
	bool toJson(json& conf, json opt = nullptr);
	bool getStatus(json& conf, string opt = "");
	string getTag(string ioAddr);

	//设备发现必须是某个父设备发现了子设备
	ioDev* onChildDevDiscovered(json childDevAddr, string type);
	ioDiscoverer  ioDiscoverService;

	void getAllSmartDev(vector<ioDev*>& aryDev);
	bool m_stopCycleAcq; //全局周期采集开关，调试时使用，调试时全局关闭周期采集。方便手工发送数据并观察
};

extern ioServer ioSrv;