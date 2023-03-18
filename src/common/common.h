#pragma once

#include <time.h>
#include <queue>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <iostream>
#include <exception>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <map>
#include <regex>
#include <queue>
#define WIN32_LEAN_AND_MEAN
#ifdef WINDOWS
#include <windows.h>
#include <Commdlg.h>
#include <ShlObj_core.h>
#include <SetupAPI.h>
#include <devguid.h>
#pragma comment (lib, "Setupapi.lib")
#elif LINUX
#include <uuid/uuid.h>
#include <unistd.h>
#include <iconv.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#endif

using namespace std;
struct TIME {
	unsigned short wYear;
	unsigned short wMonth;
	unsigned short wDay;
	unsigned short wHour;
	unsigned short wMinute;
	unsigned short wSecond;
	unsigned short wMilliseconds;
	unsigned short wDayOfWeek;

	TIME() {
		memset(this, 0, sizeof(this));
	}

	bool operator==(TIME& right){
		return 0 == memcmp(this, &right, sizeof(TIME));
	}
};
class semaphore
{
public:
	semaphore(int count_ = 0) : count(count_) {}
	void notify()
	{
		std::unique_lock<std::mutex> lock(mtx);
		count++;
		cv.notify_one();
	}

	void reset() {
		count = 0;
	}

	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx);
		while (count == 0)
		{
			cv.wait(lock);
		}
		//The while loop can be replaced as below.
		//cv.wait ( lock, [&] () { return this->count > 0; } );
		count--;
	}

	bool wait_for(int milliSec)
	{
		std::unique_lock<std::mutex> lock(mtx);
		while (count == 0)
		{
			cv_status status = cv.wait_for(lock, std::chrono::milliseconds(milliSec));
			if (status == cv_status::timeout)
				return false;
		}
		//The while loop can be replaced as below.
		//cv.wait ( lock, [&] () { return this->count > 0; } );
		count--;
		return true;
	}
private:
	std::mutex mtx;
	std::condition_variable cv;
	int count;
};


namespace common {
	string& getCharCodec();

	void setCharCodec(string codec);

	const unsigned char auchCRCHi[] =
	{
		0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
		0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
		0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
		0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
		0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
		0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
		0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
		0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
		0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
		0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
		0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
		0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
		0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
		0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
		0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
		0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
		0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
		0x40
	};


	const unsigned char auchCRCLo[] =
	{
		0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
		0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
		0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
		0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
		0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
		0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
		0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
		0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
		0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
		0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
		0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
		0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
		0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
		0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
		0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
		0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
		0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
		0x40
	};

	unsigned short N_CRC16(unsigned char* updata, long long len);
	void endianSwap(char* pData, int len);
}

#define ASSERT(exp) if((exp) == false){LOG("[error][Assert  ] fail, file:%s,line:%d,function:%s", __FILE__, __LINE__,__func__);};
#define EXCEPTION(e) LOG("[warn]Exception raised, info:%s,file:%s,line:%d,function:%s",e.what(), __FILE__, __LINE__,__func__)

namespace str {
	std::string format(const char* pszFmt,... );
}

namespace charCodec {
	//gbk,utf8 <-> unicode
	string utf16_to_utf8(wstring instr);
	string utf16_to_gb(wstring instr);
	wstring utf8_to_utf16(string instr);
	wstring gb_to_utf16(string instr);

	//gbk <-> utf8
	string utf8_to_gb(string instr);
	string gb_to_utf8(string instr);

	string utf16Str_to_utf8(string s);

	//gbk checks   GB2312 value region  A1A1－FEFE  for chinese chars is B0A1-F7FE。
	bool hasGB2312(string s);
	bool isValidGB2312(string s, size_t& errorPos, string& errorChar);
	bool isValidGB2312(string s);

	//tds local codec can  be utf8 or gbk.
	string utf16_to_tds(wstring instr);
	wstring tds_to_utf16(string instr);
	string tds_to_utf8(string instr);
	string tds_to_gb(string instr);
	string gb_to_tds(string instr);
	string utf8_to_tds(string instr);
}
namespace str {
	string trimPrefix(string s, string prefix = " ");
	string trimSuffix(string s, string suffix = " ");
	string trim(std::string s, string toTrim = " ");
	string replace(string str, const string to_replaced, const string newchars);
	bool isDigits(char* pData, int len);
	bool isDigits(string s);
	std::string parseEscapeChar(string s);
	int split(std::vector<std::string>& dst, const std::string& src, std::string separator);
	string removeChar(string str, char c);
	string trimFloat(string str);
	string fromFloat(double f);
	vector<char> toChars(string str);
	vector<unsigned char> toBytes(string str);
	string bytesToHexStr(vector<char>& bytes);
	string bytesToHexStr(vector<unsigned char>& bytes);
	vector<unsigned char> hexStrToBytes(string hexStr);
	string bytesToHexStr(char* p, size_t len, string splitter = " ");
	string bytesToHexStr(unsigned char* p, size_t len, string splitter = " ");
	string fromInt(int v);
	string fromInt(long long v);
	string fromInt(size_t v);
	string fromBuff(const char* p, size_t len);
	int toInt(string s);
	string encodeAscII(string s);
	bool isInteger(string s);
	bool isIp(string s);
	bool parseIpPort(string s, string& ip, int& port);
	bool In(wchar_t   start, wchar_t   end, wchar_t   code);
	char  getShenMu(wchar_t n);
	bool hanZi2Pinyin(string hanZi, string& pinyin,bool upperCase = false);
}

namespace timeopt {
	TIME now();
	void now(TIME& t);
	void now(TIME* t);
	string stTimeToStr(TIME time);
	time_t SysTime2Unix(TIME sDT);
	TIME Unix2SysTime(time_t iUnix);
	TIME str2st(string str);
	int HMS2Sec(string hms);
	bool isRelative(string time);
	unsigned long duration2sec(string strTime);
	int dhmsSpan2Seconds(string timeSpan);
	string rel2abs(string time);
	string st2str(TIME t,bool enableMS=false);

	string st2strWithMilli(TIME t);
	string TimeToYMD(const TIME time);
	time_t CalcTimePassSecond(TIME lastTime);
	long long CalcTimePassMilliSecond(TIME lastTime);
	time_t CalcTimeDiffSecond(TIME newTime, TIME oldTime);
	time_t getTick();
	void setAsTimeOrg(TIME& st);
	bool isValidTime(TIME& st);
	string nowStr(bool enableMS = false);
	string nowStrIso();
	string nowStrForFile(bool enableMS = false);
	TIME addTime(TIME base, int h, int m, int s);
	bool isValidTimeStr(string time);
}


namespace sys {
	struct COM_INFO {
		string portNum;
		string desc;
	};
	vector<string> getCOMList();
	vector<COM_INFO> getCOMInfoList();
	string getLastError(string szReason = "");
}


namespace fs {
	//带后缀 .XXX 作为文件路径
	//不带后缀作为文件夹路径。不要输入无后缀的文件路径
	void createFolderOfPath(string strFile);
	string appName();
	string appPath();
	string toAbsolutePath(string str);
	string getExt(string strFilePath);
	bool readFile(string path, char*& pData, int& len);
	bool readFile(string path, unsigned char*& pData, int& len);
	bool readFile(string path, string& data);
	bool writeFile(string path, char* data, size_t len);
	bool appendFile(string path, char* data, size_t len);
	bool appendFile(string path, string data);
	bool writeFile(string path, string& data);
	bool fileExist(string pszFileName);
	bool deleteFile(string path);

	struct FILE_INFO {
		string modifyTime;
		string createTime;
		size_t len;
		string accessTime;
		string name;
		string path;
		string folderPath;
	};

	void getFolderList(vector<FILE_INFO>& list, string strFolder);
	void getFileList(vector<FILE_INFO>& list, string strFolder, bool includeFolder = false, bool recursive = false,string suffix="*");
	void getFileList(vector<string>& list, string strFolder, bool includeFolder = false, bool recursive = false);
}
namespace path {
	string normalization(string& s);
}

namespace common {
	string guid();
	double randomFloat(double min, double max);
	int randomInt(int min, int max);
}


namespace buffer {
	unsigned char setBit(unsigned char byte, int bitIdx, int val);
	unsigned char setBit(unsigned char byte, int bitIdx, bool val);
}

string getbuildtime();
string getbuilddate();
void setThreadName2(string name);
void setThreadName(string name);

#define _GB(s) charCodec::utf8_to_gb(s).c_str()
