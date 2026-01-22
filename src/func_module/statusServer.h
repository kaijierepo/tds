#pragma once
#include <atomic>
#include <string>
#include <stdexcept>
#include <memory>

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

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <Psapi.h>
#include <Pdh.h>
#include <map>

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


class ProcessDiskIOMonitor {
private:
    PDH_HQUERY queryHandle;
    PDH_HCOUNTER readBytesCounter;
    PDH_HCOUNTER writeBytesCounter;
    PDH_HCOUNTER ioDataCounter;
    DWORD processId;
    std::string counterPath;

public:
    ProcessDiskIOMonitor() {
        processId = GetCurrentProcessId();
        queryHandle = NULL;

        try
        {
	        // 打开查询句柄
	        if (PdhOpenQuery(NULL, NULL, &queryHandle) != ERROR_SUCCESS) {
	            throw std::runtime_error("Failed to open PDH query");
	        }
	
	        //  
	        char processName[MAX_PATH];
	        GetModuleFileNameA(NULL, processName, MAX_PATH);
	        std::string name = std::string(processName);
	        size_t lastSlash = name.find_last_of("\\");
	        if (lastSlash != std::string::npos) {
	            name = name.substr(lastSlash + 1);
	        }
	        name = name.substr(0, name.find_last_of("."));
	
	        counterPath = "\\Process(" + name + ")\\IO Data Bytes/sec";
	        auto ret = PdhAddCounter(queryHandle, counterPath.c_str(), NULL, &ioDataCounter);
	        if (ret != ERROR_SUCCESS) {
	            // 如果失败，尝试带进程ID的路径
	            counterPath = "\\Process(" + name + "#" + std::to_string(processId) + ")\\IO Data Bytes/sec";
	            ret = PdhAddCounter(queryHandle, counterPath.c_str(), NULL, &ioDataCounter);
	            if (ret != ERROR_SUCCESS) {
	                throw std::runtime_error("Failed to add IO Data counter");
	            }
	        }
	
	        counterPath = "\\Process(" + name + ")\\IO Read Bytes/sec";
	        if (PdhAddCounter(queryHandle, counterPath.c_str(), NULL, &readBytesCounter) != ERROR_SUCCESS) {
	            counterPath = "\\Process(" + name + "#" + std::to_string(processId) + ")\\IO Read Bytes/sec";
	            if (PdhAddCounter(queryHandle, counterPath.c_str(), NULL, &readBytesCounter) != ERROR_SUCCESS) {
	                throw std::runtime_error("Failed to add IO Read counter");
	            }
	        }
	
	        counterPath = "\\Process(" + name + ")\\IO Write Bytes/sec";
	        if (PdhAddCounter(queryHandle, counterPath.c_str(), NULL, &writeBytesCounter) != ERROR_SUCCESS) {
	            counterPath = "\\Process(" + name + "#" + std::to_string(processId) + ")\\IO Write Bytes/sec";
	            if (PdhAddCounter(queryHandle, counterPath.c_str(), NULL, &writeBytesCounter) != ERROR_SUCCESS) {
	                throw std::runtime_error("Failed to add IO Write counter");
	            }
	        }
	
	        // 初始收集数据
	        PdhCollectQueryData(queryHandle);
        }
        catch (...)
        {
			if (queryHandle) {
				PdhCloseQuery(queryHandle);
			}
		}
    }

    ~ProcessDiskIOMonitor() {
        if (queryHandle) {
            PdhCloseQuery(queryHandle);
        }
    }

    bool GetIOStats(double& readBytes, double& writeBytes, double& totalBytes) {
		if (!queryHandle) return false;
        if (PdhCollectQueryData(queryHandle) != ERROR_SUCCESS) {
            return false;
        }

        PDH_FMT_COUNTERVALUE counterValue;

        if (PdhGetFormattedCounterValue(readBytesCounter, PDH_FMT_DOUBLE, NULL, &counterValue) == ERROR_SUCCESS) {
            readBytes = counterValue.doubleValue;
        }
        else {
            return false;
        }

        if (PdhGetFormattedCounterValue(writeBytesCounter, PDH_FMT_DOUBLE, NULL, &counterValue) == ERROR_SUCCESS) {
            writeBytes = counterValue.doubleValue;
        }
        else {
            return false;
        }

        if (PdhGetFormattedCounterValue(ioDataCounter, PDH_FMT_DOUBLE, NULL, &counterValue) == ERROR_SUCCESS) {
            totalBytes = counterValue.doubleValue;
        }
        else {
            return false;
        }

        return true;
    }

	bool GetCumulativeIOCounters(
		ULONGLONG& readOps,
		ULONGLONG& writeOps,
		ULONGLONG& otherOps,
		ULONGLONG& readBytes,
		ULONGLONG& writeBytes,
		ULONGLONG& otherBytes)
	{
		readOps = writeOps = otherOps = 0;
		readBytes = writeBytes = otherBytes = 0;

		//HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId);
		HANDLE hProc = GetCurrentProcess();

		IO_COUNTERS ioCounters;
		BOOL ok = GetProcessIoCounters(hProc, &ioCounters);

		if (!ok) {
			return false;
		}

		readOps = ioCounters.ReadOperationCount;
		writeOps = ioCounters.WriteOperationCount;
		otherOps = ioCounters.OtherOperationCount;
		readBytes = ioCounters.ReadTransferCount;
		writeBytes = ioCounters.WriteTransferCount;
		otherBytes = ioCounters.OtherTransferCount;

		return true;
	}
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

    std::shared_ptr<ProcessDiskIOMonitor> m_diskIOMonitor;

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