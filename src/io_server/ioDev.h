#pragma once
#include "pch.h"
#include "tdsSession.h"

class mo;
class mp;
class ioAddrSession;
class ioChannel;
//asyn pkt received is not processed from DMS_UNCONF ioDev
//no DoCycleTask for DMS_UNCONF ioDev
//do not use pIODev->m_pMO for DMS_UNCONF ioDev，it's empty
typedef void* (*fp_ioAddrRecvCallback)(void* user, char* pData, int iLen);
class ioDev
{
public:
	ioDev(void);
	~ioDev(void);

	virtual bool run() { return true; };

	//is Gateway
	// can be 1.ip or domain name with port 2.tuya project id
	//is Device 
	// can be 1.ip or domain name with port 2. field bus id
	//is Channel
	// can be 1. mqtt topic 2.tuya device id
	string m_addr;
	// addr of different level(gateway,device,channel) devices makes an ioAddr
	ioAddress getIOAddr();
	ioDev* getIODev(ioAddress iopath);
	ioDev* getIODev(string ioAddr);
	IODEV_MNG_STATUS m_mngStatus;

	//directly bridge ioDev to tds websocket session
	std::shared_ptr<TDS_SESSION> pTdsSession;

	virtual bool outputVal(json jVal,string chanAddr="") { return false; };
	virtual bool inputVal(json jVal,string chanAddr="") { return false; };

	//tree management
	vector<ioDev*> m_vecChild;
	void deleteChild(ioDev* p);
	ioDev* getChild(ioAddress& iopath);
	ioDev* getChild(string addr);
	ioDev* m_pParent;

	ioChannel* getIOChan(string tag);

	void AutoDataLink(mo* mo);
	string m_devType;
	string m_level;
	bool IsGateway();
	string m_secret;
	string m_strGatewayIP;
	bool  NotNeedGateway();   //按照现在流行的技术以及常见通讯方式， 一个IP+和一个总线地址 可以满足所有物联设备的通讯需求


	fp_ioAddrRecvCallback m_pRecvCallback;
	void* m_pCallbackUser;

	//which monitor object this ioDevice is installed to 
	string m_installedMoTag;
	mo* m_pMO;

	bool bEnableAcq;
	string GetCommIP();
	void SendToChild(SYSTEMTIME dataTime, char* pData, int iLen, string strID);//网关类型使用，转发给下层子设备

	//通信发送
	void CommLock();
	void CommUnlock();
	bool SendPkt(PKT_DATA& pkt);//发送不等待
	virtual bool sendData(char* pData, int iLen);
	bool CmdRequestSync(char* pReqData, int iReqLen, char* pRespData, int& iRespLen);//发送并阻塞等待回包
	bool CmdRequestSync(PKT_DATA& req, PKT_DATA& resp, int iRetryCount = 0, string strLogMsgWhenSend = "");//=0表示使用全局配置

	//通信接收
	virtual bool SendHeartbeatPkt();
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
	ioAddrSession* m_pCommAddrInfo;//该设备地址的通讯信息
	bool m_bOnline;
	int m_iSendDataFailCount;//记录设备通信失败次数.达到三次判定离线,重试1次就判定离线太频繁
	SYSTEMTIME m_stEqpOnLineDateTime;//设备上线时间戳
	SYSTEMTIME m_stEqpOffLineDateTime;//设备掉线时间戳
	bool IsConnected();
	virtual int GetAcqInterval();
	static bool m_bAsynAcqMode;//是否启用异步采集模式
	ioChannel* GetDataChannel(string strChanID);
	ioChannel* GetDataChannelByMPTag(string strMPTag);
	map<string,ioChannel*> m_mapDataChannel;
	map<string, string> m_mapBatchDataLink;
};


class TransparentGateway : public ioDev {
public:
	TransparentGateway() {};
};


class CCanTransparentGateway : public ioDev {
public:
	CCanTransparentGateway();
};