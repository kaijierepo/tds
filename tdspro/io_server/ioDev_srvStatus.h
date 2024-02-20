#pragma once
#include "ioDev.h"
#include "statusServer.h"


class ioDev_srvStatus : public ioDev
{
public:
	ioDev_srvStatus(void);
	~ioDev_srvStatus(void);


	unsigned char sessionHandle[4];

	//基本io设备管理
	bool run() override;
	void openProc();
	void stop() override;
	bool loadConf(json& conf) override;

	HANDLE processHandle;

	bool m_bCycleAcqThreadRunning;

	//TDS接口
	void output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync = true) override;

	bool isCommBusy() override;
	bool isConnected() override;
	DWORD GetProcHandleCount(HANDLE hProcess);
	void DoCycleTask();

	bool m_bLastCpuInfoValid;
	CPU_USE_INFO m_lastCpuUseInfo;
	CPU_USE_INFO m_currentCpuUseInfo;
	long long m_lastAcqTime;  //纳秒时间戳
	long long m_currentAcqTime;
	void getCpuUseInfo();
	bool calcCpuUse();
	double m_dbCpuUse;
};


