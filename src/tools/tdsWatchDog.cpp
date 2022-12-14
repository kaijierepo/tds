#include "pch.h"
#include "tdsWatchDog.h"
#include <winver.h>
#include "logger.h"


#pragma comment(lib, "version.lib")

tdsWatchDog watchDog;
tdsDogFeeder dogFeeder;

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
		(LPWSTR)charCodec::utf8toUtf16("tds.exe").c_str(),        // Command line
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
		Sleep(1000);
		if (!watchDog.isProcessRun("MediaServer.exe")) {
			string msPath = fs::appPath() + "/com/mediaServer/MediaServer.exe";
			if (fs::fileExist(msPath)){
				watchDog.runProcess(msPath);
			}
		}
	}
}

void thread_checkAdp() {
	while (1) {
		Sleep(1000);
		if (!watchDog.isProcessRun("node.exe")) {
			string msPath = fs::appPath() + "/com/adp/run.bat";
			if (fs::fileExist(msPath)) {
				msPath = charCodec::utf8toAnsi(msPath);
				system((msPath).c_str());
			}
		}
	}
}



void thread_checkFood() {
	timeopt::now(&watchDog.m_lastFeedTime);
	timeopt::now(&watchDog.m_lastUpdateCheckTime);
	while (1)
	{
		Sleep(100);
		int pass = timeopt::CalcTimePassMilliSecond(watchDog.m_lastFeedTime);
		int updateCheckPass = timeopt::CalcTimePassMilliSecond(watchDog.m_lastUpdateCheckTime);

		if (updateCheckPass > 1000) {
			string newRev = watchDog.getUpdateTdsVer();
			if (newRev != "" && newRev > watchDog.m_curVer) {
				watchDog.log("发现新版本:" + newRev + ",当前版本:" + watchDog.m_curVer);

				WinExec("taskkill /f /im tds.exe /t", SW_SHOW);//关闭可能处于卡死状态的程序。如果启动了多个实例，该命令可以同时关闭多个。
				WinExec("taskkill /f /im WerFault.exe /t", SW_SHOW);//某些操作系统如windows server 2008 R2 enterprize 会出现该程序，
				Sleep(200);
				try {
					filesystem::copy(charCodec::utf8toUtf16(fs::appPath() + "/update/tds.exe"), charCodec::utf8toUtf16(fs::appPath() + "/tds.exe"), std::filesystem::copy_options::overwrite_existing);
				}
				catch (std::exception& e)
				{
					string err = e.what();
					watchDog.log(err);
				}
				
				wakeUpFeeder();
				Sleep(5000);
			}
			timeopt::now(&watchDog.m_lastUpdateCheckTime);
		}


		//LOG("[keyinfo]wait food for " + str::fromInt(pass));
		if (pass > 1000)
		{
			watchDog.log("没有检测到活动的服务，重启服务");
			//watchDog.log("准备启动tds,执行 taskkill /f /im tds.exe /t 关闭现有实例");
			WinExec("taskkill /f /im tds.exe /t", SW_SHOW);//关闭可能处于卡死状态的程序。如果启动了多个实例，该命令可以同时关闭多个。
			WinExec("taskkill /f /im WerFault.exe /t", SW_SHOW);//某些操作系统如windows server 2008 R2 enterprize 会出现该程序，
			//就是一个对话框显示 tds.exe 已停止工作。联机检查解决方案并关闭程序  按钮  和  关闭程序 按钮
			Sleep(200);
			wakeUpFeeder();
			Sleep(5000);
		}
	}
}

void tdsWatchDog::run()
{
	//if (!isServiceInstalled()) {
	//	string info = _GB("是否安装tdsd服务?");
	//	string title = _GB("安装服务");
	//	if (MB_OK == ::MessageBox(NULL, info.c_str(), title.c_str(), MB_OKCANCEL)) {
	//		if (installService()) {
	//		    info = _GB("tdsd服务安装成功!");
	//			title = _GB("服务安装");
	//			::MessageBox(NULL, info.c_str(), title.c_str(), MB_OK);
	//		}
	//		else {
	//			info = _GB("tdsd服务安装失败!");
	//			title = _GB("服务安装");
	//			::MessageBox(NULL, info.c_str(), title.c_str(), MB_OK);
	//		}
	//	}
	//	exit(0);
	//}

	watchDog.log("TDS Daemon 服务启动");
	m_foodPlate.m_pCallback = this;
	m_foodPlate.m_port = FOOD_PLATE_PORT;
	m_foodPlate.start();
	thread t(thread_checkFood);
	t.detach();

	thread t1(thread_checkMediaServer);
	t1.detach();

	thread t2(thread_checkAdp);
	t2.detach();
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
		(LPWSTR)charCodec::utf8toUtf16(cmdline).c_str(),        // Command line
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
	HANDLE hProcess;
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
	if (!fs::fileExist(path))
		return "";

	path = charCodec::utf8toAnsi(path);

	DWORD dwSize = GetFileVersionInfoSize(path.c_str(), NULL);
	LPVOID pBlock = malloc(dwSize);
	GetFileVersionInfo(path.c_str(), 0, dwSize, pBlock);
	char* pVerValue = NULL;
	UINT nSize = 0;
	VerQueryValue(pBlock, TEXT("\\VarFileInfo\\Translation"), (LPVOID*)&pVerValue, &nSize);
	if (pVerValue == NULL)
		return "";
	string strSubBlock, strTranslation, strTemp;
	strTemp = str::format("000%x", *((unsigned short int*)pVerValue));
	strTranslation = strTemp.substr(strTemp.length()-4,4);
	strTemp = str::format("000%x", *((unsigned short int*) & pVerValue[2]));
	strTranslation += strTemp.substr(strTemp.length() - 4, 4);
	//080404b0为中文，040904E4为英文
	//文件描述
	strSubBlock = str::format("\\StringFileInfo\\%s\\FileDescription", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp = str::format(_GB("文件描述: %s"), pVerValue);
	strTemp = charCodec::ansi2Utf8(strTemp);
	//文件版本
	strSubBlock = str::format("\\StringFileInfo\\%s\\FileVersion", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp =str::format("%s", pVerValue);
	string strFileVersion = strTemp;
	//内部名称
	strSubBlock = str::format("\\StringFileInfo\\%s\\InternalName", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp = str::format("内部名称: %s", pVerValue);
	//合法版权
	strSubBlock = str::format("\\StringFileInfo\\%s\\LegalTradeMarks", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp = str::format("合法版权: %s", pVerValue);
	//原始文件名
	strSubBlock = str::format("\\StringFileInfo\\%s\\OriginalFileName", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp = str::format("原始文件名: %s", pVerValue);
	//产品名称
	strSubBlock = str::format("\\StringFileInfo\\%s\\ProductName", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp = str::format("产品名称: %s", pVerValue);
	//产品版本
	strSubBlock = str::format("\\StringFileInfo\\%s\\ProductVersion", strTranslation.c_str());
	VerQueryValue(pBlock, strSubBlock.c_str(), (LPVOID*)&pVerValue, &nSize);
	strTemp = str::format("产品版本: %s", pVerValue);
	string str =str::format("%s", pVerValue);
	str = charCodec::ansi2Utf8(str);
	free(pBlock);
	return str;
}

string tdsWatchDog::getCurTdsVer()
{
	return getFileVerInfo(fs::appPath() + "/tds.exe");
}

string tdsWatchDog::getUpdateTdsVer()
{
	return getFileVerInfo(fs::appPath() + "/update/tds.exe");
}

bool tdsWatchDog::installService()
{
	string path = fs::appPath() + "/tdsd.exe";
	SC_HANDLE schSCManager, schService;
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (schSCManager == NULL)
		return false;

	schService = CreateService(schSCManager, "tdsd", "tdsd",
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		path.c_str(),
		NULL,
		NULL,
		NULL,
		NULL,
		NULL);

	if (schService == NULL)
		return false;

	CloseServiceHandle(schService);
	return true;
}

bool tdsWatchDog::uninstallService()
{
	SC_HANDLE schSCManager, schService;
	schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

	if (schSCManager == NULL)
		return false;

	// 打开www服务。
	SC_HANDLE hSvc = ::OpenService(schSCManager, "tdsd",SERVICE_ALL_ACCESS);
	if (hSvc == NULL)
	{
		string info = _GB("没有找到服务tdsd");
		string title = _GB("错误");
		::MessageBox(NULL, info.c_str(), title.c_str(), MB_OK);
		return false;
	}

	if (::DeleteService(hSvc)) {
		string info = _GB("服务tdsd卸载成功");
		string title = _GB("卸载服务");
		::MessageBox(NULL, info.c_str(), title.c_str(), MB_OK);
	}
	else
	{
		string info = _GB("服务tdsd卸载失败");
		string title = _GB("卸载服务");
		::MessageBox(NULL, info.c_str(), title.c_str(), MB_OK);
	}

	CloseServiceHandle(hSvc);
	return true;
}

bool tdsWatchDog::isServiceInstalled()
{
	SC_HANDLE hSC = ::OpenSCManager(NULL,
		NULL, GENERIC_EXECUTE);
	if (hSC == NULL)
	{
		return false;
	}
	SC_HANDLE hSvc = ::OpenService(hSC, "tdsd",
		SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_STOP);
	if (hSvc == NULL)
	{
		return false;
	}
	return true;
}

BOOL Reg_LocalMachine(char* lpszFileName, char* lpszValueName)
{
	// 管理员权限
	HKEY hKey;
	// 打开注册表键
	if (ERROR_SUCCESS != ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey))
	{
		return FALSE;
	}
	// 修改注册表值，实现开机自启
	if (ERROR_SUCCESS != ::RegSetValueEx(hKey, lpszValueName, 0, REG_SZ, (BYTE*)lpszFileName, (1 + ::lstrlen(lpszFileName))))
	{
		::RegCloseKey(hKey);
		return FALSE;
	}
	// 关闭注册表键
	::RegCloseKey(hKey);

	return TRUE;
}


bool tdsWatchDog::regSelfStart()
{
	string path = fs::appPath() + "/tdsd.exe";
	path = charCodec::utf8toAnsi(path);
	if (Reg_LocalMachine((char*)path.c_str(), (char*)"tdsd"))
	{
		printf(_GB("开机启动添加成功!"));
		return true;
	}
	else {
		printf(_GB("开机启动添加失败!"));
	}
	return false;
}

bool tdsWatchDog::unregSelfStart()
{
	HKEY hkey;
	if (ERROR_SUCCESS == ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hkey))
	{
		if (ERROR_SUCCESS == ::RegDeleteValue(hkey, "tdsd"))
		{
			printf(_GB("开机启动删除成功!"));
			return true;
		}
	}
	printf(_GB("开机启动删除失败!"));
	return false;
}

bool tdsWatchDog::isSelfStartReg()
{
	HKEY hkey;
	if (ERROR_SUCCESS == ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &hkey))
	{
		//hKEY是上面打开时得到的指针
		LPBYTE getValue = new BYTE[80];//得到的键值
		DWORD keyType = REG_SZ;//定义数据类型
		DWORD DataLen = 80;//定义数据长度
		if (ERROR_SUCCESS == ::RegQueryValueEx(hkey, "tdsd", NULL, &keyType, getValue, &DataLen))
		{
			return true;
		}
	}
	return false;
}

void tdsWatchDog::OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port)
{
	string food = recvData;
	//LOG("food is " + food);
	timeopt::now(&m_lastFeedTime);
}

void tdsWatchDog::log(string s)
{
	TIME stNow;
	timeopt::now(&stNow);
	string time = str::format("%02d:%02d:%02d.%03d", stNow.wHour, stNow.wMinute, stNow.wSecond, stNow.wMilliseconds);
	s = charCodec::utf8toAnsi(s);
	s = time + " " + s +  "\r\n";
	printf(s.c_str());
	fs::appendFile(fs::appPath() + "/tdsd.log.txt", s);
}


void thread_feedDog() {
	setThreadName("feed dog thread");
	while (1)
	{
		dogFeeder.sendFood();
		Sleep(100);
	}	
}


void tdsDogFeeder::run()
{
	m_foodCart.m_port = FOOD_FEEDER_PORT;
	m_foodCart.start();
	thread t(thread_feedDog);
	t.detach();
}

void tdsDogFeeder::sendFood()
{
	string data = "yummy bone";
	m_foodCart.SendData((char*)data.c_str(), data.length(), "127.0.0.1", FOOD_PLATE_PORT);
}


