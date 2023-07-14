#ifdef _WIN32
#include "pch.h"
#include "tdsWatchDog.h"
#include "logger.h"
#include "httplib.h"
#include "dumpCatch.h"
#include "common.h"
#include <psapi.h>
#include <TlHelp32.h>
#include <Windows.h>
#include <shellapi.h>

#include <Winternl.h>

typedef NTSTATUS(NTAPI* _NtQueryInformationProcess)(
	HANDLE ProcessHandle,
	DWORD ProcessInformationClass,
	PVOID ProcessInformation,
	DWORD ProcessInformationLength,
	PDWORD ReturnLength
	);

/*
写成一个函数，来获得所有的PEB结构体信息
*/
TCHAR* GetProcessCommandLine(HANDLE hProcess)
{
	UNICODE_STRING commandLine;
	TCHAR* commandLineContents = NULL;
	_NtQueryInformationProcess NtQuery = (_NtQueryInformationProcess)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
	if (NtQuery)
	{
		PROCESS_BASIC_INFORMATION pbi;
		NTSTATUS isok = NtQuery(hProcess, ProcessBasicInformation, &pbi, sizeof(PROCESS_BASIC_INFORMATION), NULL);
		if (NT_SUCCESS(isok))
		{
			PEB peb;
			RTL_USER_PROCESS_PARAMETERS upps;
			PVOID rtlUserProcParamsAddress;
			if (ReadProcessMemory(hProcess, &(((_PEB*)pbi.PebBaseAddress)->ProcessParameters), &rtlUserProcParamsAddress, sizeof(PVOID), NULL))
			{
				if (ReadProcessMemory(hProcess,
					&(((_RTL_USER_PROCESS_PARAMETERS*)rtlUserProcParamsAddress)->CommandLine),
					&commandLine, sizeof(commandLine), NULL))
				{
					commandLineContents = (TCHAR*)malloc(commandLine.Length + sizeof(TCHAR));
					memset(commandLineContents, 0, commandLine.Length + sizeof(TCHAR));
					ReadProcessMemory(hProcess, commandLine.Buffer,
						commandLineContents, commandLine.Length, NULL);
				}
			}
		}
	}

	return commandLineContents;
}



#pragma comment(lib, "version.lib")

tdsWatchDog watchDog;

tdsWatchDog::tdsWatchDog()
{
	if (fs::appName() == "tdsd") {
		HANDLE m_hMutex = CreateMutex(NULL, FALSE, "tdsWatchDog");
		if (GetLastError() == ERROR_ALREADY_EXISTS) {
			CloseHandle(m_hMutex);
			m_hMutex = NULL;
			exit(0);
		}
	}
	m_curVer = watchDog.getCurTdsVer();
}

void wakeUpFeeder() {
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	watchDog.m_curVer = watchDog.getCurTdsVer();

	// Start the child process.
	//si.dwFlags = STARTF_USESHOWWINDOW;
	//si.wShowWindow = SW_SHOW;
	if (!CreateProcessW(NULL,   // No module name (use command line)
		(LPWSTR)charCodec::utf8_to_utf16("tds.exe").c_str(),        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_NEW_CONSOLE,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		watchDog.log("启动TDS失败!错误信息:" + sys::getLastError() + ",版本:" + watchDog.m_curVer);
	}
	else
	{
		watchDog.log("启动TDS成功!启动时间: " + timeopt::nowStr() + ",版本:" + watchDog.m_curVer);
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}


void thread_checkMediaServer() {
	while (1) {
		timeopt::sleepMilli(1000);
		if (!watchDog.isProcessRun("MediaServer.exe")) {
			string msPath = fs::appPath() + "/com/mediaServer/MediaServer.exe";
			if (fs::fileExist(msPath)){
				watchDog.runProcess(msPath);
			}
		}
	}
}


bool IsNodeRunningWithArg(const std::string& arg) {

	//string arg = str::replace(argSrc, "/", "\\");

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		std::cerr << "CreateToolhelp32Snapshot failed, " << GetLastError() << std::endl;
		return false;
	}

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(pe32);
	if (!Process32First(hSnapshot, &pe32)) {
		std::cerr << "Process32First failed, " << GetLastError() << std::endl;
		CloseHandle(hSnapshot);
		return false;
	}

	do {
		if (strcmp(pe32.szExeFile, "node.exe") == 0) {
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
			if (hProcess != NULL) {
				
				WCHAR* pszProcessCmd = (WCHAR*) GetProcessCommandLine(hProcess);
				char* pCmd = (char*)pszProcessCmd;

				wstring utf16Str;
				int iWchar = 0;
				int iChar = 0;
				while (pCmd[iChar] != 0) {//此处内存数据是utf16编码，但是结束字符是1个0，直接赋值给 wstring 会有乱码。
					utf16Str.push_back(pszProcessCmd[iWchar]);
					iWchar++;
					iChar += 2;
				}

				string s = charCodec::utf16_to_utf8(utf16Str);

				if (s.find(arg) != string::npos) {
					return true;
				}
			}
		}
	} while (Process32Next(hSnapshot, &pe32));

	CloseHandle(hSnapshot);
	return false;
}

void thread_checkTcp2com() {
	while (1) {
		timeopt::sleepMilli(1000);
		if (!watchDog.isProcessRun("tcp2com.exe")) {
			string msPath = fs::appPath() + "/tcp2com.exe";
			if (fs::fileExist(msPath)) {
				watchDog.runProcess(msPath);
			}
		}
	}
}

void thread_checkSrcMain() {
	string srcMain = fs::appPath() + "/src/main.js";

	while (1) {
		timeopt::sleepMilli(1000);

		if (fs::fileExist(srcMain) && !IsNodeRunningWithArg(srcMain))
		{
			string cmdline = fs::appPath() + "/node.exe " + srcMain;
			ShellExecute(NULL, "open", "cmd.exe", ("/C " + charCodec::utf8_to_gb(cmdline)).c_str(), NULL, SW_SHOW);
			LOG("启动Nodejs服务:" + cmdline);
			Sleep(2000);
		}
	}
}

void thread_checkAdp() {
	vector<fs::FILE_INFO> adpList;
	string adpRoot = fs::appPath() + "/com/adp/";
	if (!fs::fileExist(adpRoot)) {
		return;
	}

	string adpDevicePath = fs::appPath() + "/com/adp/adp-device";
	string adpGatewayPath = fs::appPath() + "/com/adp/adp-gateway";


	while (1) {
		timeopt::sleepMilli(1000);
		adpList.clear();
		fs::getFolderList(adpList, adpDevicePath);
		fs::getFolderList(adpList, adpGatewayPath);

		for (int i = 0; i < adpList.size(); i++) {
			string adpExe =  adpList[i].path + "/main.js";
			//程序存在且没有运行
			if (fs::fileExist(adpExe) &&  !IsNodeRunningWithArg(adpExe))
			{
				string cmdline = fs::appPath() + "/com/adp/node.exe " + adpExe;
				ShellExecute(NULL, "open", "cmd.exe" , ("/C " + charCodec::utf8_to_gb(cmdline)).c_str(), NULL, SW_SHOW);
				LOG("接入适配器启动:" + cmdline);
			}
		}
	}
}



void thread_checkFood() {
	//timeopt::now(&watchDog.m_lastFeedTime);
	//timeopt::now(&watchDog.m_lastUpdateCheckTime);

	string tdsPath = fs::appPath() + "/tds.exe";
	if (!fs::fileExist(tdsPath)) {
		return;
	}

	watchDog.m_conf.load("./tds.ini");
	watchDog.m_tdsAddr = "http://127.0.0.1:" + watchDog.m_conf.getValStr("httpPort", "667");
	LOG("TDS服务地址: " + watchDog.m_tdsAddr);

	while (1)
	{
		timeopt::sleepMilli(2000);
		//int pass = timeopt::CalcTimePassMilliSecond(watchDog.m_lastFeedTime);


		//int updateCheckPass = timeopt::CalcTimePassMilliSecond(watchDog.m_lastUpdateCheckTime);
		//if (updateCheckPass > 1000) {
		//	string newRev = watchDog.getUpdateTdsVer();
		//	if (newRev != "" && newRev > watchDog.m_curVer) {
		//		watchDog.log("发现新版本:" + newRev + ",当前版本:" + watchDog.m_curVer);

		//		WinExec("taskkill /f /im tds.exe /t", SW_SHOW);//关闭可能处于卡死状态的程序。如果启动了多个实例，该命令可以同时关闭多个。
		//		WinExec("taskkill /f /im WerFault.exe /t", SW_SHOW);//某些操作系统如windows server 2008 R2 enterprize 会出现该程序，
		//		timeopt::sleepMilli(200);
		//		try {
		//			filesystem::copy(charCodec::utf8_to_utf16(fs::appPath() + "/update/tds.exe"), charCodec::utf8_to_utf16(fs::appPath() + "/tds.exe"), std::filesystem::copy_options::overwrite_existing);
		//		}
		//		catch (std::exception& e)
		//		{
		//			string err = e.what();
		//			watchDog.log(err);
		//		}
		//		
		//		wakeUpFeeder();
		//		timeopt::sleepMilli(5000);
		//	}
		//	timeopt::now(&watchDog.m_lastUpdateCheckTime);
		//}


		//LOG("[keyinfo]wait food for " + str::fromInt(pass));

		string addr = watchDog.m_tdsAddr;

		httplib::Client cli(addr);
		cli.set_connection_timeout(2);
		cli.set_read_timeout(3);
		httplib::Params params;
		json jReq;
		jReq["method"] = "getDevInfo";
		jReq["id"] = "tdsd";
		jReq["params"] = json::object();
		TIME start = timeopt::now();
		auto res = cli.Post("/debug", jReq.dump().c_str(), "application/json; charset=utf-8");
		time_t pass = timeopt::CalcTimePassMilliSecond(start);
		//watchDog.log("get food,take millisecond:" + str::fromInt(pass));
		if (res == nullptr) {
			watchDog.log("没有检测到活动的服务,重启服务,rpc请求超时返回毫秒数:" + str::fromInt(pass));
			if (watchDog.isProcessRun("tds.exe")) {
				string path = "./dump_Ver_" + watchDog.m_curVer + "_Catch_" + timeopt::nowStrForFile() + ".dmp";
				CDumpCatch::createDump("tds", path);
				watchDog.log("tds运行中，但不响应请求，截取dump:" + path);

				//名称是时间，从老到新排列
				vector<fs::FILE_INFO> fileList;
				fs::getFileList(fileList, fs::appPath(), false, false, ".dmp");
				std::map<string, fs::FILE_INFO> mapList;
				for (int i = 0; i < fileList.size(); i++) {
					mapList[fileList[i].name] = fileList[i];
				}

				while (mapList.size() > 2) {
					fs::deleteFile(mapList.begin()->second.path);
					mapList.erase(mapList.begin());
				}
			}

			//watchDog.log("准备启动tds,执行 taskkill /f /im tds.exe /t 关闭现有实例");
			WinExec("taskkill /f /im tds.exe /t", SW_SHOW);//关闭可能处于卡死状态的程序。如果启动了多个实例，该命令可以同时关闭多个。
			WinExec("taskkill /f /im WerFault.exe /t", SW_SHOW);//某些操作系统如windows server 2008 R2 enterprize 会出现该程序，
			//就是一个对话框显示 tds.exe 已停止工作。联机检查解决方案并关闭程序  按钮  和  关闭程序 按钮
			timeopt::sleepMilli(200);
			wakeUpFeeder();
			timeopt::sleepMilli(5000);
		}
		else {
			json j = json::parse(res->body);
			if (j.contains("result")) {
				json rlt = j["result"];
				if (rlt.contains("softVer")) {
					watchDog.m_curVer = rlt["softVer"];
				}
			}
		}
	}
}


void addAutoStart() {

}

void tdsWatchDog::run()
{
	string appName = fs::appName();
	watchDog.log(appName + " 服务守护程序启动");

	regSelfStart();

	thread t(thread_checkFood);
	t.detach();

	thread t1(thread_checkMediaServer);
	t1.detach();

	thread t2(thread_checkAdp);
	t2.detach();

	thread t3(thread_checkTcp2com);
	t3.detach();

	thread t4(thread_checkSrcMain);
	t4.detach();
}


#include<iostream>
#include<Windows.h>
#include<tchar.h>
#include <tlhelp32.h>//声明快照函数的头文件


bool tdsWatchDog::runProcess(string cmdline) {
	STARTUPINFOW si;
	//si.lpTitle = (LPWSTR)title.c_str();
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// Start the child process.
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = TRUE;
	if (!CreateProcessW(
		NULL,   // No module name (use command line)
		(LPWSTR)charCodec::utf8_to_utf16(cmdline).c_str(),        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_NEW_CONSOLE,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory
		&si,            // Pointer to STARTUPINFO structure
		&pi)           // Pointer to PROCESS_INFORMATION structure
		)
	{
		log("runProcess失败:"  + cmdline + "  " + sys::getLastError());
		return 0;
	}
	else
	{
		string s = "runProcess成功: "  + cmdline;
		log(s);
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return pi.dwProcessId;
}

bool tdsWatchDog::isProcessRun(string name)
{
	HANDLE hProcessSnap;
	//HANDLE hProcess;
	PROCESSENTRY32 pe32;
	DWORD dwPriorityClass;

	bool bFind = false;

	// 获取系统中所有进程的快照。
	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	// 在使用结构之前设置结构的大小。
	pe32.dwSize = sizeof(PROCESSENTRY32);

	// 获取第一个进程的信息
	// 如果不成功则退出
	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);          // clean the snapshot object
		return false;
	}
	//现在遍历进程的快照，并且依次显示每个进程的信息
	do
	{
		// Retrieve the priority class.
		dwPriorityClass = 0;
		string exePath = pe32.szExeFile;


		if (exePath.find(name)!=string::npos)
		{
			bFind = true;
			break;
		}
	} while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);
	return bFind;
}




string tdsWatchDog::getFileVerInfo(string path)
{
	return "";
	//if (!fs::fileExist(path))
	//	return "";

	//path = charCodec::utf8_to_gb(path);

	//DWORD dwSize = GetFileVersionInfoSize(path.c_str(), NULL);
	//LPVOID pBlock = malloc(dwSize);
	//GetFileVersionInfo(path.c_str(), 0, dwSize, pBlock);
	//char* pVerValue = NULL;
	//UINT nSize = 0;
	//VerQueryValue(pBlock, TEXT("\\VarFileInfo\\Translation"), (LPVOID*)&pVerValue, &nSize);
	//if (pVerValue == NULL)
	//	return "";
	//string strSubBlock, strTranslation, strTemp;
	//strTemp = str::format("000%x", *((unsigned short*)pVerValue));
	//strTranslation = strTemp.substr(strTemp.length()-4,4);
	//strTemp = str::format("000%x", *((unsigned short*) & pVerValue[2]));
	//strTranslation += strTemp.substr(strTemp.length() - 4, 4);
	////080404b0为中文，040904E4为英文
	////文件描述
	//strSubBlock = str::format("\\StringFileInfo\\%s\\FileDescription", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp = str::format(_GB("文件描述: %s"), pVerValue);
	//strTemp = charCodec::gb_to_utf8(strTemp);
	////文件版本
	//strSubBlock = str::format("\\StringFileInfo\\%s\\FileVersion", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp =str::format("%s", pVerValue);
	//string strFileVersion = strTemp;
	////内部名称
	//strSubBlock = str::format("\\StringFileInfo\\%s\\InternalName", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp = str::format("内部名称: %s", pVerValue);
	////合法版权
	//strSubBlock = str::format("\\StringFileInfo\\%s\\LegalTradeMarks", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp = str::format("合法版权: %s", pVerValue);
	////原始文件名
	//strSubBlock = str::format("\\StringFileInfo\\%s\\OriginalFileName", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp = str::format("原始文件名: %s", pVerValue);
	////产品名称
	//strSubBlock = str::format("\\StringFileInfo\\%s\\ProductName", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp = str::format("产品名称: %s", pVerValue);
	////产品版本
	//strSubBlock = str::format("\\StringFileInfo\\%s\\ProductVersion", strTranslation.c_str());
	//VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	//strTemp = str::format("产品版本: %s", pVerValue);
	//string str =str::format("%s", pVerValue);
	//str = charCodec::gb_to_utf8(str);
	//free(pBlock);
	//return str;
}

string tdsWatchDog::getCurTdsVer()
{
	return getFileVerInfo(fs::appPath() + "/tds.exe");
}

string tdsWatchDog::getUpdateTdsVer()
{
	return getFileVerInfo(fs::appPath() + "/update/tds.exe");
}

bool tdsWatchDog::regSelfStart()
{
	string appName = fs::appName();
	char exePath[MAX_PATH];
	GetModuleFileName(NULL, exePath, MAX_PATH);
	// 检查注册表中是否已经添加过开机自启动项
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
	{
		// 检查注册表中是否已经存在当前程序的开机自启动项
		DWORD dataSize = MAX_PATH;
		char regValue[MAX_PATH];
		if (RegQueryValueEx(hKey, appName.c_str(), NULL, NULL, (BYTE*)regValue, &dataSize) == ERROR_SUCCESS)
		{
			// 如果已经存在开机自启动项，则不弹出提示
			RegCloseKey(hKey);
			return 0;
		}
	}

	// 弹出确认对话框
	int result = MessageBox(NULL, charCodec::utf8_to_gb("是否将当前程序添加到开机自启动？").c_str(), charCodec::utf8_to_gb("确认对话框").c_str(), MB_YESNO | MB_ICONQUESTION);

	if (result == IDYES)
	{
		// 打开注册表项
		HKEY hKey;
		RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);

		// 将当前程序的路径添加到开机自启动项
		RegSetValueEx(hKey, appName.c_str(), 0, REG_SZ, (BYTE*)exePath, strlen(exePath));

		// 关闭注册表项
		RegCloseKey(hKey);

		::MessageBox(NULL, charCodec::utf8_to_gb("开机启动添加成功!").c_str(), "", MB_OK);
	}
	return false;
}

bool tdsWatchDog::unregSelfStart()
{
	/*HKEY hkey;
	if (ERROR_SUCCESS == ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hkey))
	{
		if (ERROR_SUCCESS == ::RegDeleteValue(hkey, "tdsd"))
		{
			printf(_GB("开机启动删除成功!"));
			return true;
		}
	}
	printf(_GB("开机启动删除失败!"));*/
	return false;
}

bool tdsWatchDog::isSelfStartReg()
{
	//HKEY hkey;
	//if (ERROR_SUCCESS == ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hkey))
	//{
	//	//hKEY是上面打开时得到的指针
	//	LPBYTE getValue = new BYTE[80];//得到的键值
	//	DWORD keyType = REG_SZ;//定义数据类型
	//	DWORD DataLen = 80;//定义数据长度
	//	if (ERROR_SUCCESS == ::RegQueryValueEx(hkey, "tdsd", NULL, &keyType, getValue, &DataLen))
	//	{
	//		return true;
	//	}
	//}
	return false;
}

void tdsWatchDog::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, string strIP, int port)
{
	string food = str::fromBuff(recvData,recvDataLen);
	//LOG("food is " + food);
	timeopt::now(&m_lastFeedTime);
}

void tdsWatchDog::log(string s)
{
	TIME stNow;
	timeopt::now(&stNow);
	string time = str::format("%02d:%02d:%02d.%03d", stNow.wHour, stNow.wMinute, stNow.wSecond, stNow.wMilliseconds);
	s = charCodec::utf8_to_gb(s);
	s = time + " " + s +  "\r\n";
	printf(s.c_str());
	fs::appendFile(fs::appPath() + "/tdsd.log.txt", s);
}


#endif
