#include "pch.h"
#include "tdsWatchDog.h"
#include <winver.h>


#pragma comment(lib, "version.lib")

tdsWatchDog watchDog;
tdsDogFeeder dogFeeder;

tdsWatchDog::tdsWatchDog()
{
	//获取程序版本信息
	m_curVer = watchDog.getCurTdsVer();
}

void wakeUpFeeder() {
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

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


void thread_checkFood() {
	GetLocalTime(&watchDog.m_lastFeedTime);
	GetLocalTime(&watchDog.m_lastUpdateCheckTime);
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
				WinExec("copy .\\update\\tds.exe .\\tds.exe", SW_SHOW);
				wakeUpFeeder();
				Sleep(5000);
			}
			GetLocalTime(&watchDog.m_lastUpdateCheckTime);
		}


		//LOG("[keyinfo]wait food for " + str::fromInt(pass));
		if (pass > 1000)
		{
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
	watchDog.log("tds daemon 守护进程启动");
	m_foodPlate.m_pCallback = this;
	m_foodPlate.m_port = 660;
	m_foodPlate.start();
	thread t(thread_checkFood);
	t.detach();
}





string tdsWatchDog::getFileVerInfo(string path)
{
	if (!fs::fileExist(path))
		return "";

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

int tdsWatchDog::OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port)
{
	string food = recvData;
	//LOG("food is " + food);
	GetLocalTime(&m_lastFeedTime);
	return 0;
}

void tdsWatchDog::log(string s)
{
	s = charCodec::utf8toAnsi(s);
	s += "\r\n";
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
	m_foodCart.m_port = 661;
	m_foodCart.start();
	thread t(thread_feedDog);
	t.detach();
}

void tdsDogFeeder::sendFood()
{
	string data = "yummy bone";
	m_foodCart.SendData((char*)data.c_str(), data.length(), "127.0.0.1", 660);
}
