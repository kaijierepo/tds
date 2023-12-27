#pragma once
#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"
#include "Parse315Protocol.h"


class ioDev_dcqk : public ioDev
{
public:
	ioDev_dcqk();
	~ioDev_dcqk();

	void DoAcq();
	void DoCycleTask() override;
	void onEvent_online() override;

	virtual bool onRecvPkt(json jPkt) override;
	virtual bool onRecvPkt(unsigned char* pData, size_t iLen) override;
	virtual bool onRecvData(unsigned char* pData, size_t iLen) override;

	int DealJHDData(LPVOID lpParam);

	string GetAlmType(BYTE type);

	string GetAlmLevel(BYTE type);

	string GetAlarmDesc(BYTE type);

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


	std::map<int, string> m_mapSIDToName;
};