#pragma once
#include <string>
#include <iostream>
#include <fstream>
#include <atomic>
#include <functional>
#include <map>

class CCleanDisk
{
public:
	CCleanDisk();
	~CCleanDisk();
	std::atomic_int	g_nCleanStatus = 0;		//clean status 0:stop 1:starting 2:running 3:quiting

public:
	static bool IsDirExist(const std::string &csDir);
	bool DeleteDirectory(const std::string &DirName);//delete dir
	bool DeleteFile(const std::string &FileName);//delete file
	static bool IsNumberString(const std::string &str);
	static std::string format(const char *pszFmt, ...);
	bool IsInTimeZone(int Now, int sta, int sto);

public:
	void DelAllPointDir(const std::string &strDir, const std::string &strDirName, std::map<std::string, bool> &mapDeleteFiles);
	//clean log files
	bool CheckAndDelUselessFile(int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays);
	//clean picture£¬video£¬curve files
	bool CheckAndDelUsefulFile(int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays, int iRemainUsefulFileDays, bool bVideoTag = false);

	void LogStatic(const std::string &strLog);

public:
	void SetPath(const std::string &strProjectPath, const std::string &strDBPathAbs, const std::string& strVideoTag);

private:
	std::string m_strProjectPath;
	std::string m_strDBPathAbs;
	std::string m_strVideoTag;

	void CloseOldLogFile();
	bool GetLogFileName(tm &rTimeIn, std::string &strReturn);
	int	m_wLogDayID;
	std::ofstream m_hStaticAcq;

};

void CleanDiskFun(const std::string &strProjectPath, const std::string& strVideoTag, const std::string &strDBPathAbs, const std::function<void(void)> &fun,
	int dwRemainDisk, int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays, int iRemainUsefulFileDays);