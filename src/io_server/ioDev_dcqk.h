#pragma once
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"
#include "Parse315Protocol.h"
#include "ioDev_tdsp.h"

//报警类型
#define ALARM_TYPE_QKYJ			1				//缺口预警及预警图像
#define ALARM_TYPE_QKBJ			2				//缺口报警及报警图像
#define ALARM_TYPE_QKSBGZ		3				//缺口采集设备故障，此时没有缺口值及缺口图像，左右偏移标志填无效(00),总包数填1，本包序号0，图像总长度0， 本帧图像长度0.
#define ALARM_TYPE_TXWFSB		4				//缺口图像无法识别报警
#define ALARM_TYPE_GCKLGD		5				//过车时框量过大报警及过车视频
#define ALARM_TYPE_ZZJSBGZ		6				//转辙机采集设备故障报警
#define ALARM_TYPE_WDBJ			7				//温度报警
#define ALARM_TYPE_SDBJ			8				//湿度报警
#define ALARM_TYPE_YWYJ			9				//油位预警（预留）
#define ALARM_TYPE_YWBJ			10				//油位报警（预留）
#define ALARM_TYPE_POWERYJ      11              //阻力预警
#define ALARM_TYPE_POWERBJ      12              //阻力告警
#define ALARM_TYPE_ZHUANPOWERBJ      13              //道岔转换阻力超限报警
#define ALARM_TYPE_LOCKBJ      14              //外锁闭装置锁闭力超限报警
#define ALARM_TYPE_CRSGAP      15              //过车时缺口值
#define ALARM_TYPE_STATICGAPYJ      16              //静态缺口预警
#define ALARM_TYPE_STATICGAPBJ      17              //静态缺口报警
#define ALARM_TYPE_TEMPERATURE  77				//温度预警
#define ALARM_TYPE_HUMILITY		78				//湿度预警
#define ALARM_TYPE_QKYJHF		101				//缺口预警恢复及图像
#define ALARM_TYPE_QKBJHF		102				//缺口报警恢复及图像
#define ALARM_TYPE_QKSBGZHF		103				//缺口采集设备故障恢复及图像
#define ALARM_TYPE_TXWFSBHF		104				//缺口图像无法识别报警恢复及图像
#define ALARM_TYPE_ZZJSBGZHF	105				//转辙机采集设备故障报警恢复
#define ALARM_TYPE_GCKLGDHF		106				//过车时框量过大报警恢复及过车视频
#define ALARM_TYPE_WDBJHF		107				//温度报警恢复
#define ALARM_TYPE_SDBJHF		108				//温度报警恢复
#define ALARM_TYPE_YWYJHF		109				//油位预警恢复（预留）
#define ALARM_TYPE_YWBJHF		110				//油位报警恢复（预留）
#define ALARM_TYPE_POWERYJHF    111             //阻力预警恢复
#define ALARM_TYPE_POWERBJHF    112             //阻力告警恢复
#define ALARM_TYPE_ZHUANPOWERBJHF      113              //道岔转换阻力超限报警恢复
#define ALARM_TYPE_LOCKBJHF      114              //外锁闭装置锁闭力超限报警恢复
#define ALARM_TYPE_CRSGAPHF      115              //过车时缺口值恢复
#define ALARM_TYPE_STATICGAPYJHF      116              //静态缺口预警恢复
#define ALARM_TYPE_STATICGAPBJHF      117              //静态缺口报警恢复
#define ALARM_TYPE_TEMPERATUREHF  177			//温度预警恢复
#define ALARM_TYPE_HUMILITYHF	178				//湿度预警恢复

//typedef struct
//{
//	WORD packid;			//本包序号
//	DWORD datalen;			//本包内容长
//	LPVOID lpdata;			//本包内容
//}VedioPackage;

typedef struct
{
	WORD sid;				//转辙机ID
	DWORD time;				//开始时间：4字节(Unix时间)
	WORD timelen;			//视频时长
	DWORD len;				//视频长度：视频的大小，字节数
	WORD packcnt;			//总包数
	//WORD recvcnt;			//已接收包数
	WORD maxpacklen;		//包长度
	BOOL bFinished;			//是否接收完毕
	set<WORD> setPack;		//已接收包序号
	vector<BYTE> vctPacks;
}VedioFile;

typedef map<DWORD, VedioFile> VedioTimeMap;

struct eqpInfo
{
	string daochaName;
	string eqpName;
	uint32_t timeLastTriggerVedio = 0;	//上次获取扳动视频列表时间
	uint32_t timeLastPassCarVedio = 0;	//上次获取过车视频列表时间
	list<StVedioRecord> lstVedioCache;	//视频未获取列表 filldata[0]:视频类型 filldata[1]:视频采集状态
	DWORD	 timeLastReqDownload;	//上次请求下载时间
	VedioTimeMap m_mapVideos;

	CRITICAL_SECTION m_csVedioList;
	CRITICAL_SECTION m_csVedio;
	eqpInfo()
	{
		InitializeCriticalSection(&m_csVedio);
		InitializeCriticalSection(&m_csVedioList);
	}
	~eqpInfo()
	{
		DeleteCriticalSection(&m_csVedio);
		DeleteCriticalSection(&m_csVedioList);
	}

	void AddVedioCache(StVedioRecord*pTimes, WORD wCount, BYTE btType);
	void DeleteVedioCache(uint32_t sTm, BYTE btType);
};


class ioDev_dcqk : public ioDev_tdsp
{
public:
	ioDev_dcqk();
	~ioDev_dcqk();

	void DoAcq();
	void DoCycleTask() override;
	void onEvent_online() override;

	virtual bool onRecvPkt(unsigned char* pData, size_t iLen) override;
	virtual bool onRecvData(unsigned char* pData, size_t iLen) override;

	int DealJHDData(LPVOID lpParam);

	string Get0x97AlarmLevelType(BYTE type);
	string Get0x27AlarmLevelType(BYTE type);

	string GetAlmLevel(BYTE type);

	string Get0x27AlarmType(BYTE type, BYTE type1, int nVer);
	string Get0x97AlarmType(BYTE type, BYTE type1);
	string Get0x97AlarmDesc(const StAlarmAndImgRec& data);
	BOOL IsRecover(BYTE type);

	int SendHeartbeat();
	///	扳动操作
	void BanDongOpr(int iSID, BYTE bType);
	///	历史图片
	void GetHisImg(int nSID, time_t sTm, time_t eTm);
	///	历史视频
	void GetHisVedio(int nSID, time_t sTm, time_t eTm, BYTE btVedioType);
	/// 实时视频操作
	void RealVedioOpr(int nSID, BYTE btFixorinvert, BYTE btCmdType);
	/// 获取最新缺口
	void GetLastGap();
	/// 缺口实时图片
	void GetLastImg(int nSID);
	/// 缺口配置信息
	void GetGapCfg();
	///	缺口扩展配置信息
	void GetGapCfgEx();
	/// 阻力文件列表
	void Getpowerfilelist(int nSID, time_t sTm, time_t eTm, BYTE btDir);
	///	查询阻力曲线列表
	void SearchPowerCurveList(int nSID, time_t sTm, time_t eTm);
	///	下载阻力曲线文件
	void DownloadPowerCurveFile(int nSID, time_t sTm);
	///	手动加油
	void ManualOiling(int nSID, BYTE btFixorinvert);
	///	未恢复告警列表
	void GetUnRecoverAlarm(int nSID);
	///	工况操作
	void GongKuangOpr();
	/// 加油箱储油量
	void GetOilBoxVolume(int nSID);



	void ParseDaoChaNameByZZJName(const string& sZZJName, string& sDc);
	virtual void OnRecvData_TCPClient(unsigned char* pData, size_t len, tcpSessionClt* connInfo) override;

	//
	void Do_CMD_CODE_YYQX(LPVOID pData);
	void Do_CMD_CODE_GAPVAL(LPVOID pData);
	void Do_CMD_CODE_YWINFO(LPVOID pData);
	CRITICAL_SECTION		m_csEqp;
	std::map<int, eqpInfo*> m_mapEqp;

	static std::map<int, std::string> g_map0x97AlarmLevel;
	//接收
public:
	template<typename T>
	void DealVedioFile(T*);

	//发送
public:
	///	回执
	void SendCallBackHeart(StHeartBeat315* pData);
	void SendCallBack0x41(StElecCurve* lpsubdata);
	void SendCallBack0x25(StOilPreCurve* lpsubdata);
	void SendCallBack0x27(StAlarmAndImgInfo* lpsubdata);

	//查询视频列表
	void QueryVedioList(); //
	//查询视频
	void QueryVedio(WORD sid, StVedioRecord* pST);

public:
	TIME m_stLastQueryVedioTime;		//上次查询视频列表时间
	bool m_bDownloadVedioThread;		//是否存在下载视频线程
	bool m_bDownloadVedioing;			//是否正在下载视频
	thread m_threadDownloadVedio;		//下载视频线程
};