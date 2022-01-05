#include "pch.h"
#include "tdsWatchDog.h"
#include "logger.h"

tdsWatchDog watchDog;

tdsWatchDog::tdsWatchDog()
{
}

void thread_watchDog() {
	while (1)
	{
		Sleep(200);
		int iLastActive = ::GetPrivateProfileInt("tds", "lastActive", 0, "watchDog.ini");
		int current = time(NULL);

		if (current - iLastActive > 1)
		{
			LOG("启动tds");
			WinExec("tds.exe", SW_SHOWNORMAL);
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
