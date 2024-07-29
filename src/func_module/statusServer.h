#ifdef _WIN32
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <Psapi.h>
#include <Pdh.h>
#include <atomic>
#include <map>

struct SRV_STATUS {
	double cpu; //%
	double mem; //mb
	double pageFile;
	double disk; //mb/s
	double net; //Mbps
	int handle;
	int thread;
	std::atomic<long long> webReqCount;
};


struct NET_TRAFFIC {
	std::atomic<long long> send;
	std::atomic<long long> recv;

	NET_TRAFFIC() {
		send = 0;
		recv = 0;
	}
};


struct CPU_USE_INFO {
	ULONGLONG processTime;
	ULONGLONG totalTime;
};



class StatusServer
{
public:
	StatusServer(void);
	~StatusServer(void);

	bool run();
	bool m_bLogStatus;
	int m_logInterval;
	int m_physicalCoreCount;

	unsigned char sessionHandle[4];
	HANDLE OpenProcessByName(const char* processName);
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
	std::map<int, NET_TRAFFIC*> m_netStatus;
	NET_TRAFFIC m_wsNetStatus;

	void statisSend(int port, size_t len);
	void statisRecv(int port, size_t len);
};

extern StatusServer statusSrv;

void statisSend(int port, size_t len);
#endif