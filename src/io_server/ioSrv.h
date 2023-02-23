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

namespace IO_DEV_TYPE {
	namespace DEV {
		const string tdsp_device = "tdsp-device";
		const string modbus_rtu_slave = "modbus-rtu-slave";
		const string modbus_tcp_slave = "modbus-tcp-slave";
		const string iq60_gateway = "iq60-gateway";
		const string leakDetect = "leak-detect";
		const string genicam = "genicam";
		const string mqttBroker = "mqtt-broker";
		const string tuya = "tuya";
		const string visca = "visca";
		const string onvif = "onvif";
	}
	namespace GW {
		const string local_serial = "local-serial";
		const string can_gateway = "can-gateway";
		const string rs485_gateway = "rs485-gateway";
		const string tuya_iot_project = "tuya-iot-project";
	}
	namespace CHAN {
		const string io_channel = "io-channel";
	}
	namespace SERVER {
		const string tds = "tds";
	}
};

inline string getDevTypeLabel(string devType) {
	if (devType == IO_DEV_TYPE::DEV::modbus_rtu_slave)return "ModbusRTU";
	else if (devType == IO_DEV_TYPE::DEV::modbus_tcp_slave)return "ModbusTCP";
	else if (devType == IO_DEV_TYPE::DEV::tdsp_device)return "TDSP";
	else if (devType == IO_DEV_TYPE::GW::rs485_gateway)return "RS485转网络";
	else if (devType == IO_DEV_TYPE::CHAN::io_channel)return "IO通道";
	else if (devType == IO_DEV_TYPE::DEV::iq60_gateway)return "IQ60";
	else if (devType == IO_DEV_TYPE::GW::tuya_iot_project)return "涂鸦项目";
	else if (devType == "tuya") return "涂鸦设备";
	else return "未知类型";
}

struct CHILD_TDS_INFO {
	string tag;
	int httpPort;
	int httpsPort;
	string ip;
};


//并发问题
//设备上线操作ioDev列表和读取列表的并发问题,目前缺少有效的控制

class ioServer : public i_ioServer, public ioDev, public ITcpServerCallBack, public ITcpClientCallBack
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

	long getChanCount();

	//统计信息
	int m_totalPtCount;

	//通道模版配置
	json getDevTemplate(string devTplType);
	bool loadChanTemplate();
	void saveChanTemplate();
	map<string, CHAN_TEMPLATE> m_mapChanTempalte;

	//查询与管理
	void getAllSmartDev(vector<ioDev*>& aryDev);
	void getAllTDSPDev(vector<ioDev*>& aryDev);
	ioDev* getOwnerChildTdsDev(string tag);
	bool getOwnerChildTdsInfo(string tag,CHILD_TDS_INFO& info);

	//在线组态
	void rpc_addDev(json& params, RPC_RESP& rpcResp,RPC_SESSION sesion);
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
	tcpSrv* m_tcpSrv_rtu; //664 modbus RTU over tcp协议
	tcpSrv* m_tcpSrv_mbTcp; //502 modbus tcp协议
	tcpSrv* m_tcpSrv_iq60; //
	tcpSrv* m_tcpSrv_leakDetect; //
	udpServer* m_udpSrv_tdsp;   //665 tdsp ，adaptor
	string m_strAdpIp;
	int m_iAdpPort;
	void statusChange_tcpClt(tcpSessionClt* pTcpSessClt, bool bIsConn);
	void statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn);



	//通信分层处理
	//传输层处理
	void OnRecvData_TCP(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(char* pData, size_t iLen, tcpSession* pCltInfo);
	void OnRecvData_TCPClient(char* pData, size_t iLen, tcpSessionClt* connInfo);
	void OnRecvUdpData(char* recvData, size_t recvDataLen, string strIP, int port) override;

	//应用层字节流组包 与 首发包处理
	bool handleFirstRegPkt(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	bool OnRecvAppLayerData(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);

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
	void refreshSerialIODev();

	bool getStatus(json& conf, string opt = "");
	string getTag(string ioAddr);

	//设备发现必须是某个父设备发现了子设备
	ioDev* onChildDevDiscovered(json childDevAddr, string type, string subType = "");
	//ioDiscoverer  ioDiscoverService;


	bool m_stopCycleAcq; //全局周期采集开关，调试时使用，调试时全局关闭周期采集。方便手工发送数据并观察
};

extern ioServer ioSrv;