#pragma once
#include "ioDev.h"


struct SRV_STATUS {
	double cpu; //%
	double mem; //mb
	double disk; //mb/s
	double net; //Mbps
	int handle;
	int thread;
};


struct CPU_USE_INFO {
	ULARGE_INTEGER KernelTime;
	ULARGE_INTEGER UserTime;
};



class StatusServer
{
public:
	StatusServer(void);
	~StatusServer(void);


	unsigned char sessionHandle[4];

	HANDLE OpenProcessByName(const char* processName);

	bool run();
	void openProc();
	void stop();
	time_t SysTime2Unix(SYSTEMTIME sDT);
	time_t CalcTimePassSecond(SYSTEMTIME lastTime);
	void cycleAcq_srvStatus();
	long long  getTimestamp_ns();
	int get_thread_amount();
	BOOL EnableDebugPrivilege(BOOL fEnable);
	DWORD GetProcHandleCount(HANDLE hProcess);
	HANDLE processHandle;

	bool m_bCycleAcqThreadRunning;

	SYSTEMTIME m_stLastAcqTime;  
	float m_fAcqInterval; 

	bool m_bLastCpuInfoValid;
	bool m_bCurCpuInfoValid;
	CPU_USE_INFO m_lastCpuUseInfo;
	CPU_USE_INFO m_currentCpuUseInfo;
	long long m_lastAcqTime;  //time stamp in nano second
	long long m_currentAcqTime;
	CPU_USE_INFO getCpuUseInfo();
	double calcCpuUse();
	double m_dbCpuUse;

	SRV_STATUS m_srvStatus;
};

extern StatusServer statusSrv;
