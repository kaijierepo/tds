#include "pch.h"
#include "tdsWatchDog.h"
#include "logger.h"


tdsWatchDog watchDog;
tdsDogFeeder dogFeeder;

tdsWatchDog::tdsWatchDog()
{
	
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
		LOG("启动tds失败" + sys::getLastError());
	}
	else
	{
		LOG("启动tds成功,启动时间 " + timeopt::nowStr());
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}


void thread_checkFood() {
	GetLocalTime(&watchDog.m_lastFeedTime);
	while (1)
	{
		Sleep(100);
		int pass = timeopt::CalcTimePassMilliSecond(watchDog.m_lastFeedTime);
		LOG("[keyinfo]wait food for " + str::fromInt(pass));
		if (pass > 1000)
		{
			LOG("准备启动tds,执行 taskkill /f /im tds.exe /t 关闭现有实例");
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
	LOG("tds daemon 守护进程启动");
	m_foodPlate.m_pCallback = this;
	m_foodPlate.m_port = 660;
	m_foodPlate.start();
	thread t(thread_checkFood);
	t.detach();
}

int tdsWatchDog::OnRecvUdpData(char* recvData, int recvDataLen, string strIP, int port)
{
	string food = recvData;
	LOG("food is " + food);
	GetLocalTime(&m_lastFeedTime);
	return 0;
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
