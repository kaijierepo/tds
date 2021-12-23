#pragma once
#include "pch.h"
#include "tdsSession.h"


class MO;
class MP;
class ioAddrSession;
class ioChannel;
//asyn pkt received is not processed from DMS_UNCONF ioDev
//no DoCycleTask for DMS_UNCONF ioDev
//do not use pIODev->m_pMO for DMS_UNCONF ioDev，it's empty
typedef void (*fp_ioAddrRecvCallback)(void* user, char* pData, int iLen);
class ioDev
{
public:
	ioDev(void);
	~ioDev(void);

	virtual bool run() { return true; }; //连接； 执行io任务； 断线重连
	virtual void stop();
	bool m_bRunning;
	virtual bool toJson(json& conf, json opt = nullptr);
	virtual bool getStatus(json& status, string opt = ""); //status是conf+实时状态的数据
	virtual bool getChanStatus(json& statusList); //获取所有子通道的状态列表
	virtual bool loadConf(json& conf);
	virtual bool connect();
	virtual bool disconnect();
	virtual string getDesc();


	////
	//is Gateway
	// can be 1.ip or domain name with port 2.tuya project id 3.gateway guid
	//is Device 
	// can be 1.ip or domain name with port 2. field bus id
	//is Channel
	// can be 1. mqtt topic 2.tuya device id
	//device addr in string format
	string m_devAddr;  // 多个devAddr 使用 / 连接组合成 ioAddr 
	json m_jDevAddr;  //json格式的设备地址   内部的数据结构按实际类型。例如地址如果是int，就用int类型，而不用string
	//device addr in json format
	virtual json getAddr(); 
	//io addr in struct format
	string getIOAddrStr();
	string getDevAddrStr();
	string m_mngStatus;
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

	bool m_bEnableIoLog;//是否记录io日志，用于临时暂停某些周期命令的io通讯的场景
	float m_fAcqInterval; //数据采样间隔，单位秒。精度0.1秒，为0表示不采样
	//// iodev hierachy tree management
	virtual ioDev* getIODev(string ioAddr);
	virtual ioDev* getIODev(json& ioAddr);
	vector<ioDev*> getChildren(string devType);
	vector<ioDev*> m_vecChild;
	bool addChild(ioDev* p);
	void deleteChild(ioDev* p);
	void deleteDescendant(ioDev* p);
	ioDev* getChild(string devAddr);
	ioDev* m_pParent;

	//通道管理
	virtual bool scanChannel(json& chanList) { return false; };//长时间阻塞函数，启动线程调用
	ioChannel* getChan(string addr);
	ioChannel* getChanByTag(string tag);

	//// data io
	//directly bridge ioDev to tds websocket session
	std::shared_ptr<TDS_SESSION> pTdsSession;

	void setIOSession(shared_ptr<TDS_SESSION> ioSession);
	void statisOnRecv(char* recvData, int len, string addr);
	void statisOnSend(char* sendData, int len, string addr);
	shared_ptr<TDS_SESSION> pIOSession;
	mutex m_csIOSession;
	//输出到设备
	virtual bool outputVal(json jVal,string chanAddr="") { return false; };
	virtual bool inputVal(json jVal,string chanAddr="") { return false; };
	//输出到设备的下属通道
	virtual bool output(string chanAddr, json jVal, json& chanResp, bool sync = false) { return false; }


	void AutoDataLink(MO* mo);
	bool  NotNeedGateway();   //按照现在流行的技术以及常见通讯方式， 一个IP+和一个总线地址 可以满足所有物联设备的通讯需求
	void setRecvCallback(void* pUser, fp_ioAddrRecvCallback callback);
	fp_ioAddrRecvCallback m_pRecvCallback;
	void* m_pCallbackUser;
	//对于通道tagBind表示数据连接的mp位号
	//对于设备tagBind表示设备安装在某个对象上,该对象一般视作智能设备
	string m_strTagBind;
	string m_strChanTemplate;
	MO* m_pMO;
	string GetCommIP();
	void SendToChild(SYSTEMTIME dataTime, char* pData, int iLen, string strID);//网关类型使用，转发给下层子设备
	//通信发送
	bool CommLock(int dwTimeoutMS = 0);
	void CommUnlock();
	bool SendPkt(PKT_DATA& pkt);//发送不等待
	virtual bool sendData(char* pData, int iLen);
	virtual bool sendStr(string& str);
	bool CmdRequestSync(char* pReqData, int iReqLen, char* pRespData, int& iRespLen);//发送并阻塞等待回包
	bool CmdRequestSync(PKT_DATA& req, PKT_DATA& resp, int iRetryCount = 0, string strLogMsgWhenSend = "");//=0表示使用全局配置

	//通信接收
	virtual bool SendHeartbeatPkt();
	virtual bool onRecvPkt(json jPkt);
	virtual bool OnRecvData(char* pData, int iLen);//接受数据异步处理函数
	virtual bool OnRecvData(SYSTEMTIME dataTime, char* pData, int iLen);
	virtual void OnRequestTimeout(int cmd1, int cmd2);
	int CalcTimePassSecond(SYSTEMTIME* stLast);
	//命令回包超时
	virtual bool IsAsynPacket(PKT_DATA* pd);

	//周期性采集任务执行
	virtual void DoCycleTask();


	static int m_heartBeatInterval;//单位秒
	SYSTEMTIME m_stLastHeartbeatTime;
	SYSTEMTIME m_stLastSetClockTime;
	SYSTEMTIME m_stLastAcqTime;
	ioAddrSession* m_pCommAddrInfo;//该设备地址的通讯信息
	bool bEnableAcq;
	bool m_bOnline;    //设备发现后，处于在线状态
	bool m_bConnected; //建立通信链路.串口打开后，处于connect状态。tcp连接，处于connect状态
	bool m_bInUse;     //连接的设备，某个程序功能正在使用该ioAddr。例如周期轮询任务等。用于功能互斥。
	int m_iSendDataFailCount;//记录设备通信失败次数.达到三次判定离线,重试1次就判定离线太频繁
	SYSTEMTIME m_stEqpOnLineDateTime;//设备上线时间戳
	SYSTEMTIME m_stEqpOffLineDateTime;//设备掉线时间戳
	bool IsConnected();
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
	std::shared_mutex m_csThis;  //内部结构修改互斥锁


	std::recursive_timed_mutex m_csCommLock;
	DWORD m_dwLockThread;

	json m_jAlarmStatus;
	json m_jAcq;
};

ioDev* createIODev(json conf);
extern vector<std::shared_ptr<TDS_SESSION>> commpktSessions;

class TransparentGateway : public ioDev {
public:
	TransparentGateway() {};
};


class CCanTransparentGateway : public ioDev {
public:
	CCanTransparentGateway();
};