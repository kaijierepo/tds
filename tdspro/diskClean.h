#pragma once
#include <string>

class CDiskClean
{
public:
	CDiskClean(){}
	virtual ~CDiskClean(){}

	int Init();
	int Run();

	static void ThreadClean(void* lpParam);
	int DoClean(std::string folderPath, int storageMonths);

	/*
	string	triggerStratgy= LowLimit # 清理的触发策略。 LowLimit： db所在逻辑盘的剩余空间低于门限时触发；peroid：周期触发。
	int	diskSpaceLeft = 20 #单位 GB。仅triggerStratgy取值为LowLimit时有效。 数据所在盘符的剩余空间低于该值时 触发清理。
	int	judgePeriod=60 #单位 秒。仅triggerStratgy取值为peroid时有效。 每xxx时间判断下。
	int	dataStorageMonths = 48   #db/{年月}  保留最近多少月。
	int mediaStorageMonths = 12  #db/media 文件夹保留最近多少月
	*/
};

extern CDiskClean g_diskClean;