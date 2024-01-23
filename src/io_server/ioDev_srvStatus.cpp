#include "pch.h"
#include "ioDev_srvStatus.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"
#include "webSrv.h"
#include <windows.h>
#include <tlhelp32.h>
#include <Psapi.h>

ioDev* createDev_srvStatus()
{
	return new ioDev_srvStatus();
}

class createReg_srvStatus {
public:
	createReg_srvStatus();
};
createReg_srvStatus::createReg_srvStatus()
{
	mapDevCreateFunc["server-status"] = createDev_srvStatus;
	mapDevTypeLabel["server-status"] = "服务器状态";
}
createReg_srvStatus regSrvStatus;


ioDev_srvStatus::ioDev_srvStatus(void)
{
	m_devType = "server-status";
	m_bCycleAcqThreadRunning = false;
	m_level = "device";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_bLastCpuInfoValid = false;
}


ioDev_srvStatus::~ioDev_srvStatus(void)
{
}


HANDLE OpenProcessByName(const char* processName) {
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

bool ioDev_srvStatus::run()
{
	return true;
}

void ioDev_srvStatus::openProc() {
	string procName = getDevAddrStr().c_str();
	if (procName.find(".exe") == string::npos) {
		procName += ".exe";
	}
	processHandle = OpenProcessByName(procName.c_str());
}

void ioDev_srvStatus::stop()
{
	m_bRunning = false;  //设为false以后，应当不会有新的线程尝试占用通信锁

	//此处怎么设计找沈春种或者廖灰灰讨论一次。有没有基于信号量的方法
	while (m_bWorkingThreadRunning) {
		timeopt::sleepMilli(1);
	}
}



bool ioDev_srvStatus::loadConf(json& conf)
{
	return ioDev::loadConf(conf);
}

void ioDev_srvStatus::output(string chanAddr, json jVal, json& rlt,json& err, bool sync)
{

}

void ioDev_srvStatus::output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync)
{


}

bool ioDev_srvStatus::isCommBusy()
{
	assert(m_pParent);
	return m_pParent->isCommBusy();
}

bool ioDev_srvStatus::isConnected()
{
	if (m_tcpClt) {
		return m_tcpClt->IsConnect();
	}
	return false;
}

void cycleAcq_thread_srvStatus(ioDev_srvStatus* pDev) {

	if (pDev->processHandle == nullptr) {
		pDev->openProc();
	}

	if (pDev->processHandle == nullptr) {
		goto TASK_THREAD_END;
	}


	//cpu
	pDev->getCpuUseInfo();
	if (pDev->calcCpuUse()) {
		ioChannel* pC = pDev->getChanByDevAddr("cpu");
		if (pC) {
			pC->input(pDev->m_dbCpuUse);
		}
	}
	pDev->m_lastCpuUseInfo = pDev->m_currentCpuUseInfo;
	pDev->m_lastAcqTime = pDev->m_currentAcqTime;
	pDev->m_bLastCpuInfoValid = true;

	//memory
	PROCESS_MEMORY_COUNTERS_EX pmc;
	if (GetProcessMemoryInfo(pDev->processHandle, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
		double mem = pmc.PrivateUsage / (1024 * 1024); // 内存占用，单位MB
		ioChannel* pC = pDev->getChanByDevAddr("mem");
		if (pC) {
			pC->input(mem);
		}
	}

	//handle
	{
		ioChannel* pC = pDev->getChanByDevAddr("handle");
		if (pC) {
			int handle = pDev->GetProcHandleCount(pDev->processHandle);
			pC->input(handle);
		}
	}

	CloseHandle(pDev->processHandle);
	pDev->processHandle = nullptr;

TASK_THREAD_END:
	pDev->m_bCycleAcqThreadRunning = false;
}

DWORD ioDev_srvStatus::GetProcHandleCount(HANDLE hProcess)
{
	DWORD handleCount;
	if (!GetProcessHandleCount(hProcess, &handleCount))
	{
		// 处理错误情况
		return -1;
	}
	return handleCount;
}


void ioDev_srvStatus::DoCycleTask()
{
	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning)
		{
			// 任务触发线程， 执行线程， 标记位  3者的逻辑关系
			//由任务触发线程将标志位置为true,可以保证线程一定不会被触发产生2个。线程退出标志位才置为false
			//不要将 标志位置为true 放在线程里面执行。这样逻辑上触发线程可能触发2个线程出来。如果执行线程还没有置位，触发线程已经判断为false
			//这样就会产生2个执行线程实例
			m_bCycleAcqThreadRunning = true;
			thread t(cycleAcq_thread_srvStatus, this);
			t.detach();
			timeopt::now(&m_stLastAcqTime);
		}
	}
	return;
}


#include <chrono>

long long getTimestamp_ns() {
	std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
	std::chrono::nanoseconds timestamp = now.time_since_epoch();

	// 将纳秒级别的时间戳转换为整数
	return timestamp.count();
}


void ioDev_srvStatus::getCpuUseInfo()
{
	FILETIME createTime, exitTime, kernelTime, userTime;
	m_currentAcqTime = getTimestamp_ns();
	GetProcessTimes(processHandle, &createTime, &exitTime, &kernelTime, &userTime);
	m_currentCpuUseInfo.KernelTime.LowPart = kernelTime.dwLowDateTime;
	m_currentCpuUseInfo.KernelTime.HighPart = kernelTime.dwHighDateTime;
	m_currentCpuUseInfo.UserTime.LowPart = userTime.dwLowDateTime;
	m_currentCpuUseInfo.UserTime.HighPart = userTime.dwHighDateTime;
}

bool ioDev_srvStatus::calcCpuUse()
{
	if (m_bLastCpuInfoValid) {
		double msUsed = 100*((m_currentCpuUseInfo.KernelTime.QuadPart - m_lastCpuUseInfo.KernelTime.QuadPart) + (m_currentCpuUseInfo.UserTime.QuadPart - m_lastCpuUseInfo.UserTime.QuadPart));
		double msPassed = m_currentAcqTime - m_lastAcqTime;
		m_dbCpuUse = (msUsed / msPassed) * 100;
		return true;
	}
	return false;
}


