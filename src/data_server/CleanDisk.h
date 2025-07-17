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
	std::atomic_int	g_nCleanStatus = 0;		//状态 0：停止状态  1：启动中  2：运行状态 3：退出中

public:
	static bool IsDirExist(const std::string &csDir);
	bool DeleteDirectory(const std::string &DirName);//递归方式,只能删除空文件夹
	bool DeleteFile(const std::string &FileName);//删除文件
	static bool IsNumberString(const std::string &str);
	static std::string format(const char *pszFmt, ...);
	//std::string GetConfigVal(const std::string &strKey, const std::string &strDefault = "", const std::string &strAppName = STR_INI_MAIN_KEY);
	//int GetConfigIntVal(const std::string &strKey, int iDefault, const std::string &strAppName = STR_INI_MAIN_KEY);
	bool IsInTimeZone(int Now, int sta, int sto);

public:
	void DelAllPointDir(const std::string &strDir, const std::string &strDirName, std::map<std::string, bool> &mapDeleteFiles);
	//日志
	bool CheckAndDelUselessFile(int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays);
	//图片、曲线
	bool CheckAndDelUsefulFile(int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays, int iRemainUsefulFileDays);

	void LogStatic(const std::string &strLog);

public:
	void SetPath(const std::string &strProjectPath, const std::string &strDBPathAbs);

private:
	std::string m_strProjectPath;
	std::string m_strDBPathAbs;

	void CloseOldLogFile();
	bool GetLogFileName(tm &rTimeIn, std::string &strReturn);
	int	m_wLogDayID;
	std::ofstream m_hStaticAcq;

};

void CleanDiskFun(const std::string &strProjectPath, const std::string &strDBPathAbs, const std::function<void(void)> &fun,
	int dwRemainDisk, int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays, int iRemainUsefulFileDays);