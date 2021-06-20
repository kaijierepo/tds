#pragma once
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <queue>
#define WIN32_LEAN_AND_MEAN
#ifdef WINDOWS
#include <windows.h>
#endif

using namespace std;

namespace sys {
	LPCSTR getLastError(LPCTSTR szReason);
};

namespace common
{
	unsigned short N_CRC16(unsigned char* updata, long long len);
};

namespace charCodec {
	string utf8toAnsi(string instr);
	wstring utf8toUtf16(string instr);
	string ansi2Utf8(string instr);
	string ToUtf8(LPCTSTR wstr);
}

namespace timeopt {
	DWORD SysTime2Unix(SYSTEMTIME sDT);
	SYSTEMTIME Unix2SysTime(DWORD iUnix);
	SYSTEMTIME str2st(string str);
	int HMS2Sec(string hms);
	bool isRelative(string time);
	DWORD duration2sec(string strTime); //1d2h3m40s 的格式
	string rel2abs(string time);//相对与当前时间的1d2h3m40s格式
	string st2str(SYSTEMTIME t);
	string TimeToYMD(const SYSTEMTIME time);
	int CalcTimePassSecond(SYSTEMTIME lastTime);
	string nowStr(bool enableMS=false);
	time_t getTick();  // unix time in milli second
}


//字符串相关操作
namespace str {
	string& trim(std::string& s, string toTrim = " ");
	string& trimPrefix(string& s, string prefix);
	string& trimSuffix(string& s, string suffix);
	string& replace(string& str, const string to_replaced, const string newchars);
	std::string format(const char* pszFmt, ...);
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);
	void removeChar(string& str, char c);
	string trimFloat(string str);
	string fromFloat(float f);
	vector<char> toBytes(string str);
	string fromBytes(vector<char>& bytes);
	string fromBytes(char* p, int len);
	string fromInt(int v);
	int toInt(string s);
	bool isInteger(string s);
	bool isIp(string s);
};


namespace path {
	string normalization(string& s);
}

//文件系统相关操作
namespace fs {
	void createFolderOfPath(string strFile);
	string toAbsolutePath(string str);
	string appPath();
	string getExt(string path);
	bool readFile(string path, string& data);
	bool writeFile(string path, char* data, int len);
	bool appendFile(string path, string data);
	bool appendFile(string path, char* data, int len);
	bool writeFile(string path, string& data);
	bool fileExist(string pszFileName);
	bool deleteFile(string path);
	vector<string> getFileList(string strFolder);
}

namespace tds {
	string getConf(string confName, string defaultVal= "");
	int getConfInt(string confName, int defaultVal = 0);
}


