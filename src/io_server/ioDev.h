#pragma once
#include "pch.h"
#include "tdsSession.h"

class ioDev;
class OBJ;
class MP;
class ioAddrSession;
class ioChannel;
//asyn pkt received is not processed from DMS_UNCONF ioDev
//no DoCycleTask for DMS_UNCONF ioDev
//do not use pIODev->m_pMO for DMS_UNCONF ioDev，it's empty
typedef void (*fp_ioAddrRecvCallback)(void* user, char* pData, int iLen);
typedef ioDev* (*fp_createDev)();

struct DEV_QUERIER {
	bool getStatus;
	bool getConf;
	bool getChild;
	bool getChan;
	bool getDetail;
	bool getUpgradeInfo;
	DEV_QUERIER() {
		getStatus = false;
		getConf = true;
		getChild = true;
		getChan = true;
		getDetail = true;
		getUpgradeInfo = false;
	}
};

class UPGRADE_INFO {
public:
	unsigned char* fileData;
	int fileLen;
	int pktNum;
	int pktLen;
	int currentPktNo;
	unsigned fileCrc;
	string fileName;
	string binPath;
	bool isUpgrading;
	string version;
	string devType;
	bool stopUpgradeSignal;
	string grogressInfo;
	string statusInfo;

	UPGRADE_INFO() {
		fileData = nullptr;
		fileLen = 0;
		pktNum = 0;
		pktLen = 0;
		currentPktNo = 0;
		isUpgrading = false;
		stopUpgradeSignal = false;
		isUpgrading = false;
	}

	json toJson() {
		json j;
		j["progress"] = grogressInfo;
		j["fileLen"] = fileLen;
		j["pktNum"] = pktNum;
		j["pktLen"] = pktLen;
		j["currentPktNo"] = currentPktNo;
		j["fileName"] = fileName;
		j["isUpgrading"] = isUpgrading;
		j["version"] = version;
		j["devType"] = devType;
		j["status"] = statusInfo;
		return j;
	}
	int calcPktNum(int pl);
	bool loadFirmwareFile(string fileName, int pl = 4000);
};

class ioDev
{
public:
	ioDev(void);
	~ioDev(void);

	virtual bool run(); //连接； 执行io任务； 断线重连
	virtual void stop();
	bool m_bRunning;
	semaphore m_evtIO;
	virtual bool isCommBusy() { return m_bIsWaitingResp; };
	virtual bool toJson(json& conf, json opt = nullptr);
	virtual bool getStatus(json& status, string opt = ""); //status是conf+实时状态的数据
	virtual bool getChanVal(json& valList);
	virtual bool getChanStatus(json& statusList); //获取所有子通道的状态列表
	virtual bool loadConf(json& conf);
	void addChannel(ioChannel* pC);
	virtual bool connect();
	virtual bool connect(json params) { return false; };
	virtual bool disconnect();
	virtual bool isCamera() { return false; };
	virtual string getDesc();
	void triggerCycleAcq();
	virtual bool call(string method, json params, json& result, json& error, bool sync = true) { return false; };
	virtual bool handleDevRpcCall(json& jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession);

	DEV_QUERIER parseQueryOpt(json& opt);
	////
	//is Gateway
	// can be 1.ip or domain name with port 2.tuya project id 3.gateway guid
	//is Device 
	// can be 1.ip or domain name with port 2. field bus id
	//is Channel
	// can be 1. mqtt topic 2.tuya device id
	//device addr in string format
	string m_devAddr;  // 多个devAddr 使用 / 连接组合成 ioAddr 
	string m_addrMode; 
	json m_jDevAddr;  //json格式的设备地址   内部的数据结构按实际类型。例如地址如果是int，就用int类型，而不用string
	//device addr in json format
	virtual json getAddr(); 
	//io addr in struct format
	string getIOAddrStr(bool ignorePort = false);
	string getDevAddrStr(bool ignorePort = false);
	string m_dispositionMode;
	string m_devType;
	string m_devTypeLabel;
	string m_parentDevType;
	string m_level;
	string m_name; //可以理解为在硬件中配置的 mo名称
	bool IsGateway();
	string m_secret;
	string m_strGatewayIP;
	string m_channelType;
	string m_channelTypeLabel;
	string m_confNodeId; //配置节点id
	string m_charset;  //协议文本编码类型
	string m_strUser;  //某些设备需要用户名和密码才能访问
	string m_strPwd;

	bool m_bEnableIoLog;//是否记录io日志，用于临时暂停某些周期命令的io通讯的场景
	float m_fAcqInterval; //数据采样间隔，单位秒。精度0.1秒，为0表示不采样
	//// iodev hierachy tree management
	virtual ioDev* getIODev(string ioAddr, bool bChn = false, bool ignorePort = false); //是否启用中文地址拼音模式查找
	ioDev* getIODevByNodeID(string nodeID);
	bool deleteIODevByNodeID(string nodeID);
	vector<ioDev*> getChildren(string devType);
	vector<ioDev*> m_vecChildDev;
	vector<ioChannel*> m_channels;
	bool addChild(ioDev* p);
	void deleteChildren();
	void deleteAllChannels();
	void deleteChild(ioDev* p);
	void deleteDescendant(ioDev* p);
	ioDev* getChild(string devAddr);
	ioDev* m_pParent;

	//通道管理
	virtual bool scanChannel(json& chanList) { return false; };//长时间阻塞函数，启动线程调用
	ioChannel* getChanByDevAddr(string addr);
	ioChannel* getChanByIOAddr(string addr);
	ioChannel* getChanByTag(string tag);
	static void recursiveGetChanCount(ioDev* p, long& count);

	//// data io
	//directly bridge ioDev to tds websocket session
	std::shared_ptr<TDS_SESSION> pSessionClientBridge;

	void bindIOSession(shared_ptr<TDS_SESSION> ioSession);
	void statisOnRecv(char* recvData, int len, string addr);
	void statisOnSend(char* sendData, int len, string addr);

	static string removePortFromIoAddr(string ioAddr);
	static string removePortFromDevAddr(string devAddr);

	//设备关联的网络会话。1个会话可以关联多台设备。  1台设备只关联1个会话
	tcpClt* m_tcpClt;
	shared_ptr<TDS_SESSION> pIOSession;
	mutex m_csIOSession;
	//输出到设备
	virtual bool outputVal(json jVal,string chanAddr="") { return false; };
	virtual bool inputVal(json jVal,string chanAddr="") { return false; };

	//输出到设备的下属通道
	virtual bool output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) { return false; }
	virtual bool output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync = true) { return false; };

	//设备多通道批量输入
	virtual bool input(vector<string> chanAddr, vector<json> val, SYSTEMTIME* stDataTime = nullptr);

	void AutoDataLink(OBJ* mo);
	bool  NotNeedGateway();   //按照现在流行的技术以及常见通讯方式， 一个IP+和一个总线地址 可以满足所有物联设备的通讯需求
	void setRecvCallback(void* pUser, fp_ioAddrRecvCallback callback);
	fp_ioAddrRecvCallback m_pRecvCallback;
	void* m_pCallbackUser;
	//对于通道tagBind表示数据连接的mp位号
	//对于设备tagBind表示设备安装在某个对象上,该对象一般视作智能设备
	string m_strTagBind;
	string m_strChanTemplate;
	string m_acqMode; //周期采集模式，分 all group single 三种模式
	string m_chanGroup;//在acqMode为group模式下有效
	bool m_acqAlarm; //是否轮询报警，发送getAlarmStatus给TDSP设备
	OBJ* m_pMO;
	string GetCommIP();
	void SendToChild(SYSTEMTIME dataTime, char* pData, int iLen, string strID);//网关类型使用，转发给下层子设备
	ioDev* getIODevByTag(string tag);
	//通信发送
	virtual bool CommLock(int dwTimeoutMS = 0);
	virtual void CommUnlock();
	bool SendPkt(PKT_DATA& pkt);//发送不等待
	virtual bool sendData(char* pData, int iLen);
	virtual bool sendStr(string& str);
	bool CmdRequestSync(char* pReqData, int iReqLen, char* pRespData, int& iRespLen);//发送并阻塞等待回包
	bool CmdRequestSync(PKT_DATA& req, PKT_DATA& resp, int iRetryCount = 0, string strLogMsgWhenSend = "");//=0表示使用全局配置

	//通信接收
	virtual bool SendHeartbeatPkt();
	virtual bool onRecvPkt(json jPkt);
	virtual bool onRecvPkt(char* pData, int iLen) { return false; }; //接收到完整的协议数据包
	virtual bool OnRecvData(char* pData, int iLen);//接受数据异步处理函数
	virtual bool OnRecvData(SYSTEMTIME dataTime, char* pData, int iLen);
	virtual void OnRequestTimeout(int cmd1, int cmd2);
	//命令回包超时
	virtual bool IsAsynPacket(PKT_DATA* pd);

	virtual bool startUploadFirmware(string firmwareFileName);
	virtual bool stopUpgrade() { return false; };
	//周期性采集任务执行
	virtual void DoCycleTask();
	virtual void checkAcqReqTimeout();

	static int m_heartBeatInterval;//单位秒
	SYSTEMTIME m_stLastHeartbeatTime;
	SYSTEMTIME m_stLastSetClockTime;
	SYSTEMTIME m_stLastAcqTime;  //上一次采集任务开始时间
	bool m_bIsWaitingResp;  //表示一次通信会话正在进行中。可能是异步处理，也可能是同步处理
	SYSTEMTIME m_stLastReqSendTime; //上一次采集请求发送时间
	SYSTEMTIME m_stLastChanDataTime;
	SYSTEMTIME m_stLastAlarmStatusTime;
	SYSTEMTIME m_stLastActiveTime;
	ioAddrSession* m_pCommAddrInfo;//该设备地址的通讯信息
	bool m_bEnableAcq;
	void setOnline();
	void setOffline();
	virtual bool isOnline() { return m_bOnline; };
	bool m_bOnline;    //设备发现后，处于在线状态
	bool m_bConnected; //建立通信链路.串口打开后，处于connect状态。tcp连接，处于connect状态
	bool m_bInUse;     //连接的设备，某个程序功能正在使用该ioAddr。例如周期轮询任务等。用于功能互斥。
	bool m_bUdpDev;    //udp设备。udp设备没有连接
	int m_iSendDataFailCount;//记录设备通信失败次数.达到三次判定离线,重试1次就判定离线太频繁
	SYSTEMTIME m_stOnlineTime;//设备上线时间戳
	SYSTEMTIME m_stOfflineTime;//设备掉线时间戳
	virtual bool isConnected();
	virtual int GetAcqInterval();
	static bool m_bAsynAcqMode;//是否启用异步采集模式
	
	ioChannel* GetDataChannelByMPTag(string strMPTag);
	map<string,ioChannel*> m_mapDataChannel;
	map<string, string> m_mapBatchDataLink;

	string m_softVer;
	string m_hardVer;
	string m_mfrDate;
	string m_IMEI; 

	bool m_bWorkingThreadRunning;
	semaphore m_signalWorkThreadExit; //工作线程退出信号

	UPGRADE_INFO m_upgradeInfo;

	//动态数据锁与配置数据锁设计概要
	//动态数据在修改时，不影响配置，因此不应当影响配置的读取
	//
	std::shared_mutex m_csThis;  //配置-静态-数据锁
	map<DWORD,DWORD> m_mapConfLockSharedOwnerThread;
	std::shared_mutex m_csMapLock;
	DWORD m_dwConfLockUniqueOwnerThread;
	void lock_conf_shared() { 
#ifdef DEBUG
		DWORD dw = GetCurrentThreadId();
		m_csMapLock.lock();
		m_mapConfLockSharedOwnerThread[dw] = dw;
		m_csMapLock.unlock();
#endif
		m_csThis.lock_shared(); 
	}
	void unlock_conf_shared() { 
#ifdef DEBUG
		DWORD dw = GetCurrentThreadId();
		m_csMapLock.lock();
		m_mapConfLockSharedOwnerThread.erase(dw);
		m_csMapLock.unlock();
#endif
		m_csThis.unlock_shared(); 
	}
	void lock_conf_unique() {
#ifdef DEBUG
		m_csThis.lock(); 
		DWORD dw = GetCurrentThreadId();
#endif
		m_dwConfLockUniqueOwnerThread = dw;
	}
	void unlock_conf_unique() { 
#ifdef DEBUG
		m_dwConfLockUniqueOwnerThread = 0;
#endif
		m_csThis.unlock(); 
	}

	std::recursive_timed_mutex m_csCommLock;  //运行时-动态-数据锁
	DWORD m_dwCommLockOwnerThread;
	DWORD m_dwLockThread;

	json m_jAlarmStatus;
	json m_jAcq;
	json m_jConf;
	json m_jInfo;
	bool m_onlineInfoQueried; //上线后2秒，执行一次信息查询。等两秒是为了防止串口转网络数据错误问题
	json m_jSettingConf; //当前正在尝试设置的设备配置。如果setConf命令返回成功。将m_jSettingConf合并到m_jConf

	void saveConfBuff();
	bool loadConfBuff();
	void saveInfoBuff();
	bool loadInfoBuff();
	void saveStatusBuff();
	bool loadStatusBuff();

	string m_strErrorInfo;
};

ioDev* createIODev(string type);
extern std::map<string, fp_createDev> mapDevCreateFunc;

class TransparentGateway : public ioDev {
public:
	TransparentGateway() {};
};


class CCanTransparentGateway : public ioDev {
public:
	CCanTransparentGateway();
};