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
#define ALARM_TYPE_TEMPERATUREHF  177			//温度预警恢复
#define ALARM_TYPE_HUMILITYHF	178				//湿度预警恢复

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

	string GetAlarmLevelType(BYTE type);

	string GetAlmLevel(BYTE type);

	string GetAlarmType(BYTE type, BYTE type1);
	string GetAlarmDesc(const StAlarmAndImgRec& data);
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


	///	回执
	void SendCallBackHeart(StHeartBeat315* pData);
	void SendCallBack0x41(StElecCurve* lpsubdata);
	void SendCallBack0x25(StOilPreCurve* lpsubdata);
	void SendCallBack0x27(StAlarmAndImgInfo* lpsubdata);

	void ParseDaoChaNameByZZJName(const string& sZZJName, string& sDc);
	virtual void OnRecvData_TCPClient(unsigned char* pData, size_t len, tcpSessionClt* connInfo) override;

	//
	void Do_CMD_CODE_YYQX(LPVOID pData);
	void Do_CMD_CODE_GAPVAL(LPVOID pData);
	void Do_CMD_CODE_YWINFO(LPVOID pData);
	std::map<int, string> m_mapSIDToName;
};