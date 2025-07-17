// CleanDisk.cpp
//

#include "CleanDisk.h"
#include <chrono>
#include <thread>
#include <cstdarg>
#include <iomanip>


#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || (!defined(_MSVC_LANG) && defined(__cplusplus) && __cplusplus < 201703L)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

bool IsLeapYear(int wYear)
{
	return ((wYear % 4) == 0) && ((wYear % 100) != 0) || ((wYear % 400) == 0);
}

int DaysInAMonth(int wYear, int wMonth)
{
	const int MonthDays[] = { 31, 28, 31,30, 31,30, 31, 31,30, 31,30, 31,
	31, 29, 31,30, 31,30, 31, 31,30, 31,30, 31 };

	if (IsLeapYear(wYear) == false)
		return MonthDays[wMonth - 1];
	else
		return MonthDays[wMonth + 12 - 1];
}

CCleanDisk::CCleanDisk()
{
	m_wLogDayID = 0;
}

CCleanDisk::~CCleanDisk()
{
	CloseOldLogFile();
}
void CCleanDisk::CloseOldLogFile()
{
	if (m_hStaticAcq.is_open())
	{
		m_hStaticAcq.close();
	}
}

bool CCleanDisk::GetLogFileName(tm &rTimeIn, std::string &strReturn)
{
	std::string strDate = format("%04d%02d%02d", rTimeIn.tm_year + 1900, rTimeIn.tm_mon + 1, rTimeIn.tm_mday);
	std::string strDatePath = m_strProjectPath + "\\Log\\" + strDate;
	strReturn = strDatePath + "\\CleanDisk_" + strDate + ".log";
	fs::create_directories(strDatePath);

	return true;
}

void CCleanDisk::LogStatic(const std::string &strLog)
{
	auto now = std::chrono::system_clock::now();
	time_t now_c = std::chrono::system_clock::to_time_t(now);
	tm st;
	localtime_s(&st, &now_c);

	if (st.tm_mday != m_wLogDayID)
	{
		CloseOldLogFile();
		std::string strFile;
		if (!GetLogFileName(st, strFile))
		{
			return;
		}
		m_hStaticAcq.open(strFile, std::ios_base::app);
		if (!m_hStaticAcq.is_open())
		{
			return;
		}
		m_hStaticAcq.seekp(std::ios::end, 0);
		m_wLogDayID = st.tm_mday;
	}
	
	char timeString[100];
	std::strftime(timeString, sizeof(timeString), "%T", &st);
	const auto duration_in_millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	const int milliseconds = duration_in_millis.count();
	m_hStaticAcq << timeString 
		<< "." << std::setfill('0') << std::setw(3) << milliseconds
		<< " " << strLog << std::endl;
}

std::string CCleanDisk::format(const char *pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt);
	{
		int nLength = _vscprintf(pszFmt, args);
		nLength += 1;
		std::vector<char> vectorChars(nLength);
		vsnprintf_s(vectorChars.data(), nLength, _TRUNCATE, pszFmt, args);
		str.assign(vectorChars.data());
	}
	va_end(args);
	return str;
}

std::time_t to_time_t(const fs::file_time_type& ftime)
{
#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || (!defined(_MSVC_LANG) && defined(__cplusplus) && __cplusplus < 201703L)
	return fs::file_time_type::clock::to_time_t(ftime);
#else
	const auto epoch = ftime.time_since_epoch();
	auto system_epoch = std::chrono::duration_cast<std::chrono::system_clock::duration>(epoch);
	std::chrono::system_clock::time_point sys_time{ system_epoch };
	return std::chrono::system_clock::to_time_t(sys_time);
#endif
}


bool CCleanDisk::IsNumberString(const std::string &str)
{
	for (auto c : str)
	{
		if (c < '0' || c>'9')
			return false;
	}
	return true;
}

bool CCleanDisk::IsDirExist(const std::string &csDir)
{
	if (fs::exists(csDir) && fs::is_directory(csDir))
		return true;
	
	return false;
}

bool CCleanDisk::DeleteFile(const std::string &FileName)
{
	bool result = false;
	try {
		result = fs::remove(FileName);
	}
	catch (const fs::filesystem_error &ex) {
		//std::cerr << "Deletion failed: " << ex.what() << std::endl;
		auto strLog = format("[DiskMng]delete file exception:%s[%s]， delete file again after remove attribute", FileName.c_str(), ex.what());
		LogStatic(strLog);

		// get the current permissions
		auto current_perms = fs::status(FileName).permissions();

		// remove the read-only bit
		current_perms |= (fs::perms::owner_write| fs::perms::group_write | fs::perms::others_write);

		// update the file's permissions 
		try {
			fs::permissions(FileName, current_perms);
			result = fs::remove(FileName);
		}
		catch (const std::system_error &e) {
			strLog = format("[DiskMng]delete file exception again:%s[%s]", FileName.c_str(), ex.what());
			LogStatic(strLog);
			return 1;
		}
	}
	return result;
}


bool CCleanDisk::DeleteDirectory(const std::string &DirName)
{
	bool result = false;
	try {
		result = fs::remove_all(DirName);
	}
	catch (const fs::filesystem_error &ex) {
		//std::cerr << "Deletion failed: " << ex.what() << std::endl;
		auto strLog = format("[DiskMng]delete dir exception:%s[%s]", DirName.c_str(), ex.what());
		LogStatic(strLog);

		for (const auto &entry : fs::directory_iterator(DirName)) {
			if (entry.status().type() != fs::file_type::directory)
			{
				DeleteFile(entry.path().string());
			}
		}

		try {
			result = fs::remove_all(DirName);
		}
		catch(...){}
	}
	return result;
}


void CCleanDisk::DelAllPointDir(const std::string &strDir, const std::string &strDirName, std::map<std::string, bool>& mapDeleteFiles)
{
	if (strDir == "")
		return;

	for (const auto &entry : fs::directory_iterator(strDir))
	{
		if (entry.status().type() == fs::file_type::directory)
		{
			if (entry.path().filename() == "." || entry.path().filename() == "..")
			{
				continue;
			}
			auto strPath = entry.path().string();

			if (strPath.find(strDirName) != std::string::npos)
			{
				mapDeleteFiles[strPath] = true;
			}
			else
				DelAllPointDir(strPath, strDirName, mapDeleteFiles);

		}
	}
}

bool CCleanDisk::IsInTimeZone(int Now, int sta, int sto)
{
	if (sto > sta)
	{
		if (Now >= sta && Now <= sto)
			return true;
		else
			return false;
	}
	else
	{
		if (Now <= sta && Now >= sto)
			return true;
		else
			return false;
	}
}

bool CCleanDisk::CheckAndDelUselessFile(int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays)
{
	if (iRemainUselessFileDays <= 7) return true;
	std::string strLogPathFile = m_strProjectPath + "\\Log\\";
	std::string strFilePath = "", strFileName = "", strLog = "", strTemp = "";

	auto now = std::chrono::system_clock::now();
	time_t now_c = std::chrono::system_clock::to_time_t(now);
	tm now_tm;
	localtime_s(&now_tm, &now_c);
	if (IsInTimeZone(now_tm.tm_hour, iCleanDiskStartTime, iCleanDiskEndTime) == false)
		return true;

	std::map<std::string, bool> mapDeleteFiles;//true:dir false:file

	bool bDel = false;

	for (const auto &entry : fs::directory_iterator(strLogPathFile))
	{
		strFileName = entry.path().filename().string();
		strFilePath = entry.path().string();

		if (entry.status().type() == fs::file_type::directory
			&& strFileName != "." && strFileName != ".."
			&& strFileName.size() == 8 && IsNumberString(strFileName))
		{
			tm cFolderTime;
			memset(&cFolderTime, 0, sizeof(cFolderTime));
			cFolderTime.tm_year = stoi(strFileName.substr(0, 4)) - 1900;
			cFolderTime.tm_mon = stoi(strFileName.substr(4, 2)) - 1;
			cFolderTime.tm_mday = stoi(strFileName.substr(6, 2));
			if (cFolderTime.tm_year >= 0)
			{
				double difference = std::difftime(now_c, std::mktime(&cFolderTime)) / (60 * 60 * 24);

				if (int(difference) > iRemainUselessFileDays)
				{
					mapDeleteFiles[strFilePath] = true;
					bDel = true;
				}
			}
		}
		else if (strFileName != "." && strFileName != "..")
		{
			fs::file_time_type time = fs::last_write_time(entry.path());
			std::time_t cftime = to_time_t(time);

			double difference = std::difftime(now_c, cftime) / (60 * 60 * 24);			
			if ((int)difference > iRemainUselessFileDays)
			{
				if (entry.status().type() == fs::file_type::directory)
				{
					mapDeleteFiles[strFilePath] = true;
				} 
				else
				{
					mapDeleteFiles[strFilePath] = false;
				}

				bDel = true;
			}
		}
	}

	//start delete
	auto bDelRetu = false;
	for (auto& it : mapDeleteFiles)
	{
		if (it.second)
		{
			bDelRetu = DeleteDirectory(it.first);

			strLog = format("[DiskMng]delete log dir:%s[%s]", strFilePath.c_str(), bDelRetu ? "success" : "failed");
			LogStatic(strLog);
		} 
		else
		{
			bDelRetu = DeleteFile(it.first);
			strLog = format("[DiskMng]delete file:%s[%s]", strFilePath.c_str(), bDelRetu ? "success" : "failed");
			LogStatic(strLog);
		}
	}
	return bDel;
}


bool CCleanDisk::CheckAndDelUsefulFile(int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays, int iRemainUsefulFileDays, bool bVideoTag/* = false*/)
{
	if (iRemainUsefulFileDays <= 30 || iRemainUselessFileDays <= 7) return true;
	std::string strDataPath = m_strDBPathAbs;
	if (bVideoTag)
	{
		strDataPath + "/" + m_strVideoTag + "/";
	}
	std::string strFilePath = "", strFileName = "", strLog = "", strTemp = "";

	bool bDel = false;

	std::map<std::string, bool> mapDeleteFiles;//true:dir false:file
	{
		auto now = std::chrono::system_clock::now();
		time_t now_c = std::chrono::system_clock::to_time_t(now);
		tm now_tm;
		localtime_s(&now_tm, &now_c);

		if (IsInTimeZone(now_tm.tm_hour, iCleanDiskStartTime, iCleanDiskEndTime) == false)
			return true;


		for (const auto &entry : fs::directory_iterator(strDataPath))
		{
			strFileName = entry.path().filename().string();
			if (entry.status().type() == fs::file_type::directory
				&& strFileName != "." && strFileName != ".."
				&& strFileName.size() == 6 && IsNumberString(strFileName))
			{
				if (IsNumberString(strFileName))
				{
					int year_month = stoi(strFileName);
					tm cFolderTime;
					memset(&cFolderTime, 0, sizeof(cFolderTime));
					cFolderTime.tm_year = year_month / 100 - 1900;
					cFolderTime.tm_mon = year_month % 100 - 1;
					cFolderTime.tm_mday = DaysInAMonth(year_month / 100, year_month % 100);

					bool bDeleteDir = false;
					if (cFolderTime.tm_year >= 0)
					{
						double difference = std::difftime(now_c, std::mktime(&cFolderTime)) / (60 * 60 * 24);

						if (int(difference) > iRemainUsefulFileDays)
						{
							strFilePath = entry.path().string();
							bDel = true;
							bDeleteDir = true;
							mapDeleteFiles[strFilePath] = true;
						}
					}

					if (!bDeleteDir)
					{
						for (int iDay = 1; iDay <= 31; iDay++)
						{
							strTemp = format("%02d", iDay);
							strFilePath = entry.path().string() + "\\" + strTemp;
							if (IsDirExist(strFilePath))
							{
								tm cFolderTime;
								memset(&cFolderTime, 0, sizeof(cFolderTime));
								cFolderTime.tm_year = stoi(strFileName.substr(0, 4)) - 1900;
								cFolderTime.tm_mon = stoi(strFileName.substr(4, 2)) - 1;
								cFolderTime.tm_mday = iDay;
								if (cFolderTime.tm_year >= 0)
								{
									double difference = std::difftime(now_c, std::mktime(&cFolderTime)) / (60 * 60 * 24);
	
									if (int(difference) > iRemainUsefulFileDays)
									{
										mapDeleteFiles[strFilePath] = true;

										bDel = true;
									}
								}
							}
						}
					}
				}
			}
			else if (strFileName != "." && strFileName != "..")
			{
				fs::file_time_type time = fs::last_write_time(entry.path());
				std::time_t cftime = to_time_t(time);

				double difference = std::difftime(now_c, cftime) / (60 * 60 * 24);		
				if ((int)difference > iRemainUselessFileDays)
				{
					strFilePath = entry.path().string();

					if (entry.status().type() == fs::file_type::directory)
					{
						mapDeleteFiles[strFilePath] = true;
					}
					else
					{
						mapDeleteFiles[strFilePath] = false;
					}
					bDel = true;
				}
			}
		}
	}

	{
		auto now = std::chrono::system_clock::now();
		time_t now_c = std::chrono::system_clock::to_time_t(now);
		tm now_tm;
		localtime_s(&now_tm, &now_c);

		if (IsInTimeZone(now_tm.tm_hour, iCleanDiskStartTime, iCleanDiskEndTime))
		{
			std::string strVideoPath = m_strDBPathAbs;
			if (bVideoTag)
			{
				strVideoPath + "/" + m_strVideoTag + "/";
			}
			for (const auto& entry : fs::directory_iterator(strVideoPath))
			{
				strFileName = entry.path().filename().string();

				if (entry.status().type() == fs::file_type::directory
					&& strFileName != "." && strFileName != ".."
					&& strFileName.size() == 6 && IsNumberString(strFileName))
				{
					if (IsNumberString(strFileName))
					{
						for (int iDay = 1; iDay <= 31; iDay++)
						{
							strTemp = format("%02d", iDay);
							strFilePath = entry.path().string() + "\\" + strTemp;
							if (IsDirExist(strFilePath))
							{
								tm cFolderTime;
								memset(&cFolderTime, 0, sizeof(cFolderTime));
								cFolderTime.tm_year = stoi(strFileName.substr(0, 4)) - 1900;
								cFolderTime.tm_mon = stoi(strFileName.substr(4, 2)) - 1;
								cFolderTime.tm_mday = iDay;
								if (cFolderTime.tm_year >= 0)
								{
									double difference = std::difftime(now_c, std::mktime(&cFolderTime)) / (60 * 60 * 24);

									if (int(difference) > iRemainUselessFileDays)
									{
										DelAllPointDir(strFilePath, "录像", mapDeleteFiles);
										bDel = true;
									}
								}
							}
						}
					}
				}
				else if (strFileName != "." && strFileName != "..")
				{
					fs::file_time_type time = fs::last_write_time(entry.path());
					std::time_t cftime = to_time_t(time);

					double difference = std::difftime(now_c, cftime) / (60 * 60 * 24);
					if ((int)difference > iRemainUselessFileDays)
					{
						strFilePath = entry.path().string();

						if (entry.status().type() == fs::file_type::directory)
						{
							mapDeleteFiles[strFilePath] = true;
						}
						else
						{
							mapDeleteFiles[strFilePath] = false;
						}
						bDel = true;
					}
				}
			}
		}
	}


	//start delete
	auto bDelRetu = false;
	for (auto& it : mapDeleteFiles)
	{
		if (it.second)
		{
			bDelRetu = DeleteDirectory(it.first);

			strLog = format("[DiskMng]delete dir:%s[%s]", strFilePath.c_str(), bDelRetu ? "success" : "failed");
			LogStatic(strLog);
		}
		else
		{
			bDelRetu = DeleteFile(it.first);
			strLog = format("[DiskMng]delete file:%s[%s]", strFilePath.c_str(), bDelRetu ? "success" : "failed");
			LogStatic(strLog);
		}
	}

	return bDel;
}

void CCleanDisk::SetPath(const std::string &strProjectPath, const std::string &strDBPathAbs, const std::string& strVideoTag)
{
	m_strProjectPath = strProjectPath;
	m_strDBPathAbs = strDBPathAbs;
	m_strVideoTag = strVideoTag;
}

void CleanDiskFun(const std::string &strProjectPath, const std::string& strVideoTag, const std::string &strDBPathAbs, const std::function<void(void)> &fun,
	int dwRemainDisk, int iCleanDiskStartTime, int iCleanDiskEndTime, int iRemainUselessFileDays, int iRemainUsefulFileDays)
{
	CCleanDisk CleanDisk;
	CleanDisk.SetPath(strProjectPath, strDBPathAbs, strVideoTag);

	dwRemainDisk = dwRemainDisk * 1024;
	int dwCurLeftDisk = 0;

	if (iCleanDiskStartTime < 0 || iCleanDiskStartTime >= 24) iCleanDiskStartTime = 2;
	if (iCleanDiskEndTime < 0 || iCleanDiskEndTime >= 24) iCleanDiskEndTime = 4;
	if (CleanDisk.g_nCleanStatus == 3)
	{
		CleanDisk.g_nCleanStatus = 0;
		if (fun)
			fun();
		return;
	}
	CleanDisk.g_nCleanStatus = 2;
	if (fun)
		fun();

	while (true)
	{
		int nSecond = 30;
		while (nSecond > 0)
		{
			if (CleanDisk.g_nCleanStatus == 3) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			nSecond--;
		}
		if (CleanDisk.g_nCleanStatus == 3) break;
		try
		{
			do
			{
				auto now = std::chrono::system_clock::now();
				time_t now_c = std::chrono::system_clock::to_time_t(now);
				tm now_tm;
				localtime_s(&now_tm, &now_c);
				
				{
					if (CleanDisk.IsInTimeZone(now_tm.tm_hour, iCleanDiskStartTime, iCleanDiskEndTime) == false)
						continue;
	
					fs::space_info space = fs::space(strProjectPath);
					dwCurLeftDisk = int(space.free >> 20);
					if (dwCurLeftDisk >= dwRemainDisk)
						continue;
				}
	
				int nRemainUselessFileDays = iRemainUselessFileDays;
				CleanDisk.LogStatic("start clean log disk");

				auto bDel = CleanDisk.CheckAndDelUselessFile(iCleanDiskStartTime, iCleanDiskEndTime, nRemainUselessFileDays);
				if (CleanDisk.g_nCleanStatus == 3) break;
				while (false == bDel && nRemainUselessFileDays > 0)
				{
					CleanDisk.LogStatic("no clean log disk, force delete one day log");
	
					bDel = CleanDisk.CheckAndDelUselessFile(iCleanDiskStartTime, iCleanDiskEndTime, --nRemainUselessFileDays); 
					if (CleanDisk.g_nCleanStatus == 3) break;
				}
				CleanDisk.LogStatic("end clean log disk");
	
			} while (false);
	
			do
			{
				auto now = std::chrono::system_clock::now();
				time_t now_c = std::chrono::system_clock::to_time_t(now);
				tm now_tm;
				localtime_s(&now_tm, &now_c);
	
				{
					if (CleanDisk.IsInTimeZone(now_tm.tm_hour, iCleanDiskStartTime, iCleanDiskEndTime) == false)
						continue;
	
					fs::space_info space = fs::space(strDBPathAbs);
					dwCurLeftDisk = int(space.free >> 20);
					if (dwCurLeftDisk >= dwRemainDisk)
						continue;
				}
				int nRemainUselessFileDays = iRemainUselessFileDays;
				int nRemainUsefulFileDays = iRemainUsefulFileDays;
	
				CleanDisk.LogStatic("start clean data disk");
				
				auto bDel = CleanDisk.CheckAndDelUsefulFile(iCleanDiskStartTime, iCleanDiskEndTime, nRemainUselessFileDays, nRemainUsefulFileDays);
				if (CleanDisk.g_nCleanStatus == 3) break;
				while (false == bDel && nRemainUselessFileDays > 0 && nRemainUsefulFileDays > 0)
				{
					CleanDisk.LogStatic("no clean data disk, force delete one day data");
					bDel = CleanDisk.CheckAndDelUsefulFile(iCleanDiskStartTime, iCleanDiskEndTime, --nRemainUselessFileDays, --nRemainUsefulFileDays);
					if (CleanDisk.g_nCleanStatus == 3) break;
				}
				CleanDisk.LogStatic("end clean data disk");

				if (!strVideoTag.empty())
				{
					nRemainUsefulFileDays = iRemainUsefulFileDays;
					CleanDisk.LogStatic("start clean video data disk");

					auto bDel = CleanDisk.CheckAndDelUsefulFile(iCleanDiskStartTime, iCleanDiskEndTime, nRemainUselessFileDays, nRemainUsefulFileDays, true);
					if (CleanDisk.g_nCleanStatus == 3) break;
					while (false == bDel && nRemainUselessFileDays > 0 && nRemainUsefulFileDays > 0)
					{
						CleanDisk.LogStatic("no clean video data disk, force delete one day video data");
						bDel = CleanDisk.CheckAndDelUsefulFile(iCleanDiskStartTime, iCleanDiskEndTime, --nRemainUselessFileDays, --nRemainUsefulFileDays, true);
						if (CleanDisk.g_nCleanStatus == 3) break;
					}
					CleanDisk.LogStatic("end clean video data disk");
				}

			} while (false);
		}
		catch (const fs::filesystem_error &ex) {
			auto strLog = CCleanDisk::format("[DiskMng]exception: %s", ex.what());
			CleanDisk.LogStatic(strLog);
		}
		catch (...)
		{
		}
	}

	CleanDisk.g_nCleanStatus = 0;
	if (fun)
		fun();
}