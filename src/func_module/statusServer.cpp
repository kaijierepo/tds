#include "StatusServer.h"
#include "statusServer.h"
#include "database/tDatabase.h"
#include <thread>
#include "common.h"
#include <chrono>

#ifdef _WIN32
#include <tchar.h>
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
	m_ipmiMonitor = nullptr;
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

	m_diskIOMonitor = std::make_shared<ProcessDiskIOMonitor>();
	m_ipmiMonitor = std::make_shared<IpmiMonitor>();

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
		std::this_thread::sleep_for(std::chrono::milliseconds(300));

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

			//disk io
			string sDiskIo;
			try {
				double readBytes = 0, writeBytes = 0, totalBytes = 0;
				ULONGLONG readOps = 0, writeOps = 0, otherOps = 0;
				ULONGLONG readBytesCumu = 0, writeBytesCumu = 0, otherBytesCumu = 0;
				if (m_diskIOMonitor 
					&& m_diskIOMonitor->GetIOStats(readBytes, writeBytes, totalBytes)
					&& m_diskIOMonitor->GetCumulativeIOCounters(readOps, writeOps, otherOps, readBytesCumu, writeBytesCumu, otherBytesCumu)) {
					//	KB/s
					readBytes /= 1024.0;
					writeBytes /= 1024.0;
					totalBytes /= 1024.0;

					auto mut_doc = yyjson_mut_doc_new(NULL);
					auto mut_root = yyjson_mut_obj(mut_doc);
					yyjson_mut_doc_set_root(mut_doc, mut_root);

					yyjson_mut_obj_add_real(mut_doc, mut_root, "readBytes", readBytes);
					yyjson_mut_obj_add_real(mut_doc, mut_root, "writeBytes", writeBytes);
					yyjson_mut_obj_add_real(mut_doc, mut_root, "totalBytes", totalBytes);

					yyjson_mut_obj_add_int(mut_doc, mut_root, "readOps", readOps);
					yyjson_mut_obj_add_int(mut_doc, mut_root, "writeOps", writeOps);
					yyjson_mut_obj_add_int(mut_doc, mut_root, "readBytesCumu", readBytesCumu);
					yyjson_mut_obj_add_int(mut_doc, mut_root, "writeBytesCumu", writeBytesCumu);

					char* temp = yyjson_mut_write(mut_doc, 0, 0);
					yyjson_mut_doc_free(mut_doc);
					if (temp)
					{
						sDiskIo = temp;
						free(temp);
					}
				}
			}
			catch (const std::exception& e) {
				std::cerr << "����: " << e.what() << std::endl;
			}

			//get handle count
			m_srvStatus.handle = GetProcHandleCount(processHandle);

			//get thread count 
			m_srvStatus.thread = get_thread_amount();

			// IPMI sensor data
			string sIpmi;
			if (m_ipmiMonitor && m_ipmiMonitor->available()) {
				std::vector<IPMI_SENSOR> sensors;
				if (m_ipmiMonitor->query(sensors)) {
					auto mut_doc = yyjson_mut_doc_new(NULL);
					auto mut_root = yyjson_mut_obj(mut_doc);
					yyjson_mut_doc_set_root(mut_doc, mut_root);

					auto mut_arr = yyjson_mut_arr(mut_doc);
					for (std::vector<IPMI_SENSOR>::iterator it = sensors.begin(); it != sensors.end(); ++it) {
						auto mut_s = yyjson_mut_obj(mut_doc);
						yyjson_mut_obj_add_str(mut_doc, mut_s, "name", it->name.c_str());
						yyjson_mut_obj_add_real(mut_doc, mut_s, "value", it->value);
						yyjson_mut_obj_add_str(mut_doc, mut_s, "unit", it->unit.c_str());
						yyjson_mut_obj_add_str(mut_doc, mut_s, "status", it->status.c_str());
						yyjson_mut_arr_append(mut_arr, mut_s);
					}
					yyjson_mut_obj_add_val(mut_doc, mut_root, "sensors", mut_arr);

					char* temp = yyjson_mut_write(mut_doc, 0, 0);
					yyjson_mut_doc_free(mut_doc);
					if (temp) {
						sIpmi = temp;
						free(temp);
					}
				}
			}

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

				ssdb->Insert("diskio", sDiskIo, &dbt);

				if (!sIpmi.empty()) {
					ssdb->Insert("ipmi", sIpmi, &dbt);
				}
				
				for (auto& iter : m_netStatus) {
					string portId = str::format("port_%d_send", iter.first);
					ssdb->Insert(portId, iter.second->send.load(), &dbt);
					iter.second->send = 0;
					portId = str::format("port_%d_recv", iter.first);
					ssdb->Insert(portId, iter.second->recv.load(), &dbt);
					iter.second->recv = 0;
				}
				ssdb->Insert("webReqCount", m_srvStatus.webReqCount.load(), &dbt);
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

	//��ȡ����ִ��ʱ��
	FILETIME ftCreation, ftExit, ftKernel, ftUser;
	ULARGE_INTEGER ulKernel, ulUser;
	GetProcessTimes(processHandle, &ftCreation, &ftExit, &ftKernel, &ftUser);
	ulKernel.LowPart = ftKernel.dwLowDateTime;
	ulKernel.HighPart = ftKernel.dwHighDateTime;
	ulUser.LowPart = ftUser.dwLowDateTime;
	ulUser.HighPart = ftUser.dwHighDateTime;
	cui.processTime = ulKernel.QuadPart + ulUser.QuadPart; //��λ100����

	//��ȡcpu��ִ��ʱ��
	HMODULE hNtDll = GetModuleHandle("ntdll.dll");
	NtQuerySystemInformationPtr NtQuerySystemInformation = (NtQuerySystemInformationPtr)GetProcAddress(hNtDll, "NtQuerySystemInformation");
	SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION cpuInfo[64]; // ���֧��64��CPU����
	ULONG returnLength;
	NTSTATUS status = NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS::SystemProcessorPerformanceInformation, cpuInfo, sizeof(cpuInfo), &returnLength); // 8��ʾSystemProcessorPerformanceInformation
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

		cui.totalTime = totalIdleTime + totalKernelTime + totalUserTime; //��λ100����
		cui.totalTime = cui.totalTime / (numCores / m_physicalCoreCount);
	}


	return cui;
}

double StatusServer::calcCpuUse()
{

	ULONGLONG processDiff = m_currentCpuUseInfo.processTime - m_lastCpuUseInfo.processTime;
	ULONGLONG totalDiff = m_currentCpuUseInfo.totalTime - m_lastCpuUseInfo.totalTime;

	if (totalDiff == 0) {
		return 0.0;
	}

	double cpuUsed = 100* static_cast<double>(processDiff) / static_cast<double>(totalDiff);
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

// ===== IpmiMonitor implementation (cross-platform) =====
#include <sstream>
#include <cstdlib>

IpmiMonitor::IpmiMonitor() : m_available(false) {
}

bool IpmiMonitor::exec(const char* cmd, std::string& output) {
	std::string redirectCmd = std::string(cmd);
#ifdef _WIN32
	redirectCmd += " 2>nul";
	FILE* fp = _popen(redirectCmd.c_str(), "r");
#else
	redirectCmd += " 2>/dev/null";
	FILE* fp = popen(redirectCmd.c_str(), "r");
#endif
	if (!fp) return false;
	char buf[512];
	while (fgets(buf, sizeof(buf), fp))
		output += buf;
#ifdef _WIN32
	_pclose(fp);
#else
	pclose(fp);
#endif
	return !output.empty();
}

bool IpmiMonitor::available() {
	if (m_available) return true;
	std::string out;
	if (!exec("ipmitool -I open sensor list", out))
		return false;
	m_available = !out.empty();
	return m_available;
}

bool IpmiMonitor::query(std::vector<IPMI_SENSOR>& out) {
	std::string output;
	if (!exec("ipmitool -I open sensor list", output))
		return false;

	std::istringstream ss(output);
	std::string line;
	while (std::getline(ss, line)) {
		if (line.empty()) continue;
		IPMI_SENSOR s = parseLine(line);
		if (!s.name.empty())
			out.push_back(s);
	}
	return !out.empty();
}

IPMI_SENSOR IpmiMonitor::parseLine(const std::string& line) {
	IPMI_SENSOR s;
	std::vector<std::string> parts;
	size_t pos = 0;
	while (pos < line.size()) {
		size_t end = line.find('|', pos);
		if (end == std::string::npos) end = line.size();
		std::string part = line.substr(pos, end - pos);
		// trim whitespace
		size_t first = part.find_first_not_of(" \t\r");
		if (first != std::string::npos) {
			size_t last = part.find_last_not_of(" \t\r");
			part = part.substr(first, last - first + 1);
		} else {
			part = "";
		}
		parts.push_back(part);
		if (end == line.size()) break;
		pos = end + 1;
	}

	if (parts.size() >= 4) {
		s.name = parts[0];
		s.value = atof(parts[1].c_str());
		s.unit = parts[2];
		s.status = parts[3];
	}
	return s;
}

