#include <diskCleaner.h>
#include <thread>
#include <filesystem>
#include "logger.h"
#include "kvIni.h"

void DiskCleaner() {

}

void DiskCleaner::run()
{
	KV_INI kvi;

	kvi.load("diskCleaner.ini");

	dbDir = kvi.getValStr("dbPath", "");
	saveMonthCount = kvi.getValInt("saveMonthCount", 36);

	LOG("diskCleaner启动,数据库路径=%s,保留数据=%d个月", dbDir.c_str(), saveMonthCount);
}
