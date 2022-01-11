#include "pch.h"
#include "tdsWatchDog.h"
#include "logger.h"

tdsWatchDog watchDog;
tdsDogFeeder dogFeeder;

string foodPath = fs::appPath() + "/watchDog.ini";

tdsWatchDog::tdsWatchDog()
{
}


void thread_feedDog()
{
	LOG("[keyinfo][软件狗   ]软件狗通讯线程启动");
	while (1)
	{
		Sleep(100);
		string t = timeopt::nowStr(true);
		::WritePrivateProfileString("watchDog", "lastActive", t.c_str(), foodPath.c_str());
	}
}


void runTds() {
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// Start the child process.
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOW;
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
		LOG("启动tds失败" + sys::getLastError());
	}
	else
	{
		
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}


void thread_watchDog() {
	while (1)
	{
		Sleep(100);
		char szTime[30] = { 0 };
		::GetPrivateProfileString("watchDog", "lastActive","", szTime,30,foodPath.c_str());
		string t = szTime;

		int pass = timeopt::CalcTimePassMilliSecond(timeopt::str2st(t));
		//LOG("[keyinfo]wait food for " + str::fromInt(pass));
		if (pass > 2500)
		{
			LOG("启动tds");
			WinExec("taskkill /f /im tds.exe /t", SW_SHOW);//关闭可能处于卡死状态的程序。
			Sleep(200);
			runTds();
			Sleep(5000);
		}
	}
}

void tdsWatchDog::run()
{
	LOG("软件狗启动");
	thread t(thread_watchDog);
	t.detach();
}

void tdsDogFeeder::run()
{
	thread t(thread_feedDog);
	t.detach();
}
