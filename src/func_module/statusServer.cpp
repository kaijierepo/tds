#ifdef _WIN32
#include "StatusServer.h"
#include <tchar.h>
#include <chrono>
#include "statusServer.h"
#include "tdb.h"
#include <thread>

#pragma comment(lib, "Pdh.lib")
#pragma comment(lib, "Psapi.lib")

long long  StatusServer::getTimestamp_ns() {
	std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
	std::chrono::nanoseconds timestamp = now.time_since_epoch();
	return timestamp.count();
}
StatusServer statusSrv;


StatusServer::StatusServer(void)
{
	m_bCycleAcqThreadRunning = false;
	m_bLastCpuInfoValid = false;
	m_fAcqInterval = 60;
	m_bCurCpuInfoValid = false;
	processHandle = INVALID_HANDLE_VALUE;
	memset(&m_stLastAcqTime, 0, sizeof(m_stLastAcqTime));
	m_bLogStatus = false;
	m_logInterval = 60;
}


StatusServer::~StatusServer(void)
{
}


HANDLE StatusServer::OpenProcessByName(const char* processName) {
	HANDLE hProcess = NULL;
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
		if (Process32First(hSnapshot, &entry)) {
			do {
				if (strcmp(entry.szExeFile, processName) == 0) {
					hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
					break;
				}
			} while (Process32Next(hSnapshot, &entry));
		}
		CloseHandle(hSnapshot);
	}

	return hProcess;
}

void cycleAcq_thread_srvStatus(StatusServer* pss) {
	pss->cycleAcq_srvStatus();
}

bool StatusServer::run()
{
	if (db.m_timeUnit != BY_DAY)
		return false;
	thread t(cycleAcq_thread_srvStatus, this);
	t.detach();
	return true;
}

void StatusServer::openProc() {
	string str;
	TCHAR p[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, p, MAX_PATH);
	string strPath = (char*)p;
	size_t nEnd = strPath.rfind('\\');
	str = strPath.substr(nEnd + 1, strPath.length() - nEnd - 1);
	//str = charCodec::gb_to_tds(str);
	string procName = str;
	processHandle = OpenProcessByName(procName.c_str());
}

void StatusServer::stop()
{

}


time_t StatusServer::SysTime2Unix(SYSTEMTIME sDT)
{
	tm temptm = { sDT.wSecond, sDT.wMinute, sDT.wHour,
		sDT.wDay, sDT.wMonth - 1, sDT.wYear - 1900, sDT.wDayOfWeek, 0, 0 };
	time_t iReturn = mktime(&temptm);
	return iReturn;
}

time_t StatusServer::CalcTimePassSecond(SYSTEMTIME lastTime)
{
	time_t last = SysTime2Unix(lastTime);
	time_t now = time(NULL);
	time_t milli = now - last;
	return milli;
}

void StatusServer::cycleAcq_srvStatus() {
	while (1) {
		Sleep(300);

		if (CalcTimePassSecond(m_stLastAcqTime) > m_logInterval){
			if (processHandle == INVALID_HANDLE_VALUE){
				openProc();
			}


			if (processHandle == INVALID_HANDLE_VALUE) {
				continue;
			}

			//get cpu
			//last cpu used time
			m_lastCpuUseInfo = m_currentCpuUseInfo;
			m_bLastCpuInfoValid = m_bCurCpuInfoValid;
			m_lastAcqTime = m_currentAcqTime;
			//current cpu used time
			m_currentCpuUseInfo = getCpuUseInfo();
			m_currentAcqTime = getTimestamp_ns();
			m_bCurCpuInfoValid = true;
		
			if (m_bCurCpuInfoValid && m_bLastCpuInfoValid) { 
				m_srvStatus.cpu = calcCpuUse();
			}


			//get mem
			PROCESS_MEMORY_COUNTERS_EX pmc;
			if (GetProcessMemoryInfo(processHandle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
				m_srvStatus.mem = (double)pmc.PrivateUsage / (double)(1024 * 1024); // unit MB
				m_srvStatus.pageFile = (double)pmc.PagefileUsage / (double)(1024 * 1024); // unit MB;
			}

			//get handle count
			m_srvStatus.handle = GetProcHandleCount(processHandle);

			//get thread count 
			m_srvStatus.thread = get_thread_amount();

			GetLocalTime(&m_stLastAcqTime);

			
			if (m_bLogStatus) {
				DB_TIME dbt; dbt.setNow();
				if (m_bCurCpuInfoValid && m_bLastCpuInfoValid) {
					db.Insert("serverStatus.cpu", dbt, m_srvStatus.cpu);
				}
				db.Insert("serverStatus.mem", dbt, m_srvStatus.mem);
				db.Insert("serverStatus.pageFile", dbt, m_srvStatus.pageFile);
				//db.Insert("serverStatus.net", dbt, m_srvStatus.net);
				//db.Insert("serverStatus.disk", dbt, m_srvStatus.disk);
				db.Insert("serverStatus.handle", dbt, m_srvStatus.handle);
			}
		}
	}
}


int StatusServer::get_thread_amount()
{
	int i = 0;
	char Buff[9] = "";
	PROCESSENTRY32 pe32 = { 0 };
	pe32.dwSize = sizeof(pe32);

	int processid = GetCurrentProcessId();
	HANDLE hProcessSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		printf("CreateToolhelp32Snapshot() failed. error code:%d.\n", GetLastError());
		return 0;
	}
	BOOL bMore = ::Process32First(hProcessSnap, &pe32);

	int iReturn = 0;

	while (bMore)
	{
		if (pe32.th32ProcessID == processid)
		{
			iReturn = pe32.cntThreads;
			goto CommonExit;
		}

		bMore = Process32Next(hProcessSnap, &pe32);
		i++;
	}
CommonExit:

	CloseHandle(hProcessSnap);

	return iReturn;
}


DWORD StatusServer::GetProcHandleCount(HANDLE hProcess)
{
	DWORD handleCount;
	if (!GetProcessHandleCount(hProcess, &handleCount))
	{
		return -1;
	}
	return handleCount;
}


CPU_USE_INFO StatusServer::getCpuUseInfo()
{
	CPU_USE_INFO cui;
	FILETIME createTime, exitTime, kernelTime, userTime;
	GetProcessTimes(processHandle, &createTime, &exitTime, &kernelTime, &userTime);
	cui.KernelTime.LowPart = kernelTime.dwLowDateTime;
	cui.KernelTime.HighPart = kernelTime.dwHighDateTime;
	cui.UserTime.LowPart = userTime.dwLowDateTime;
	cui.UserTime.HighPart = userTime.dwHighDateTime;
	return cui;
}

double StatusServer::calcCpuUse()
{
	double msUsed = 100*((m_currentCpuUseInfo.KernelTime.QuadPart - m_lastCpuUseInfo.KernelTime.QuadPart) + (m_currentCpuUseInfo.UserTime.QuadPart - m_lastCpuUseInfo.UserTime.QuadPart));
	double msPassed = m_currentAcqTime - m_lastAcqTime;
	double cpuUsed = (msUsed / msPassed) * 100;
	return cpuUsed;
}

#endif

