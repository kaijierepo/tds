#include "pch.h"
#include "dumpCatch.h"

#ifdef WINDOWS

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <Shlwapi.h>
#pragma comment(lib,"shlwapi.lib")

CDumpCatch dumpCatch;//全局虽然未使用但不能删除

void CDumpCatch::MyPureCallHandler(void)
{
	throw invalid_argument("");
}

void CDumpCatch::MyInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
{
	//The parameters all have the value NULL unless a debug version of the CRT library is used.
	throw invalid_argument("");
}

void CDumpCatch::SetInvalidHandle()
{
#if _MSC_VER >= 1400  // MSVC 2005/8
	m_preIph = _set_invalid_parameter_handler(MyInvalidParameterHandler);
#endif  // _MSC_VER >= 1400
	m_prePch = _set_purecall_handler(MyPureCallHandler);   //At application, this call can stop show the error message box.
}

void CDumpCatch::UnSetInvalidHandle()
{
#if _MSC_VER >= 1400  // MSVC 2005/8
	_set_invalid_parameter_handler(m_preIph);
#endif  // _MSC_VER >= 1400
	_set_purecall_handler(m_prePch); //At application this can stop show the error message box.
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI CDumpCatch::TempSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
	return NULL;
}

BOOL CDumpCatch::AddExceptionHandle()
{
	m_preFilter = ::SetUnhandledExceptionFilter(UnhandledExceptionFilterEx);
	PreventSetUnhandledExceptionFilter();
	return TRUE;
}

BOOL CDumpCatch::RemoveExceptionHandle()
{
	if (m_preFilter != NULL)
	{
		::SetUnhandledExceptionFilter(m_preFilter);
		m_preFilter = NULL;
	}
	return TRUE;
}

CDumpCatch::CDumpCatch()
{
	SetInvalidHandle();
	AddExceptionHandle();
}

CDumpCatch::~CDumpCatch()
{
	//UnSetInvalidHandle();
	//RemoveExceptionHandle();
}

#include <windows.h>  
#include <TlHelp32.h> 

struct PROC_INFO {
	HANDLE hProcess;
	DWORD ProcessId;
};

bool GetProcessIdFromName(string name, PROC_INFO& pi)
{
	bool ret = false;
	HANDLE  hsnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hsnapshot == INVALID_HANDLE_VALUE)
	{
		printf("CreateToolhelp32Snapshot Error!\n");
		return false;
	}

	PROCESSENTRY32 pe;
	pe.dwSize = sizeof(PROCESSENTRY32);

	int flag = Process32First(hsnapshot, &pe);

	while (flag != 0)
	{
		if (strcmp(pe.szExeFile, name.c_str()) == 0)
		{
			pi.ProcessId = pe.th32ProcessID;
			pi.hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);
			ret = true;
			break;
		}
		flag = Process32Next(hsnapshot, &pe);
	}

	CloseHandle(hsnapshot);

	return ret;
}


void CDumpCatch::createDump(string processName, string dumpFilePath)
{
	PROC_INFO pi;
	if (!GetProcessIdFromName(processName + ".exe", pi)) {
		return;
	}
	HANDLE hDumpFile = ::CreateFile(dumpFilePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile == INVALID_HANDLE_VALUE)
	{
		return;
	}
	BOOL bRet = ::MiniDumpWriteDump(pi.hProcess, pi.ProcessId, hDumpFile, MiniDumpWithFullMemory,nullptr, NULL, NULL);
	DWORD dwErr = GetLastError();
	::CloseHandle(hDumpFile);
	::CloseHandle(pi.hProcess);
}

BOOL CDumpCatch::ReleaseDumpFile(const std::string& strPath, EXCEPTION_POINTERS* pException)
{
	HANDLE hDumpFile = ::CreateFile(strPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
	dumpInfo.ExceptionPointers = pException;
	dumpInfo.ThreadId = ::GetCurrentThreadId();
	dumpInfo.ClientPointers = TRUE;
	BOOL bRet = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory, &dumpInfo, NULL, NULL);
	::CloseHandle(hDumpFile);
	return bRet;
}

void CDumpCatch::createDump(struct _EXCEPTION_POINTERS* pException)
{
	char szPath[MAX_PATH] = { 0 };
	::GetModuleFileName(NULL, szPath, MAX_PATH);
	std::string strexe = szPath;

	::PathRemoveFileSpec(szPath);
	std::string strPath = szPath;

	string dmpfile = strPath + "\\*.dmp";
	string strcmd = "del /s /q " + dmpfile;
	system(strcmd.c_str());

	TIME stNow;
	timeopt::now(&stNow);

	string strFile = str::format("%s %4d.%02d.%02d %02d-%02d-%02d.dmp", tds->getVersion().c_str(), stNow.wYear, stNow.wMonth, stNow.wDay, stNow.wHour, stNow.wMinute, stNow.wSecond);
	strFile = strPath + "\\" + strFile;

	HANDLE hDumpFile = ::CreateFile(strFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile == INVALID_HANDLE_VALUE)
	{
		return;
	}
	MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
	dumpInfo.ExceptionPointers = pException;
	dumpInfo.ThreadId = ::GetCurrentThreadId();
	dumpInfo.ClientPointers = TRUE;
	BOOL bRet = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory, &dumpInfo, NULL, NULL);
	::CloseHandle(hDumpFile);
	return;
}

LONG WINAPI CDumpCatch::UnhandledExceptionFilterEx(struct _EXCEPTION_POINTERS* pException)
{
	createDump(pException);
	//不使用自启动机制。内核态奔溃无法 进入此回调。还是用软件狗
	//ShellExecute(NULL, "open", strexe.c_str(), NULL, NULL, SW_SHOW);

	return EXCEPTION_EXECUTE_HANDLER;
}

BOOL CDumpCatch::PreventSetUnhandledExceptionFilter()
{
	HMODULE hKernel32 = LoadLibrary("kernel32.dll");
	if (hKernel32 == NULL)
	{
		return FALSE;
	}
	void* pOrgEntry = ::GetProcAddress(hKernel32, "SetUnhandledExceptionFilter");
	if (pOrgEntry == NULL)
	{
		return FALSE;
	}

	unsigned char newJump[5];
	DWORD dwOrgEntryAddr = (DWORD)pOrgEntry;
	dwOrgEntryAddr += 5;//jump instruction has 5 byte space

	void* pNewFunc = &TempSetUnhandledExceptionFilter;
	DWORD dwNewEntryAddr = (DWORD)pNewFunc;
	DWORD dwRelativeAddr = dwNewEntryAddr - dwOrgEntryAddr;

	newJump[0] = 0xE9;//jump
	memcpy(&newJump[1], &dwRelativeAddr, sizeof(DWORD));
	SIZE_T bytesWritten;
	DWORD dwOldFlag, dwTempFlag;
	::VirtualProtect(pOrgEntry, 5, PAGE_EXECUTE_READWRITE, &dwOldFlag);
	BOOL bRet = ::WriteProcessMemory(::GetCurrentProcess(), pOrgEntry, newJump, 5, &bytesWritten);
	::VirtualProtect(pOrgEntry, 5, dwOldFlag, &dwTempFlag);
	return bRet;
}

#endif