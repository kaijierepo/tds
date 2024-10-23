#include <diskClean.h>
#include <thread>
#include <tds.h>
//#include <synchapi.h>
#include <common/common.h>
#include <data_server/tdb.h>

CDiskClean g_diskClean;
using namespace std;

int CDiskClean::Init()
{
	return 0;
}

int CDiskClean::Run()
{
	thread t(ThreadClean, this);
	t.detach();
	return 0;
}

// 检查字符是否在 '0' 到 '9' 之间
bool isAllDigits(const std::string& str) {
	return std::all_of(str.begin(), str.end(),   [](char c) {	return c >= '0' && c <= '9'; }  );
}

void CDiskClean::ThreadClean(void* lpParam)
{
	if (tds->conf->triggerStratgy == "LowLimit") {

	}
	else if (tds->conf->triggerStratgy == "peroid") {
		int period = tds->conf->judgePeriod;  if (period < 5)  period = 5;
		while (1) {
			timeopt::sleepMilli(period * 1000);

			int storageMonths = tds->conf->dataStorageMonths;  if (storageMonths < 12)  storageMonths = 12;
			string folderPath = db.m_path;
			g_diskClean.DoClean(folderPath, storageMonths);

			storageMonths = tds->conf->mediaStorageMonths;  if (storageMonths < 3)  storageMonths = 3;
			folderPath = db.m_path+"/media";
			g_diskClean.DoClean(folderPath, storageMonths);
		}
	}
}


int CDiskClean::DoClean(string folderPath, int storageMonths)
{
	vector<fs::FILE_INFO>  vecDirs;
	fs::getFolderList(vecDirs, folderPath, false);

	vector<int>  vecYearMonth;
	for (int i = 0; i < vecDirs.size(); i++) {
		string dir = vecDirs[i].name;
		if (dir.length() == 6 && isAllDigits(dir)
			&& atoi(dir.substr(0, 4).c_str()) >= 1970
			&& atoi(dir.substr(4, 2).c_str()) >= 1 && atoi(dir.substr(4, 2).c_str()) <= 12) {

			vecYearMonth.push_back(atoi(dir.c_str()));
		}
	}

	//从大到小排序 再删除后面的
	if (vecYearMonth.size() > storageMonths) {

		std::sort(vecYearMonth.begin(), vecYearMonth.end(), [](int a, int b) { return a > b; });

		for (int i = storageMonths; i < vecYearMonth.size(); i++) {
			string dirPath = folderPath + "/" + to_string(vecYearMonth[i]);
			DB_FS::deleteDirectory(dirPath);
		}
	}

	return 0;
}
