#ifdef _WIN32
#include "StatusServer.h"
#include <tchar.h>
#include <chrono>
#include "statusServer.h"
#include "tdb.h"
#include <thread>
#include "common.h"
#include "winternl.h"

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


int GetPhysicalCoreCount() {
	DWORD length = 0;
	GetLogicalProcessorInformation(nullptr, &length);
	std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
	if (!GetLogicalProcessorInformation(buffer.data(), &length)) {
		std::cerr << "Failed to get logical processor information." << std::endl;
		return -1;
	}

	int physicalCoreCount = 0;
	for (const auto& info : buffer) {
		if (info.Relationship == RelationProcessorCore) {
			physicalCoreCount++;
		}
	}

	return physicalCoreCount;
}

bool StatusServer::run()
{
	if (db.m_timeUnit != BY_DAY)
		return false;

	m_physicalCoreCount = GetPhysicalCoreCount();

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
			//current cpu used time
			m_currentCpuUseInfo = getCpuUseInfo();
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
				TDB* ssdb = db.getChildDB("serverStatus");

				if (m_bCurCpuInfoValid && m_bLastCpuInfoValid) {
					ssdb->Insert("cpu", dbt, m_srvStatus.cpu);
				}
				ssdb->Insert("mem", dbt, m_srvStatus.mem);
				ssdb->Insert("pageFile", dbt, m_srvStatus.pageFile);
				//ssdb->Insert("disk", dbt, m_srvStatus.disk);
				ssdb->Insert("handle", dbt, m_srvStatus.handle);
				
				for (auto& iter : m_netStatus) {
					string portId = str::format("port_%d_send", iter.first);
					ssdb->Insert(portId, dbt, iter.second->send);
					iter.second->send = 0;
					portId = str::format("port_%d_recv", iter.first);
					ssdb->Insert(portId, dbt, iter.second->recv);
					iter.second->recv = 0;
				}

				ssdb->Insert("webReqCount", dbt, m_srvStatus.webReqCount);
				m_srvStatus.webReqCount = 0;
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


typedef NTSTATUS(WINAPI* NtQuerySystemInformationPtr)(ULONG SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);

CPU_USE_INFO StatusServer::getCpuUseInfo()
{
	CPU_USE_INFO cui;

	//获取程序执行时间
	FILETIME ftCreation, ftExit, ftKernel, ftUser;
	ULARGE_INTEGER ulKernel, ulUser;
	GetProcessTimes(processHandle, &ftCreation, &ftExit, &ftKernel, &ftUser);
	ulKernel.LowPart = ftKernel.dwLowDateTime;
	ulKernel.HighPart = ftKernel.dwHighDateTime;
	ulUser.LowPart = ftUser.dwLowDateTime;
	ulUser.HighPart = ftUser.dwHighDateTime;
	cui.processTime = ulKernel.QuadPart + ulUser.QuadPart; //单位100纳秒

	//获取cpu总执行时间
	HMODULE hNtDll = GetModuleHandle("ntdll.dll");
	NtQuerySystemInformationPtr NtQuerySystemInformation = (NtQuerySystemInformationPtr)GetProcAddress(hNtDll, "NtQuerySystemInformation");
	SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION cpuInfo[64]; // 最多支持64个CPU核心
	ULONG returnLength;
	NTSTATUS status = NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS::SystemProcessorPerformanceInformation, cpuInfo, sizeof(cpuInfo), &returnLength); // 8表示SystemProcessorPerformanceInformation
	if (status == 0) {
		ULONGLONG totalIdleTime = 0;
		ULONGLONG totalKernelTime = 0;
		ULONGLONG totalUserTime = 0;

		int numCores = returnLength / sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION);

		for (int i = 0; i < numCores; i++) {
			totalIdleTime += cpuInfo[i].IdleTime.QuadPart;
			totalKernelTime += cpuInfo[i].KernelTime.QuadPart;
			totalUserTime += cpuInfo[i].UserTime.QuadPart;
		}

		cui.totalTime = totalIdleTime + totalKernelTime + totalUserTime; //单位100纳秒
		cui.totalTime = cui.totalTime / (numCores / m_physicalCoreCount);
	}


	return cui;
}

double StatusServer::calcCpuUse()
{
	double cpuUsed = 100* (m_currentCpuUseInfo.processTime - m_lastCpuUseInfo.processTime) / (m_currentCpuUseInfo.totalTime - m_lastCpuUseInfo.totalTime);
	return cpuUsed;
}

void StatusServer::statisSend(int port, size_t len) {
	map<int, NET_TRAFFIC*>::iterator iter = m_netStatus.find(port);
	if (iter == m_netStatus.end()) {
		NET_TRAFFIC* nt = new NET_TRAFFIC();
		nt->send += len;
		m_netStatus[port] = nt;
	}
	else {
		NET_TRAFFIC* nt = iter->second;
		nt->send += len;
	}
}
void StatusServer::statisRecv(int port, size_t len) {
	map<int, NET_TRAFFIC*>::iterator iter = m_netStatus.find(port);
	if (iter == m_netStatus.end()) {
		NET_TRAFFIC* nt = new NET_TRAFFIC();
		nt->recv += len;
		m_netStatus[port] = nt;
	}
	else {
		NET_TRAFFIC* nt = iter->second;
		nt->recv += len;
	}
}


void statisSend(int port, size_t len){
	statusSrv.statisSend(port, len);
}

#endif

