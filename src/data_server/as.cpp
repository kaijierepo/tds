#include "as.h"
#include "tdb.h"

#include <regex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#include <Commdlg.h>
#include <ShlObj_core.h>
#include <SetupAPI.h>
#include <devguid.h>
#pragma comment (lib, "Setupapi.lib")
#else
#include <unistd.h>
//#include <iconv.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#endif

almServer almSrv;
almServer almSrv_dev;
almServer almSrv_fau;
almServer almSrv_fauDev;

namespace as {
	const char* rPC_NULL = "null";
	const char* rPC_OK = "\"ok\"";
	const char* rPC_TIMEOUT = "\"timeout\"";
	const char* rPC_FAIL = "\"fail\"";
	//#define RPC_STR(s) "\""+s+"\""

	string Date::toStr()
	{
		string s = as_str::format("%04d-%02d-%02d", wYear, wMonth, wDay);
		return s;
	}

	void Date::fromStr(string s)
	{
		int y, m, d;
		sscanf(s.c_str(), "%4d-%2d-%2d", &y, &m, &d);
		wYear = y;
		wMonth = m;
		wDay = d;
	}

	string HMS::toStr()
	{
		string s = as_str::format("%02d:%02d:%02d", wHour, wMinute, wSecond);
		return s;
	}

	void HMS::fromStr(string str)
	{
		int h, m, s;
		sscanf(str.c_str(), "%2d:%2d:%2d", &h, &m, &s);
		wHour = h;
		wMinute = m;
		wSecond = s;
	}

	void TIME::setDate(Date t)
	{
		wYear = t.wYear;
		wMonth = t.wMonth;
		wDay = t.wDay;
		wDayOfWeek = t.wDayOfWeek;
	}

	void TIME::setHMS(HMS t)
	{
		wHour = t.wHour;
		wMinute = t.wMinute;
		wSecond = t.wSecond;
		wMilliseconds = t.wMilliseconds;
	}

	string TIME::toStr(bool enableMilli)
	{
		return as_timeopt::st2str(*this, enableMilli);
	}

	void TIME::fromStr(string s) {
		*this = as_timeopt::str2st(s);
	}

	string TIME::toDateStr()
	{
		string s = as_str::format("%04d-%02d-%02d", wYear, wMonth, wDay);
		return s;
	}

	string TIME::toTimeStr()
	{
		string s = as_str::format("%02d:%02d:%02d", wHour, wMinute, wSecond);
		return s;
	}

	string TIME::toStampFull()
	{
		string s = as_str::format("%04d-%02d-%02d %02d%02d%02d", wYear, wMonth, wDay, wHour, wMinute, wSecond);
		return s;
	}

	time_t  TIME::toUnixTimeStamp() {
		tm temptm = { wSecond, wMinute, wHour,wDay,wMonth - 1,wYear - 1900,wDayOfWeek, 0, 0 };
		time_t unixTime = mktime(&temptm);
		return unixTime;
	}

	void  TIME::fromUnixTimeStamp(time_t unixTime) {
		static std::mutex mtx;
		mtx.lock();
		tm time_tm = *localtime(&unixTime);  //线程安全linux下推荐用localtime_r，win下推荐用localtime_s，此处为方便直接加个锁
		mtx.unlock();

		wYear = time_tm.tm_year + 1900;
		wMonth = time_tm.tm_mon + 1;
		wDay = time_tm.tm_mday;
		wHour = time_tm.tm_hour;
		wMinute = time_tm.tm_min;
		wSecond = time_tm.tm_sec;
		wDayOfWeek = time_tm.tm_wday;
	}

	string TIME::toStampHMS()
	{
		string s = as_str::format("%02d%02d%02d", wHour, wMinute, wSecond);
		return s;
	}


	string getUUID()
	{
		//线程id（8B）+纳妙时间戳(16B) +  同线程ID的递增数(4B)  //允许同个线程同个纳秒时间点执行65536次 cpu主频65536 GHz以上才出错
		static map<uint32_t, int> mapCnt; //重启后重新从0计数

		std::thread::id this_id = std::this_thread::get_id();
		std::hash<std::thread::id> hasher;
		uint32_t thdId = static_cast<uint32_t>(hasher(this_id));

		std::chrono::system_clock::duration d = std::chrono::system_clock::now().time_since_epoch();
		std::chrono::nanoseconds nan = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
		uint64_t nanTime = nan.count();

		int cnt = 0;
		if (mapCnt.find(thdId) == mapCnt.end()) {
			mapCnt[thdId] = 0;
		}
		else {
			mapCnt[thdId]++;
			cnt = mapCnt[thdId];
		}

		std::stringstream stream0;  stream0 << std::setfill('0') << std::setw(8) << std::hex << thdId;
		std::stringstream stream1;  stream1 << std::setfill('0') << std::setw(16) << std::hex << nanTime;
		std::stringstream stream2;  stream2 << std::setfill('0') << std::setw(4) << std::hex << cnt;
		return stream0.str() + stream1.str() + stream2.str();
	}

	bool matchTag(string pattern, const string& src)
	{
		if (pattern.find("*") == string::npos) {
			if (pattern == src)
				return true;
		}
		else {
			string& strReg = pattern;
			strReg = as_str::replace(strReg, ".", "\\.");
			strReg = as_str::replace(strReg, "*", ".*");
			std::regex reg(strReg);
			if (std::regex_match(src, reg) == true) {
				return true;
			}
		}
		return false;
	}

	//src和pattern相等 或 *匹配
	bool generalMatch(string pattern, const string& src)
	{
		if (pattern.find("*") == string::npos) {
			if (pattern == src)
				return true;
		}
		else {
			string& strReg = pattern;
			strReg = as_str::replace(strReg, "*", ".*");
			std::regex reg(strReg);
			if (std::regex_match(src, reg) == true) {
				return true;
			}
		}
		return false;
	}

	int _vscprintf_cross(const char* format, va_list pargs) {
		int retval;
		va_list argcopy;
		va_copy(argcopy, pargs);
		retval = vsnprintf(NULL, 0, format, argcopy);
		va_end(argcopy);
		return retval;
	}
}

namespace as_timeopt {
	as::TIME Unix2SysTime(time_t iUnix, int milli)
	{
		static std::mutex mtx;
		mtx.lock();
		tm time_tm = *localtime(&iUnix);  //线程安全linux下推荐用localtime_r，win下推荐用localtime_s，此处为方便直接加个锁
		mtx.unlock();

		as::TIME t;
		t.wYear = time_tm.tm_year + 1900;
		t.wMonth = time_tm.tm_mon + 1;
		t.wDay = time_tm.tm_mday;
		t.wHour = time_tm.tm_hour;
		t.wMinute = time_tm.tm_min;
		t.wSecond = time_tm.tm_sec;
		t.wMilliseconds = milli;
		t.wDayOfWeek = time_tm.tm_wday;
		return t;
	}

	as::TIME now() {
		auto now = std::chrono::system_clock::now();
		//通过不同精度获取相差的毫秒数 <1000毫秒值
		unsigned short milli = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
			- std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
		time_t tt = std::chrono::system_clock::to_time_t(now);

		return Unix2SysTime(tt, milli);
	}
	void now(as::TIME& t) {
		t = now();
	}

	void now(as::TIME* t) {
		*t = now();
	}
	as::TIME str2st(string str)
	{
		as::TIME t;
		memset(&t, 0, sizeof(t));
		int y, m, d, h, min, s, milli;
		if (str.length() > 3 && str[2] != '-' && str[2] != ':') {
			//2023-12-31T16:00:00.000Z
			if (str.length() == 24) {
				sscanf(str.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%dZ",
					&y,
					&m,
					&d,
					&h,
					&min,
					&s,
					&milli);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min; t.wSecond = s; t.wMilliseconds = milli;
			}
			//2022-02-22 11:11:11.123   23bytes
			else if (str.length() == 23) {
				sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d:%2d.%d",
					&y,
					&m,
					&d,
					&h,
					&min,
					&s,
					&milli);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min; t.wSecond = s; t.wMilliseconds = milli;
			}
			//2022-02-22 11:11:11   19bytes
			else if (str.length() == 19)
			{
				sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
					&y,
					&m,
					&d,
					&h,
					&min,
					&s);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min; t.wSecond = s;
			}
			//2022-02-22 11:11   16bytes
			else if (str.length() == 16)
			{
				sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d",
					&y,
					&m,
					&d,
					&h,
					&min);
				t.wYear = y; t.wMonth = m; t.wDay = d; t.wHour = h; t.wMinute = min;
			}
			else if (str.length() == 10) //2022-02-02
			{
				sscanf(str.c_str(), "%4d-%2d-%2d",
					&y,
					&m,
					&d);
				t.wYear = y; t.wMonth = m; t.wDay = d;
			}
			else if (str.length() == 8) //12:11:11
			{
				sscanf(str.c_str(), "%2d:%2d:%2d",
					&h,
					&min,
					&s);
				t.wHour = h; t.wMinute = min; t.wSecond = s;
			}
		}
		else if (as_str::isDigits(str)) {
			time_t tt = atoi(str.c_str());
			t = Unix2SysTime(tt);
		}

		return t;
	}

	string st2str(as::TIME t, bool enableMS)
	{
		if (enableMS) {
			string str = as_str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
			return str;
		}
		else {
			string str = as_str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond);
			return str;
		}
	}

	string stTimeToStr(as::TIME time)
	{
		string str = as_str::format("%4d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		return str;
	}

	string nowStr(bool enableMS)
	{
		as::TIME t = now();

		return st2str(t, enableMS);
	}
}

namespace as_str {
	string trimPrefix(string s, string prefix)
	{
		if (prefix == "")
			return s;

		while (1)
		{
			if (s.find(prefix) == 0)
			{
				s = s.substr(prefix.length(), s.length() - prefix.length());
			}
			else
			{
				break;
			}
		}

		return s;
	}

	string trimSuffix(string s, string suffix)
	{
		if (suffix == "")
			return s;

		while (1)
		{
			size_t ipos = s.rfind(suffix);
			if (ipos != string::npos && ipos + suffix.length() == s.length())
			{
				s = s.substr(0, ipos);
			}
			else
			{
				break;
			}
		}
		return s;
	}

	string trim(std::string s, string toTrim)
	{
		s = trimPrefix(s, toTrim);
		s = trimSuffix(s, toTrim);
		return s;
	}

	std::string format(const char* pszFmt, ...)
	{
		std::string str;
		va_list args;
		va_start(args, pszFmt);
		{
			int nLength = as::_vscprintf_cross(pszFmt, args);
			nLength += 1;  //上面返回的长度是包含\0，这里加上
			std::vector<char> vectorChars(nLength);
			vsnprintf(vectorChars.data(), nLength, pszFmt, args);
			str.assign(vectorChars.data());
		}
		va_end(args);
		return str;
	}

	string replace(string str, const string to_replaced, const string newchars)
	{
		for (string::size_type pos(0); pos != string::npos; pos += newchars.length())
		{
			pos = str.find(to_replaced, pos);
			if (pos != string::npos)
				str.replace(pos, to_replaced.length(), newchars);
			else
				break;
		}
		return   str;
	}

	bool isDigits(char* pData, int len) {
		for (int i = 0; i < len; i++)
		{
			char c = pData[i];
			if (c >= '0' && c <= '9')
			{
				continue;
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	bool isDigits(string s)
	{
		for (int i = 0; i < s.length(); i++)
		{
			char c = s[i];
			if (c >= '0' && c <= '9')
			{
				continue;
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	int split(std::vector<std::string>& dst, const std::string& src, std::string separator)
	{
		if (src.empty() || separator.empty())
			return 0;

		int nCount = 0;
		std::string temp;
		size_t pos = 0, offset = 0;

		// 分割第1~n-1个
		while ((pos = src.find(separator, offset)) != std::string::npos)
		{
			temp = src.substr(offset, pos - offset);
			if (temp.length() > 0) {
				dst.push_back(temp);
				nCount++;
			}
			else
			{
				dst.push_back("");
				nCount++;
			}
			offset = pos + separator.size();
		}

		// 分割第n个
		temp = src.substr(offset, src.length() - offset);
		if (temp.length() > 0) {
			dst.push_back(temp);
			nCount++;
		}

		return nCount;
	}

	vector<unsigned char> hexStrToBytes(string hexStr)
	{
		vector<unsigned char> ary;
		hexStr = as_str::removeChar(hexStr, ' ');
		if (0 != hexStr.length() % 2)
		{
			hexStr += "0";
		}
		size_t strLen = 0;
		strLen = hexStr.length();
		transform(hexStr.begin(), hexStr.end(), hexStr.begin(), ::toupper);

		for (size_t i = 0; i < strLen / 2; i++)
		{
			char cByteHigh = hexStr.at(i * 2);
			char cByteLow = hexStr.at(i * 2 + 1);
			int bHigh = 0, bLow = 0;
			if (cByteHigh >= 'A')
			{
				bHigh = cByteHigh - 'A' + 10;
			}
			else
			{
				bHigh = cByteHigh - '0';
			}

			if (cByteLow >= 'A')
			{
				bLow = cByteLow - 'A' + 10;
			}
			else
			{
				bLow = cByteLow - '0';
			}

			int val = (bHigh * 16 + bLow);
			unsigned char b = (unsigned char)val;
			ary.push_back((unsigned char)b);
		}
		return ary;
	}

	string removeChar(string str, char c)
	{
		str.erase(std::remove(str.begin(), str.end(), c), str.end());
		return str;
	}
}

namespace as_fs {
	string GetDir(string strIn)
	{
#ifdef _WIN32
		std::string& str = strIn;
		std::string::size_type pos = str.find_last_of("\\/");
		std::string strDir = str;
		if (pos != std::string::npos) {
			strDir = str.substr(0, pos);
		}
		return strDir;
#else
		return "";
#endif
	}

	void CreateDirectoryPlus_old(string str)
	{
#ifdef _WIN32
		if (str.empty()) return;
		::CreateDirectory(str.c_str(), NULL);

		DWORD dwAttrib = GetFileAttributes(str.c_str());
		bool bDirExist = INVALID_FILE_ATTRIBUTES != dwAttrib && 0 != (dwAttrib & FILE_ATTRIBUTE_DIRECTORY);

		if (!bDirExist) {
			CreateDirectoryPlus_old(GetDir(str));
			::CreateDirectory(str.c_str(), NULL);
		}
#else
		return;
#endif
	}


	//带后缀 .XXX 作为文件路径
//不带后缀作为文件夹路径。不要输入无后缀的文件路径
//filesystem::path 统一用 wstring utf16输入，可以做到windows与linux兼容
	bool createFolderOfPath(string strFile)
	{
		strFile = as_str::replace(strFile, "\\", "/");
		strFile = as_str::replace(strFile, "////", "/");
		strFile = as_str::replace(strFile, "///", "/");
		strFile = as_str::replace(strFile, "//", "/");

		size_t iDotPos = strFile.rfind('.');
		size_t iSlashPos = strFile.rfind('/');
		if (iDotPos != string::npos && iDotPos > iSlashPos)//是一个文件
		{
			strFile = strFile.substr(0, iSlashPos);
		}
		//如果路径的末尾是/，创建成功也会返回false,因此删除末尾的 /
		if (iSlashPos == strFile.length() - 1) {
			strFile = strFile.substr(0, iSlashPos);
		}

#ifdef _WIN32
#ifndef _WINXP

#if __cplusplus <= 201402L
		CreateDirectoryPlus_old(strFile);
		return true;
#else
		return filesystem::create_directories(as_charCodec::tds_to_utf16(strFile));
#endif

#endif
#else
		filesystem::path p = strFile;
		return filesystem::create_directories(p);
#endif
	}


	bool readFile(string path, char*& pData, int& len)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp, as_charCodec::tds_to_utf16(path).c_str(), L"rb");
#else
		fp = fopen(path.c_str(), "rb");
#endif
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			len = ftell(fp);
			pData = new char[len];
			fseek(fp, 0, SEEK_SET);
			fread(pData, 1, len, fp);
			fclose(fp);
			return true;
		}
		return false;
	}
	bool readFile(string path, unsigned char*& pData, int& len)
	{
		char* p = nullptr;
		bool bRet = readFile(path, p, len);
		pData = (unsigned char*)p;
		return bRet;
	}
	bool readFile(string path, string& data)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp, as_charCodec::tds_to_utf16(path).c_str(), L"rb");
#else
		fp = fopen(path.c_str(), "rb");
#endif
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			long len = ftell(fp);
			char* pdata = new char[len + 2];
			memset(pdata, 0, len + 2);
			fseek(fp, 0, SEEK_SET);
			fread(pdata, 1, len, fp);
			data = pdata;
			fclose(fp);
			delete[] pdata;
			return true;
		}
		return false;
	}
	bool writeFile(string path, unsigned char* data, size_t len)
	{
		return writeFile(path, (char*)data, len);
	}
	bool writeFile(string path, char* data, size_t len)
	{
		CreateDirectoryPlus_old(path);

		FILE* fp = nullptr;
#ifdef _WIN32
		wstring wpath = as_charCodec::tds_to_utf16(path);
		_wfopen_s(&fp, wpath.c_str(), L"wb");
#else
		fp = fopen(path.c_str(), "wb");
#endif
		if (fp)
		{
			fwrite(data, 1, len, fp);
			fclose(fp);
			return true;
		}
		else
		{
#ifdef _WIN32
			string err = as_sys::getLastError();
			err = as_charCodec::tds_to_gb(err);
			printf("[error]%s", err.c_str());
#endif
		}
		return false;
	}

	bool writeFile(string path, string& data)
	{
		return writeFile(path, (char*)data.c_str(), data.length());
	}
}

namespace as_charCodec {

	string utf16_to_utf8(wstring instr) //utf-8-->ansi
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 4 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, instr.c_str(), -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete charstr;
#else

#endif
		return str;
	}
	string utf16_to_gb(wstring instr)
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, instr.c_str(), -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete charstr;
#else

#endif
		return str;
	}
	wstring utf8_to_utf16(string instr) //utf-8-->ansi
	{
		wstring str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		str = wcharstr;
		delete[] wcharstr;

#else

#endif
		return str;
	}
	wstring gb_to_utf16(string instr)
	{
		wstring str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		str = wcharstr;
		delete wcharstr;
#else

#endif
		return str;
	}

	string utf8_to_gb(string instr) //utf-8-->ansi
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, wcharstr, -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete wcharstr;
		delete charstr;
#else
		//int ret = 0;
		//size_t inlen = instr.size() + 1;
		//size_t outlen = 2*inlen;

		//// duanqn: The iconv function in Linux requires non-const char *
		//// So we need to copy the source string
		//char* inbuf = (char*)malloc(inlen);
		//memset(inbuf,0,inlen);
		//char* inbuf_hold = inbuf;   // iconv may change the address of inbuf
		//							// so we use another pointer to keep the address
		//memcpy(inbuf, instr.data(), instr.length());

		//char* outbuf =(char*)malloc(outlen);
		//memset(outbuf, 0, outlen);
		//iconv_t cd;
		//cd = iconv_open("GBK", "UTF-8");
		//if (cd != (iconv_t)-1) {
		//	ret = iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
		//	if (ret != 0) {
		//		printf("iconv failed err: %s\n", strerror(errno));
		//	}

		//	iconv_close(cd);
		//}
		//free(inbuf_hold);   // Don't pass in inbuf as it may have been modified

		//if(outbuf!=nullptr){
		//	str = outbuf;
		//	free(outbuf);
		//}
		str = instr;
#endif
		return str;
	}
	string gb_to_utf8(string instr) //ansi-->utf-8
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, wcharstr, -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete wcharstr;
		delete charstr;
#else
		//int ret = 0;
		//size_t inlen = instr.length() + 1;
		//size_t outlen = 2*inlen;

		//// duanqn: The iconv function in Linux requires non-const char *
		//// So we need to copy the source string
		//char* inbuf = (char*)malloc(inlen);
		//char* inbuf_hold = inbuf;   // iconv may change the address of inbuf
		//							// so we use another pointer to keep the address
		//memcpy(inbuf, instr.data(), instr.length());

		//char* outbuf = (char*)malloc(outlen);
		//memset(outbuf, 0, outlen);
		//iconv_t cd;

		//cd = iconv_open("UTF-8", "GBK");
		//if (cd != (iconv_t)-1) {
		//	ret = iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
		//	if (ret != 0)
		//		printf("iconv failed err: %s\n", strerror(errno));
		//	iconv_close(cd);
		//}
		//free(inbuf_hold);   // Don't pass in inbuf as it may have been modified
		//str = outbuf;
		//free(outbuf);
		str = instr;
#endif
		return str;
	}

	//GB2312 value region  A1A1－FEFE  for chinese chars is B0A1-F7FE。
	bool hasGB2312(string s)
	{
		for (size_t i = 0; i < s.length(); i++)
		{
			int b = (int)(unsigned char)s.at(i);
			if (b >= 0xA1 && b <= 0xFE) //gb2312
			{
				if (i + 1 < s.length())
				{
					int bNext = (int)(unsigned char)s.at(i + 1);
					if (bNext >= 0xA1 && bNext <= 0xFE)
					{
						return true;
					}
				}
			}
		}

		return false;
	}
	bool isValidGB2312(string s, size_t& errorPos, string& errorChar)
	{
		for (size_t i = 0; i < s.length();)
		{
			int b = (int)(unsigned char)s.at(i);
			if (b > 0 && b < 127) //ascii
			{
				i++;
				continue;
			}
			else
			{
				if (b >= 0xA1 && b <= 0xFE) //gb2312
				{
					if (i + 1 < s.length())
					{
						int bNext = (int)(unsigned char)s.at(i + 1);
						if (bNext >= 0xA1 && bNext <= 0xFE)
						{
							i += 2;
							continue;
						}
						else
						{
							errorPos = i;
							errorChar = as_str::format("%02X%02X", b, bNext);
							return false;
						}
					}
					else // invalid length
					{
						errorPos = i;
						errorChar = "invalid length";
						return false;
					}
				}
				else // wrong hex value
				{
					errorPos = i;
					errorChar = as_str::format("%02X", b);
					return false;
				}
			}
		}

		return true;
	}
	bool isValidGB2312(string s)
	{
		size_t pos = 0;
		string errorChar;
		return isValidGB2312(s, pos, errorChar);
	}

	string utf16Str_to_utf8(string s) {
		string u8Str;
		for (size_t i = 0; i < s.length() - 5; i++) {
			char c = s[i];
			if (c == '\\' && s[i + 1] == 'u') {
				string strCode = s.substr(i + 2, 4);
				wchar_t wchar;
				vector<unsigned char> vec = as_str::hexStrToBytes(strCode);
				wchar = vec[0] * 256 + vec[1];
				wstring wStr;
				wStr.push_back(wchar);
				string u8Char = as_charCodec::utf16_to_utf8(wStr);
				u8Str += u8Char;
				i += 5;
			}
			else {
				u8Str.push_back(c);
			}
		}

		return u8Str;
	}

	string utf16_to_tds(wstring instr)
	{
		string s;
		if (as_common::getCharCodec() == "gb2312")
		{
			s = utf16_to_gb(instr);
		}
		else
		{
			s = utf16_to_utf8(instr);
		}
		return s;
	}
	wstring tds_to_utf16(string instr)
	{
		wstring w;
		if (as_common::getCharCodec() == "gb2312")
		{
			w = gb_to_utf16(instr);
		}
		else
		{
			w = utf8_to_utf16(instr);
		}
		return w;
	}
	string tds_to_utf8(string instr)
	{
		string s;
		if (as_common::getCharCodec() == "gb2312")
		{
			s = gb_to_utf8(instr);
			return s;
		}
		else
		{
			return instr;
		}
	}
	string tds_to_gb(string instr)
	{
		string s;
		if (as_common::getCharCodec() == "gb2312")
		{
			return instr;
		}
		else
		{
			return utf8_to_gb(instr);
		}
	}
	string gb_to_tds(string instr)
	{
		string s;
		if (as_common::getCharCodec() == "gb2312")
		{
			return instr;
		}
		else
		{
			return gb_to_utf8(instr);
		}
	}
	string utf8_to_tds(string instr)
	{
		string s;
		if (as_common::getCharCodec() == "gb2312")
		{
			return utf8_to_gb(instr);
		}
		else
		{
			return instr;
		}
	}
}

namespace as_common {
	string& getCharCodec() {
		static string charCodec = "utf8";
		return charCodec;
	}
}

namespace as_sys {

	string getLastError(string szReason)
	{
		string szErrMsg = "";
#ifdef WINDOWS
		DWORD dwErrCode = GetLastError(); //之前的错误代码

		LPVOID lpMsgBuf = NULL;
		DWORD dwLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dwErrCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPWSTR)&lpMsgBuf,
			0,
			NULL
		);



		if (dwLen == 0)
		{
			DWORD dwFmtErrCode = GetLastError(); //FormatMessage 引起的错误代码
			szErrMsg = as_str::format("FormatMessage failed with %u\n", dwFmtErrCode);
		}

		if (lpMsgBuf)
		{
			wstring utf16msg = (LPWSTR)lpMsgBuf;
			string utf8Msg = as_charCodec::utf16_to_tds(utf16msg);
			szErrMsg = as_str::format("%s\n Code = %u, Mean = %s", szReason.c_str(), dwErrCode, utf8Msg.c_str());
		}

		if (lpMsgBuf)
		{
			// Free the buffer.
			LocalFree(lpMsgBuf);
			lpMsgBuf = NULL;
		}
#endif

		return szErrMsg;
	}

}

namespace as_TAG {
	string addRoot(string tag, string root)
	{
		if (root == "")
			return tag;

		//tag是相对于root的相对位号
		if (tag == "")
			return root;

		return root + "." + tag;
	}
}

almServer::almServer(void)
{
	m_bTestSrv = false;
}


almServer::~almServer(void)
{
}

void almServer::init()
{
	string s;
	if (as_fs::readFile(m_initParam.confPath + "/alarm.json", s) && s != "")
	{
		json jAlms = json::parse(s);
		for (int i = 0; i < jAlms.size(); i++)
		{
			json& jAlmDesc = jAlms[i];
			ALARM_TEMPLATE at;
			at.name = jAlmDesc["type"].get<string>();
			at.label = jAlmDesc["typeLabel"].get<string>();
			at.enable = true;
			if (jAlmDesc["enable"] != nullptr && jAlmDesc["enable"].get<bool>() == false) //报警屏蔽
			{
				at.enable = false;
			}
			m_mapCustomAlarmDesc[jAlmDesc["type"].get<string>()] = at;
		}
	}

	//tableStatus.init("\\alarms\\status");
	//tableUnack.init("\\alarms\\unack");

}

void almServer::init(const string& aCurPath, const string& aHisPath, AsInitParam& asInitParam)
{
	m_initParam = asInitParam;

	m_curPath = aCurPath;
	m_histPath = aHisPath;

	init();

	tableCurrent.init(aCurPath);
	tableCurrent.SetAlarmSrv(this);

	tableHist.init(aHisPath);
	tableHist.bOneFilePerMonth = true;
	tableHist.SetAlarmSrv(this);

	initMOAlarmStatus();
}

void almServer::recover(AS_ALARM_INFO& key)
{
	/*tableStatus.remove(key);

	AS_ALARM_INFO ai;
	if(tableUnack.query(key,ai))
	{
		ai.bRecover = 1;
		tableUnack.update(ai);
	}

	if(tableHist.query(key,ai))
	{
		ai.bRecover = 1;
		tableHist.update(ai);
	}*/

	AS_ALARM_INFO ai;
	json params;
	params["time"] = key.time;
	params["type"] = key.type;
	params["tag"] = key.tag;
	if (tableCurrent.query(params, ai))
	{
		ai.bRecover = 1;
		ai.stRecoverTime = key.stRecoverTime;
		if (ai.bAck && ai.bRecover)//删除已消除已确认报警
		{
			tableCurrent.remove(key);
		}
		else
			tableCurrent.update(ai);
	}
	if (tableHist.query(params, ai))
	{
		ai.bRecover = 1;
		ai.stRecoverTime = key.stRecoverTime;
		tableHist.update(ai);
	}

	json j = ai.toJson(this);
	//rpcSrv.notify("onAlarmRecover", j);  
	if (m_initParam.func_rpcHand_notify)
		m_initParam.func_rpcHand_notify("onAlarmRecover", j);
}

/*
2类as：  正式 和 测试
对于正式as
1）tag支持 小括号和 中括号
真正tag可放小括号里外面的表示备注或额外说明，或   中括号表示备注 外面的表示备注
即： 真正tag、xxx(真正tag)、真正tag[xxx]
2）支持基于真正tag的报警过滤
*/
void almServer::addAlarm(AS_ALARM_INFO ai)
{
	if (m_bTestSrv == false)
	{
		string sTag = ai.tag;
		if ((ai.tag.find("(") != string::npos || ai.tag.find(")") != string::npos)
			&& (ai.tag.find("[") != string::npos || ai.tag.find("]") != string::npos)) {
			//LOG("[报警服务]新报警,tag非法，小括号中括号不能同时存在,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log("[报警服务]新报警,tag非法，小括号中括号不能同时存在,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			return;
		}
		string::size_type pos_s = ai.tag.find("(");
		if (pos_s != string::npos) {
			string::size_type pos_e = ai.tag.find(")");
			if (pos_e != string::npos) {
				sTag = ai.tag.substr(pos_s + 1, pos_e - (pos_s + 1));
			}
		}
		else {
			string::size_type pos_s = ai.tag.find("[");
			if (pos_s != string::npos) {
				string::size_type pos_e = ai.tag.find("]");
				if (pos_e != string::npos) {

					sTag = ai.tag.substr(0, pos_s) + ai.tag.substr(pos_e + 1);
				}
			}
		}

		/*OBJ* pObj = prj.queryObj(sTag, "zh");
		if (pObj) {
			if (!pObj->m_bEnableAlarm)
			{
				LOG("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
				return;
			}
		}*/
		auto func_obj_isEnableAlarm = m_initParam.func_obj_isEnableAlarm;
		if (func_obj_isEnableAlarm != NULL && func_obj_isEnableAlarm(sTag, "zh") == false) {
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			//LOG("[报警服务]新报警,报警被禁用,%s,%s", sTag.c_str(), ai.toJson(this).dump().c_str());
			return;
		}

		//如果没有位号，报警默认不禁用
	}

	//LOG("[报警服务]新报警,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());
	auto func_log = m_initParam.func_log;
	if (func_log)
		func_log("[报警服务]新报警,%s,%s", ai.tag.c_str(), ai.toJson(this).dump().c_str());

	//事件报警重复性检查
	if (m_eventAlarmRepetitiveCheck) {
		json q;
		q["time"] = ai.time;
		as::RPC_SESSION rs;
		string s = rpc_getHistory(q, rs);
		json j = json::parse(s);
		if (j.is_array() && j.size() > 0) {
			return;
		}
	}



	ai.uuid = as::getUUID();

	tableCurrent.add(ai);
	tableHist.add(ai);

	//报警短信通知
	string msg = "报警类型:" + ai.typeLabel + "; ";
	msg += "报警对象:" + ai.tag + "; ";
	msg += "报警时间:" + ai.time + "; ";

	/*
	vector<USER_INFO> relateUsers = userMng.getRelateUsers(ai.tag);
	string pl, pnl;

	for (int i = 0; i < relateUsers.size(); i++)
	{
		USER_INFO& ui = relateUsers[i];
		if (ui.phone != "")
		{
			if (pl != "") pl += ",";
			pl += ui.phone;

			if (pnl != "") pnl += ";";
			pnl += ui.name + "," + ui.phone;
		}
	}

	if (pl != "" && tds->smsServer)
	{
		if (tds->smsServer->send(msg, pl))
		{
			//LOG("[报警短信通知]报警:" + msg + ",通知人:" + pnl);
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log(("[报警短信通知]报警:" + msg + ",通知人:" + pnl).c_str());
		}

	}*/
	if (m_initParam.func_sms_notify) {
		m_initParam.func_sms_notify(ai.tag, msg);
	}

	//通知给TDS客户端
	if (!m_bTestSrv) {
		json j = ai.toJson(this);
		//rpcSrv.notify("onAlarmAdd", j); 
		if (m_initParam.func_rpcHand_notify)
			m_initParam.func_rpcHand_notify("onAlarmAdd", j);
	}
}


void almServer::Update(AS_ALARM_INFO newStatus)
{
	//忽略屏蔽报警
	if (newStatus.typeLabel == "")
	{
		//内置类型查找
		newStatus.typeLabel = getAlarmTypeLabel(newStatus.type);
		//自定义类型查找
		if (newStatus.typeLabel == "") {
			if (m_mapCustomAlarmDesc.find(newStatus.type) != m_mapCustomAlarmDesc.end())
			{
				ALARM_TEMPLATE at = m_mapCustomAlarmDesc[newStatus.type];
				newStatus.typeLabel = at.label;
				if (at.enable == false)
					return;
			}
		}


		if (newStatus.typeLabel == "")
		{
			//LOG("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息");
			auto func_log = m_initParam.func_log;
			if (func_log)
				func_log(("[warn]未知的报警类型" + newStatus.type + ",请在项目报警模板文件alarm.json中配置该报警类型信息").c_str());
		}
	}

	std::lock_guard<mutex> g(m_csAlarmData);

	if (newStatus.time == "")
	{
		as::TIME st;
		as_timeopt::now(&st);
		newStatus.time = as_timeopt::stTimeToStr(st);
	}

	//the time attr of a status record is always the newest occuring event
	//time attr is not needed to specify a status record 
	json filter;
	filter["tag"] = newStatus.tag;
	filter["type"] = newStatus.type;
	filter["isRecover"] = false;
	AS_ALARM_INFO lastStatus;
	bool bTagAlarmStatusChanged = false; //该位号的报警状态是否发生改变
	if (tableCurrent.query(filter, lastStatus))
	{
		//check if status has changed
		//如果当前报警等级和之前发生改变。
		if (lastStatus.level != newStatus.level)
		{
			lastStatus.stRecoverTime = as_timeopt::str2st(newStatus.time);
			//先进行报警恢复。例如从报警到预警的变化。先恢复报警。
			recover(lastStatus);
			if (newStatus.level != "" && newStatus.level != "normal" && newStatus.level != "正常")
			{
				//再产生新的报警
				addAlarm(newStatus);
			}
			bTagAlarmStatusChanged = true;
		}
		else
		{
			//maintain last status
			//lastStatus.stRecoverTime = timeopt::str2st(newStatus.time);
			//lastStatus.bRecover = true;
			//recover(lastStatus);
		}
	}
	else
	{
		if (newStatus.level != "" && newStatus.level != "normal" && newStatus.level != "正常")
		{
			addAlarm(newStatus);
			bTagAlarmStatusChanged = true;
		}
	}


	//更新mo对象中的缓存
	//if (bTagAlarmStatusChanged)
	//{
	string sTag = newStatus.tag;
	string::size_type pos_s = newStatus.tag.find("(");
	if (pos_s != string::npos)
	{
		string::size_type pos_e = newStatus.tag.find(")");

		if (pos_e != string::npos)
		{
			sTag = newStatus.tag.substr(pos_s + 1, pos_e - (pos_s + 1));
		}
	}

	/*OBJ* pmo = prj.queryObj(sTag, "zh");
	if (pmo)
	{
		pmo->m_jAlarmStatus = getAlarmStatus(newStatus.tag);
	}*/
	auto func_obj_setJAlmStatus = m_initParam.func_obj_setJAlmStatus;
	if (func_obj_setJAlmStatus != NULL) {
		json  js = getAlarmStatus(newStatus.tag);
		func_obj_setJAlmStatus(sTag, "zh", js);
	}

	//}

	//json j = newStatus.toJson(this);
	//rpcSrv.notify("onUpdateAlarmStatus", j);
}

void almTable::freeBuff(map<string, AS_ALARM_INFO*>& mapAlarm)
{
	map<string, AS_ALARM_INFO*>::iterator i = mapAlarm.begin();
	for (; i != mapAlarm.end(); i++)
	{
		delete i->second;
	}
	mapAlarm.clear();
}

json almServer::getAlarmStatus(string tag)
{
	json querier;
	querier["tag"] = tag;
	querier["isRecover"] = false;
	vector<AS_ALARM_INFO*> statusList = tableCurrent.query(querier);
	json list = json::array();

	for (int i = 0; i < statusList.size(); i++)
	{
		AS_ALARM_INFO* p = statusList[i];
		json j = p->toJson(this);
		list.push_back(j);
	}
	return list;
}

void almServer::initMOAlarmStatus()
{

}

string almServer::getAlarmTypeLabel(string type)
{
	if (type == ALARM_TYPE::overHighLimit) {
		return "超高限";
	}
	else if (type == ALARM_TYPE::overLowLimit) {
		return "超低限";
	}
	return "";
}

void almServer::AddEvent(AS_ALARM_INFO ai)
{
	std::lock_guard<mutex>  g(m_csAlarmData);
	ai.uuid = as::getUUID();
	tableCurrent.add(ai);
	tableHist.add(ai);
}

#if 1
string almServer::rpc_addAlarm(json j, as::RPC_RESP& resp, bool bUpdate)
{
	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = as_TAG::addRoot(tag, rootTag);
	}

	AS_ALARM_INFO ai;
	ai.fromJson(j);

	if (j["time"].is_string()) {
		ai.time = j["time"];
	}
	else
		ai.time = as_timeopt::nowStr();

	m_eventAlarmRepetitiveCheck = false;
	if (j.contains("repeteCheck")) {
		bool b0 = j["repeteCheck"].get<bool>();
		if (b0) {
			m_eventAlarmRepetitiveCheck = true;
		}
	}

	addAlarm(ai);
	return "\"success\"";
}

void almServer::rpc_recoverAlarm(json j, as::RPC_RESP& resp)
{
	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = as_TAG::addRoot(tag, rootTag);
	}

	if (!j.contains("level"))
		j["level"] = "normal";
	rpc_updateStatus(j, resp);
	return;
}

void almServer::rpc_updateStatus(json j, as::RPC_RESP& resp)
{
	if (j["tag"] == nullptr && j["ioAddr"] == nullptr)
	{
		json jErr = "必须指定 tag 或者 ioAddr 字段";
		resp.error = jErr.dump();
		return;
	}
	if (j["type"] == nullptr)
	{
		json jErr = "必须指定 type 字段";
		resp.error = jErr.dump();
		return;
	}

	if (j.contains("rootTag")) {
		string rootTag = j["rootTag"];
		string tag = j["tag"];
		j["tag"] = as_TAG::addRoot(tag, rootTag);
	}

	try
	{
		AS_ALARM_INFO ai;
		ai.fromJson(j);
		//ai.time = timeopt::nowStr();
		Update(ai);
		resp.result = as::rPC_OK;
	}
	catch (std::exception& e)
	{
		json jErr = e.what();
		resp.error = jErr.dump();
	}

}


//基于 uuid,或 tag+ time+ type 匹配记录 
void almServer::rpc_acknowledge(json& params, as::RPC_RESP& resp, as::RPC_SESSION session) {
	if (params.contains("uuid") == false && (params.contains("tag") == false)) {
		string error = as::makeRPCError(as::RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定uuid或tag字段");
		resp.error = error;
		return;
	}

	if (params.contains("tag")) {
		string rootTag;
		if (params.contains("rootTag")) {
			rootTag = params["rootTag"];
		}
		string tag = params["tag"];
		tag = as_TAG::addRoot(tag, rootTag);

		//用户位号转系统位号
		tag = as_TAG::addRoot(tag, session.org);
		params["tag"] = tag;
	}

	AS_ALARM_INFO ai;
	if (tableCurrent.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		as_timeopt::now(&ai.stConfirmTime);
		//if (ai.bAck && ai.bRecover)//删除已消除已确认报警
		//{
		//	tableCurrent.remove(ai);
		//}
		//else
		//	tableCurrent.update(ai);

		tableCurrent.acknowledge(ai);
	}
	else
	{
		string error = as::makeRPCError(as::RPC_ERROR_CODE::ALM_alarmEventNotFound, "未找到报警事件");
		resp.error = error;
		return;
	}

	if (params.contains("time") == false)//用时间对应历史表文件  时间来自未确定文件.
		params["time"] = ai.time;
	if (tableHist.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		as_timeopt::now(&ai.stConfirmTime);
		//tableHist.update(ai);
		tableHist.acknowledge(ai);

	}

	json j = ai.toJson(this);
	//rpcSrv.notify("onAlarmAck", j);  
	if (m_initParam.func_rpcHand_notify)
		m_initParam.func_rpcHand_notify("onAlarmAck", j);

	resp.result = "\"ok\"";
}

void almServer::rpc_acknowledgeAll(json& params, as::RPC_RESP& resp, as::RPC_SESSION session)
{

}

//params ：对应ai那个结构
//返回负数  失败, 非负数 成功：0 未通过，1通过
//按原理，会马上恢复掉，仅恢复掉的允许审核 前端保证
int almServer::rpc_approve(json& params, as::RPC_RESP& resp, as::RPC_SESSION session) {
	int nRet = -1;
	if (params.contains("uuid") == false && (params.contains("tag") == false)) {
		string error = as::makeRPCError(as::RPC_ERROR_CODE::ALM_alarmEventNotFound, "未指定uuid或tag字段");
		resp.error = error;
		return nRet;
	}
	if (false == params["isRecover"].get<bool>()) {
		resp.error = "尚未恢复";
		nRet = -2;
		return nRet;
	}
	if (params.contains("tag")) {
		string rootTag;
		if (params.contains("rootTag")) {
			rootTag = params["rootTag"];
		}
		string tag = params["tag"];
		tag = as_TAG::addRoot(tag, rootTag);

		//用户位号转系统位号
		tag = as_TAG::addRoot(tag, session.org);
		params["tag"] = tag;
	}

	AS_ALARM_INFO ai;
	if (tableCurrent.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];
		if (info == "通过") {
			nRet = 1;
		}
		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		as_timeopt::now(&ai.stConfirmTime);
		if (ai.bAck && ai.bRecover)//删除已消除已确认报警
		{
			tableCurrent.remove(ai);
		}
		else
			tableCurrent.update(ai);
	}
	else
	{
		string error = as::makeRPCError(as::RPC_ERROR_CODE::ALM_alarmEventNotFound, "未找到报警事件");
		resp.error = error;
		return nRet;
	}

	if (params.contains("time") == false)//用时间对应历史表文件  时间来自未确定文件.
		params["time"] = ai.time;
	if (tableHist.query(params, ai))
	{
		string user = session.user;
		string info;
		if (params.contains("ackInfo"))
			info = params["ackInfo"];

		ai.bAck = 1;
		ai.strConfirmUser = session.user;
		ai.strConfirmInfo = info;
		as_timeopt::now(&ai.stConfirmTime);
		tableHist.update(ai);
	}

	json j = ai.toJson(this);
	//rpcSrv.notify("onAlarmAck", j);  
	if (m_initParam.func_rpcHand_notify)
		m_initParam.func_rpcHand_notify("onAlarmAck", j);

	resp.result = "\"ok\"";
	return nRet;
}

//构造 querier {K1:V1,...} 基础key： rootTag、user，记录key：记录任意字段 如tag、time、type等  字符串型的val可模糊匹配
json almServer::rpcReqParams2Querier(json& params, as::RPC_SESSION session)
{
	json querier;
	string rootTag = "";
	//把用户rootTag转成系统rootTag
	if (params["rootTag"] != nullptr)
	{
		rootTag = params["rootTag"].get<string>();
		rootTag = as_TAG::addRoot(rootTag, session.org);
	}
	//用户没有设置rootTag.将用户的org直接作为rootTag
	else
	{
		rootTag = as_TAG::addRoot(rootTag, session.org);
	}
	querier["rootTag"] = rootTag;
	querier["user"] = session.user;

	if (params.contains("tag")) querier["tag"] = params["tag"];//string or array
	if (params.contains("type")) querier["type"] = params["type"];//string or array
	if (params.contains("time")) querier["time"] = params["time"].get<string>();
	if (params.contains("level")) querier["level"] = params["level"];//string or array
	if (params.contains("isRecover")) querier["isRecover"] = params["isRecover"].get<bool>();
	if (params.contains("isAck")) querier["isAck"] = params["isAck"].get<bool>();
	if (params.contains("pageNo")) querier["pageNo"] = params["pageNo"].get<int>();
	if (params.contains("pageSize")) querier["pageSize"] = params["pageSize"].get<int>();
	if (params.contains("a-sort")) querier["a-sort"] = params["a-sort"];
	if (params.contains("d-sort")) querier["d-sort"] = params["d-sort"];
	//....
	return querier;
}

string almServer::rpc_getCurrent(json params, as::RPC_SESSION session)
{
	//全局报警禁用功能
	if (!m_initParam.enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getUnRecover(json params, as::RPC_SESSION session)
{
	//全局报警禁用功能
	if (!m_initParam.enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	querier["isRecover"] = false;
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getUnack(json params, as::RPC_SESSION session)
{
	//全局报警禁用功能
	if (!m_initParam.enableGlobalAlarm)
	{
		return "[]";
	}

	json querier = rpcReqParams2Querier(params, session);
	querier["isAck"] = false;
	return tableCurrent.toJsonStr(querier);
}

string almServer::rpc_getHistory(json params, as::RPC_SESSION session)
{
	DE_SELECTOR deSel;

	//root tag 转 系统位号
	string rootTag = "";
	if (params["rootTag"].is_string()) {
		rootTag = params["rootTag"].get<string>();
	}
	rootTag = as_TAG::addRoot(rootTag, session.org);
	params["rootTag"] = rootTag;

	if (!params.contains("tag")) {
		params["tag"] = "*";
	}

	bool getTypeTag = false;
	if (params.contains("getTypeTag") && params["getTypeTag"].is_boolean()) {
		getTypeTag = true;
	}

	vector<string> vecType;
	if (params.contains("type")) {
		if (params["type"].is_array()) {
			for (int i = 0; i < params["type"].size(); i++) {
				if (params["type"][i].is_string()) {
					vecType.push_back(params["type"][i].get<string>());
				}
			}
		}
		else if (params["type"].is_string()) {
			as_str::split(vecType, params["type"].get<string>(), ",");
		}
	}

	vector<string> vecLevel;
	if (params.contains("level")) {
		if (params["level"].is_array()) {
			for (int i = 0; i < params["level"].size(); i++) {
				if (params["level"][i].is_string()) {
					vecLevel.push_back(params["level"][i].get<string>());
				}
			}
		}
		else if (params["level"].is_string()) {
			as_str::split(vecLevel, params["level"].get<string>(), ",");
		}
	}

	bool filter_isRecover = false;
	bool isRecover = false;
	if (params.contains("isRecover")) {
		filter_isRecover = true;
		isRecover = params["isRecover"].get<bool>();
	}

	bool filter_isAck = false;
	bool isAck = false;
	if (params.contains("isAck")) {
		filter_isAck = true;
		isAck = params["isAck"].get<bool>();
	}

	string error;
	string sParams = params.dump();
	db.parseDESelector(sParams, deSel, error);
	if (error != "")
		return error;
	TIME_SELECTOR& timeSelector = deSel.timeSel;
	TAG_SELECTOR& tagSelector = deSel.tagSel;

	std::lock_guard<mutex> g(m_csAlarmData);
	int startYear = timeSelector.atomSelList[0].stStart.wYear;
	int startMonth = timeSelector.atomSelList[0].stStart.wMonth;
	int endYear = timeSelector.atomSelList[0].stEnd.wYear;
	int endMonth = timeSelector.atomSelList[0].stEnd.wMonth;
	int iMonth = 0;
	int iEndMonth = 0;
	map<SORT_FLAG, AS_ALARM_INFO*> deList_Sort;
	vector<almTable*> histTables;
	for (int iYear = startYear; iYear <= endYear; iYear++) {
		if (iYear == startYear) iMonth = startMonth;
		else iMonth = 1;
		if (iYear == endYear) iEndMonth = endMonth;
		else iEndMonth = 12;
		for (; iMonth <= iEndMonth; iMonth++) {
			almTable* tableTemp = new almTable();
			tableTemp->init(m_histPath);
			tableTemp->bOneFilePerMonth = true;
			tableTemp->SetAlarmSrv(this);
			histTables.push_back(tableTemp);
			tableTemp->loadFile(tableTemp->getFilePath(iYear, iMonth));
			for (map<string, AS_ALARM_INFO*>::iterator it = tableTemp->buff.begin(); it != tableTemp->buff.end(); it++) {
				if (session.user != "") {
					//if (!userMng.checkTagPermission(session.user, it->second->tag))
						//continue;
					if (m_initParam.func_usrMng_checkTagPermission) {
						if (m_initParam.func_usrMng_checkTagPermission(session.user, it->second->tag)) {
							continue;
						}
					}

				}
				if (!tagSelector.match(it->second->tag)) {
					continue;
				}
				if (!timeSelector.Match(it->second->time)) {
					continue;
				}

				bool bTypeMatch = false;
				if (vecType.size() == 0)
					bTypeMatch = true;
				else {
					for (auto& one : vecType) {
						if (as::generalMatch(one, it->second->type)) {
							bTypeMatch = true;
							break;
						}
					}
				}
				if (!bTypeMatch)
					continue;

				bool bLevelMatch = false;
				if (vecLevel.size() == 0)
					bLevelMatch = true;
				else {
					for (auto& one : vecLevel) {
						if (as::generalMatch(one, it->second->level)) {
							bLevelMatch = true;
							break;
						}
					}
				}
				if (!bLevelMatch)
					continue;

				if (filter_isRecover) {
					if (isRecover != it->second->bRecover) {
						continue;
					}
				}

				if (filter_isAck) {
					if (isAck != it->second->bAck) {
						continue;
					}
				}

				SORT_FLAG sf;
				sf.sFlag = it->second->getSortKey(deSel.sortKey);
				deList_Sort[sf] = it->second;
			}
		}
	}

	vector<AS_ALARM_INFO*> afterSortList;
	if (deSel.sortKey == "") deSel.ascendingSort = false;
	//因为有升降序之后还有分页需求,所以要再把map转为Vector;
	//也可以直接根据map直接生成最后的json,但是逻辑稍微复杂,所以转换成Vector
	//报警这块没有配置的话,按时间降序排列
	if (deSel.ascendingSort)
	{
		for (auto it = deList_Sort.begin(); it != deList_Sort.end(); ++it) {
			afterSortList.push_back(it->second);
		}
	}
	else
	{
		for (auto it = deList_Sort.rbegin(); it != deList_Sort.rend(); ++it) {
			afterSortList.push_back(it->second);
		}
	}

	string dataSet;
	//pageNo缺省时默认返回第一页
	if (deSel.pageSize > 0)
	{
		json resultObj;
		json jDataSet = json::array();
		resultObj["pageNo"] = deSel.pageNo;
		resultObj["pageSize"] = deSel.pageSize;
		resultObj["pageCount"] = afterSortList.size() / deSel.pageSize + (afterSortList.size() % deSel.pageSize == 0 ? 0 : 1);
		resultObj["deCount"] = afterSortList.size();
		if (afterSortList.size() > (deSel.pageNo - 1) * deSel.pageSize)
		{
			for (int i = 0; i < min(deSel.pageSize, afterSortList.size() - (deSel.pageNo - 1) * deSel.pageSize); i++)
			{
				auto it = afterSortList[i + (deSel.pageNo - 1) * deSel.pageSize];
				json j = it->toJson(this, rootTag);
				if (getTypeTag) {
					/*json jTypeTag = prj.getTypeTagByTag(it->tag);
					if (jTypeTag != nullptr) {
						j["typeTag"] = jTypeTag;
					}*/
					auto func_obj_getTypeTagByTag = m_initParam.func_obj_getTypeTagByTag;
					if (func_obj_getTypeTagByTag != NULL) {
						json jTypeTag = func_obj_getTypeTagByTag(it->tag);
						if (jTypeTag != nullptr) {
							j["typeTag"] = jTypeTag;
						}
					}

				}

				jDataSet.push_back(j);
			}
		}
		resultObj["pageData"] = jDataSet;
		dataSet = resultObj.dump(2);
	}
	else
	{
		json jDataSet = json::array();
		for (int i = 0; i < afterSortList.size(); i++)
		{
			auto it = afterSortList[i];
			json j = it->toJson(this, rootTag);
			if (getTypeTag) {
				/*json jTypeTag = prj.getTypeTagByTag(it->tag);
				if (jTypeTag != nullptr) {
					j["typeTag"] = jTypeTag;
				}*/
				auto func_obj_getTypeTagByTag = m_initParam.func_obj_getTypeTagByTag;
				if (func_obj_getTypeTagByTag != NULL) {
					json jTypeTag = func_obj_getTypeTagByTag(it->tag);
					if (jTypeTag != nullptr) {
						j["typeTag"] = jTypeTag;
					}
				}
			}

			jDataSet.push_back(j);
		}
		dataSet = jDataSet.dump(2);
	}

	for (auto& i : histTables) {
		delete i;
	}

	return dataSet;
}
#endif

/*
AS_ALARM_LEVEL almServer::StringToAlarmLevel(string level)
{
	if (level.find("预")!= string::npos)
	{
		return AL_PRE_ALARM;
	}
	else if (level.find("告")!=string::npos)
	{
		return AL_ALARM;
	}
	else if (level.find("报") != string::npos)
	{
		return AL_ALARM;
	}
	else if (level.find("一级") != string::npos)
	{
		return AL_ALARM_L1;
	}
	else if (level.find("二级") != string::npos)
	{
		return AL_ALARM_L2;
	}
	else if (level.find("三级") != string::npos)
	{
		return AL_ALARM_L3;
	}
	return AL_NORMAL;
}

string almServer::AlarmLevelToString(AS_ALARM_LEVEL level) {
	string strLevel;
	if (level == AL_PRE_ALARM)
	{
		strLevel = "预警";
	}
	else if (level == AL_ALARM)
	{
		strLevel = "告警";
	}
	else if (level == AL_ALARM_L3)
	{
		strLevel = "三级告警";
	}
	else if (level == AL_ALARM_L2)
	{
		strLevel = "二级告警";
	}
	else if (level == AL_ALARM_L1)
	{
		strLevel = "一级告警";
	}
	return strLevel;
}*/

bool almServer::CompareTime(as::TIME& time1, as::TIME& time2) {
	if (time1.wYear == time2.wYear && time1.wMonth == time2.wMonth && time1.wDay == time2.wDay && time1.wHour == time2.wHour && time1.wMinute == time2.wMinute && time1.wSecond == time2.wSecond)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void almTable::saveFile(string strFile, map<string, AS_ALARM_INFO*>& memData)
{
	string data = "uuid,位号,报警时间,报警类型,报警等级,报警信息,报警详情,恢复状态,恢复时间,确认状态,确认时间,确认信息,确认用户\r\n";
	if (as_charCodec::isValidGB2312(data)) {
		data = as_charCodec::gb_to_utf8(data);
	}
	map<string, AS_ALARM_INFO*>::iterator i;
	for (i = memData.begin(); i != memData.end(); i++)
	{
		AS_ALARM_INFO& ai = *i->second;
		string str = toCSV(ai);
		data += str;
	}
	as_fs::CreateDirectoryPlus_old(strFile);
	//data = as_charCodec::utf8_to_gb(data);//直接存储utf8
	as_fs::writeFile(strFile, data);
}

string almTable::getFilePath(int y, int m) {
	string p;
	if (bOneFilePerMonth)
	{
		string strYM = as_str::format("%04d%02d", y, m);
		p = db.m_path + filePath + "_" + strYM + ".csv";
	}
	else
	{
		p = db.m_path + filePath + ".csv";
	}
	return p;
}

string almTable::getFilePath(string time) {
	if (time == "")
		return db.m_path + filePath + ".csv";

	as::TIME st = as_timeopt::str2st(time);
	int y, m;
	y = st.wYear;
	m = st.wMonth;
	return getFilePath(y, m);
}

void almTable::loadFile(string strFile)
{
	//如果当前缓存对应的数据文件和要加载的相同，直接使用内存即可，返回
	if (buffFilePath == strFile)
		return;

	//加载新的路径到缓存
	freeBuff(buff);
	buffFilePath = strFile;

	string strDBData;
	as_fs::readFile(strFile, strDBData);
	//strDBData = as_charCodec::gb_to_utf8(strDBData);//默认使用utf8,出现乱码的GB2312只有健康管理系统,自己手动改数据库
	vector<string> recLines;
	as_str::split(recLines, strDBData, "\r\n");
	for (int i = 1; i < recLines.size(); i++)
	{
		string str = recLines.at(i);
		if (as_str::trim(str) == "")
			continue;
		AS_ALARM_INFO* pAi = new AS_ALARM_INFO();
		*pAi = fromCSV(str);
		buff[pAi->getKey()] = pAi;
	}
}


AS_ALARM_INFO AS_ALARM_INFO::fromJson(json j)
{
	AS_ALARM_INFO& ai = *this;

	//必填字段
	ai.tag = j["tag"];
	ai.type = j["type"];

	if (j["time"] != nullptr)
		ai.time = j["time"];

	if (j["level"] != nullptr)
		ai.level = j["level"];
	else
		ai.level = AS_ALARM_LEVEL::alarm;

	//可选字段
	if (j["desc"] != nullptr)
		ai.strAlarmDesc = j["desc"];
	if (j["isRecover"] != nullptr)
		ai.bRecover = j["isRecover"].get<bool>();
	if (j["recoverTime"] != nullptr)
		ai.stRecoverTime.fromStr(j["recoverTime"]);
	return ai;
}

json AS_ALARM_INFO::toJson(almServer* almSrv, string rootTag)
{
	AS_ALARM_INFO* info = this;
	json j;
	j["uuid"] = info->uuid;

	if (rootTag == "") {
		j["tag"] = info->tag;
	}
	else {
		string tag = info->tag;
		tag = as_str::trimPrefix(tag, rootTag + ".");
		j["tag"] = tag;
	}

	j["type"] = info->type;


	if (almSrv->m_mapCustomAlarmDesc.find(info->type) != almSrv->m_mapCustomAlarmDesc.end())
	{
		ALARM_TEMPLATE at = almSrv->m_mapCustomAlarmDesc[info->type];
		j["typeLabel"] = at.label;
	}
	else
	{
		j["typeLabel"] = j["type"];
	}

	j["level"] = info->level;
	string levelLabel = getAlarmLevelLabel(level);
	if (as_charCodec::isValidGB2312(levelLabel)) {
		levelLabel = as_charCodec::gb_to_utf8(levelLabel);
	}
	if (levelLabel != "")
	{
		j["levelLabel"] = levelLabel;
	}
	else
	{
		j["levelLabel"] = info->level;
	}

	j["desc"] = info->strAlarmDesc;
	j["detail"] = info->strAlarmDetail;
	j["time"] = info->time;
	j["suggest"] = info->strSuggest;
	j["isRecover"] = info->bRecover;
	j["recoverTime"] = as_timeopt::st2str(info->stRecoverTime);
	j["isAck"] = info->bAck;
	j["ackTime"] = as_timeopt::st2str(info->stConfirmTime);
	j["ackInfo"] = info->strConfirmInfo;
	j["ackUser"] = info->strConfirmUser;
	j["picUrl"] = info->pic_url;
	j["dbPath"] = almSrv->tableCurrent.filePath;
	return j;
}

AS_ALARM_INFO almTable::fromCSV(const string& line)
{
	vector<string> cols;
	string el;
	bool bInQuotation = false;
	for (int i = 0; i < line.length(); i++)
	{
		char* p = (char*)line.c_str() + i;
		if (!bInQuotation && *p == ',')
		{
			cols.push_back(el);
			el = "";
		}
		else if (*p == '\"')
		{
			bInQuotation = !bInQuotation;
		}
		else
		{
			el += *p;
		}
	}
	cols.push_back(el);

	int paddingSize = 14 - cols.size();
	for (int i = 0; i < paddingSize; i++) {
		cols.push_back("");
	}

	AS_ALARM_INFO ai;
	ai.uuid = cols[0];
	ai.tag = cols[1];
	ai.time = cols[2].c_str();
	ai.type = cols[3].c_str();
	ai.level = cols[4].c_str();
	ai.strAlarmDesc = cols[5].c_str();
	ai.strAlarmDetail = cols[6].c_str();
	ai.bRecover = atoi(cols[7].c_str());
	ai.stRecoverTime = as_timeopt::str2st(cols[8].c_str());
	ai.bAck = atoi(cols[9].c_str());
	ai.stConfirmTime = as_timeopt::str2st(cols[10].c_str());
	ai.strConfirmInfo = cols[11].c_str();
	ai.strConfirmUser = cols[12].c_str();
	ai.pic_url = cols[13].c_str();
	return ai;
}

string almTable::toCSV(AS_ALARM_INFO& info)
{
	string str;
	str += info.uuid; str += ",";
	/*0*/str += info.tag; str += ",";
	/*1*/str += info.time; str += ",";
	/*2*/str += info.type; str += ",";
	/*3*/str += info.level; str += ",";
	/*4*/str += "\"" + info.strAlarmDesc + "\""; str += ",";
	/*5*/str += "\"" + info.strAlarmDetail + "\""; str += ",";
	/*6*/str += info.bRecover ? "1" : "0"; str += ",";
	/*7*/str += as_timeopt::st2str(info.stRecoverTime); str += ",";
	/*8*/str += info.bAck ? "1" : "0"; str += ",";
	/*9*/str += as_timeopt::st2str(info.stConfirmTime); str += ",";
	/*10*/str += "\"" + info.strConfirmInfo + "\""; str += ",";
	/*11*/str += info.strConfirmUser; str += ",";
	/*12*/str += info.pic_url;
	str += "\r\n";
	return str;
}

string AS_ALARM_INFO::toJsonStr(almServer* almSrv, string rootTag)
{
	json j = toJson(almSrv, rootTag);
	return j.dump(2);
}

void almServer::ClearMap(map<string, AS_ALARM_INFO*>& inMap)
{
	for (map<string, AS_ALARM_INFO*>::iterator it = inMap.begin(); it != inMap.end(); it++) {
		if (it->second) delete it->second;
	}
	inMap.clear();
}

void almTable::init(string file)
{
	filePath = file;
}

void almTable::add(AS_ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa);
	AS_ALARM_INFO* pNew = new AS_ALARM_INFO();
	*pNew = ai;
	buff[ai.getKey()] = pNew;
	saveFile(pa, buff);
}

void almTable::acknowledge(const AS_ALARM_INFO& ai, bool remove)
{
	for (auto i = buff.begin(); i != buff.end(); )
	{
		AS_ALARM_INFO* it = i->second;
		if (it->tag != ai.tag)
			goto LOOP_END;
		if (it->type != ai.type)
			goto LOOP_END;
		if (it->bAck)
			goto LOOP_END;
		it->bAck = true;
		if (remove && it->bAck && it->bRecover)
		{
			i = buff.erase(i);
			continue;
		}
		it->strConfirmUser = ai.strConfirmUser;
		it->strConfirmInfo = ai.strConfirmInfo;
		it->stConfirmTime = ai.stConfirmTime;
	LOOP_END:
		i++;
	}
}

void almTable::acknowledge(const AS_ALARM_INFO& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	if (bOneFilePerMonth)
	{
		/*
		__cplusplus
		C++98: 199711L
		C++03: 199711L（与 C++98 相同，C++03 只是对 C++98 的一些修正，没有新特性）
		C++11: 201103L
		C++14: 201402L
		C++17: 201703L
		C++20: 202002L
		*/
#if __cplusplus <= 201402L
		WIN32_FIND_DATA findFileData;
		std::string searchPath = db.m_path + "/alarms/";
		HANDLE hFind = FindFirstFile((searchPath + "*").c_str(), &findFileData);//添加通配符以匹配所有文件

		if (hFind == INVALID_HANDLE_VALUE) {
			//Error finding files in directory;
			return;
		}
		do {
			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) //目录
				continue;
			std::string filename = findFileData.cFileName;
			if (filename == "." || filename == "..") {
				continue;
			}

			if (filename.find("history_") == 0 && filename.find(".csv") == 14 && filename.size() == 18
				&& to_string(stoi(filename.substr(8, 6))) == filename.substr(8, 6))
			{ // "history_YYYYMM.csv" 的长度为 15
				string fi = searchPath + filename;
				loadFile(fi);
				acknowledge(ai, false);
				saveFile(fi, buff);
			}
		} while (FindNextFile(hFind, &findFileData) != 0);

		FindClose(hFind); // 关闭句柄

#else
		// 获取当前路径
		std::filesystem::path currentPath = db.m_path + "/alarms/";
		// 遍历当前文件夹
		for (const auto& entry : std::filesystem::directory_iterator(currentPath)) {
			if (entry.is_regular_file()) { // 确保是文件
				std::string filename = entry.path().filename().string();
				// 检查文件名是否符合指定格式
				if (filename.find("history_") == 0 && filename.find(".csv") == 14 && filename.size() == 18
					&& to_string(stoi(filename.substr(8, 6))) == filename.substr(8, 6))
				{ // "history_YYYYMM.csv" 的长度为 15
					loadFile(entry.path().string());

					acknowledge(ai, false);

					saveFile(entry.path().string(), buff);
				}
			}
		}
#endif
	}
	else
	{
		string  pa = getFilePath("");
		loadFile(pa);

		acknowledge(ai, true);

		saveFile(pa, buff);
	}
}

//找基于uuid匹配的唯一一个 或 其他字段的组合匹配到的最后一个
bool almTable::query(json params, AS_ALARM_INFO& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	bool bFind = false;
	AS_ALARM_INFO* p = NULL;
	string time;
	if (params["time"] != nullptr)
		time = params["time"].get<string>();
	string  pa = getFilePath(time);
	loadFile(pa);
	const string strRecoverFlag = /*as_charCodec::gb_to_utf8(*/"恢复"/*)*/;
	string strType = "";
	if (params["type"] != nullptr)
	{
		strType = params["type"].get<string>();
		auto pos = strType.find(strRecoverFlag);
		if (pos != string::npos)
		{
			strType.replace(pos, strRecoverFlag.length(), "");
		}
	}

	for (auto& i : buff)
	{
		AS_ALARM_INFO& it = *i.second;
		if (params["uuid"] != nullptr) {
			if (it.uuid == params["uuid"].get<string>()) {
				ai = it;
				bFind = true;
				break;
			}
		}
		else {
			if (params["tag"] != nullptr && it.tag != params["tag"].get<string>())
				continue;
			if (params["time"] != nullptr && it.time != params["time"].get<string>())
				continue;
			if (params["type"] != nullptr)
			{
				if (it.type != strType)
					continue;
			}
			if (params["isAck"] != nullptr && it.bAck != params["isAck"].get<bool>())
				continue;
			if (params["isRecover"] != nullptr && it.bRecover != params["isRecover"].get<bool>())
				continue;

			ai = it;
			bFind = true;
		}
	}
	if (bFind)
	{
		return true;
	}
	return false;
}
void almTable::update(AS_ALARM_INFO ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa); //获取报警对应的数据文件
	AS_ALARM_INFO* p = buff.at(ai.getKey());
	if (p)
	{
		*p = ai;
		saveFile(pa, buff);
	}
}
void almTable::remove(ALARM_KEY& ai)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	string  pa = getFilePath(ai.time);
	loadFile(pa);
	buff.erase(ai.getKey());
	saveFile(pa, buff);
}

ALARM_QUERY almTable::parseQuerier(json& querier)
{
	ALARM_QUERY aq;
	if (querier.contains("user"))
	{
		aq.filter_user = true;
		aq.user = querier["user"].get<string>();
	}
	if (querier.contains("rootTag"))
	{
		aq.filter_rootTag = true;
		aq.rootTag = querier["rootTag"].get<string>();
	}

	if (querier.contains("tag")) {
		aq.filter_tag = true;
		if (querier["tag"].is_array()) {
			for (int i = 0; i < querier["tag"].size(); i++) {
				if (querier["tag"][i].is_string()) {
					aq.vecTag.push_back(querier["tag"][i].get<string>());
				}
				else
					assert(false);
			}
		}
		else if (querier["tag"].is_string()) {
			aq.vecTag.push_back(querier["tag"].get<string>());
		}
		else {
			assert(false);
		}
	}
	if (querier.contains("time"))
	{
		aq.filter_time = true;
		if (querier["time"].is_string())
			aq.time = querier["time"].get<string>();
		else
			assert(false);
	}
	if (querier.contains("type"))
	{
		aq.filter_type = true;
		if (querier["type"].is_array()) {
			for (int i = 0; i < querier["type"].size(); i++) {
				if (querier["type"][i].is_string()) {
					aq.vecType.push_back(querier["type"][i].get<string>());
				}
				else
					assert(false);
			}
		}
		else if (querier["type"].is_string()) {
			as_str::split(aq.vecType, querier["type"].get<string>(), ",");
		}
		else {
			assert(false);
		}
	}
	if (querier.contains("level"))
	{
		aq.filter_level = true;
		if (querier["level"].is_array()) {
			for (int i = 0; i < querier["level"].size(); i++) {
				if (querier["level"][i].is_string()) {
					aq.vecLevel.push_back(querier["level"][i].get<string>());
				}
				else
					assert(false);
			}
		}
		else if (querier["level"].is_string()) {
			aq.vecLevel.push_back(querier["level"].get<string>());
		}
		else {
			assert(false);
		}
	}
	if (querier.contains("isAck"))
	{
		aq.filter_isAck = true;
		if (querier["isAck"].is_boolean())
			aq.isAck = querier["isAck"].get<bool>();
		else
			assert(false);
	}
	if (querier.contains("isRecover"))
	{
		aq.filter_isRecover = true;
		if (querier["isRecover"].is_boolean())
			aq.isRecover = querier["isRecover"].get<bool>();
		else
			assert(false);
	}
	if (querier.contains("a-sort"))
	{
		aq.ascendingSort = true;
		if (querier["a-sort"].is_string())
			aq.sortKey = querier["a-sort"].get<string>();
		else aq.sortKey = "time";
	}
	else if (querier.contains("d-sort"))
	{
		aq.ascendingSort = false;
		if (querier["d-sort"].is_string())
			aq.sortKey = querier["d-sort"].get<string>();
		else aq.sortKey = "time";
	}
	else
	{
		aq.ascendingSort = false;
		aq.sortKey = "time";
	}
	return aq;
}

vector<AS_ALARM_INFO*> almTable::query(json querier)
{
	std::unique_lock<shared_mutex> lock(m_csTable);
	vector<AS_ALARM_INFO*> dataSet;
	loadFile(getFilePath());
	ALARM_QUERY aq = parseQuerier(querier);

	TIME_SELECTOR ts;
	if (aq.filter_time) {
		ts.init(aq.time);
	}
	map<SORT_FLAG, AS_ALARM_INFO*> deList_Sort;
	for (map<string, AS_ALARM_INFO*>::iterator it = buff.begin(); it != buff.end(); it++) {
		//if (aq.filter_user && !userMng.checkTagPermission(aq.user, it->second->tag))
			//continue;
		if (aq.filter_user) {
			if (m_pAlmSrv->m_initParam.func_usrMng_checkTagPermission) {
				if (m_pAlmSrv->m_initParam.func_usrMng_checkTagPermission(aq.user, it->second->tag)) {
					continue;
				}
			}
		}

		AS_ALARM_INFO* pAi = it->second;

		if (aq.filter_rootTag && pAi->tag.find(aq.rootTag) == string::npos)
			continue;

		//记录里存的绝对tag。 单独的tag是相对于roottag的。
		if (aq.filter_tag) {
			bool bMatch = false;
			for (const auto& oneTag : aq.vecTag) {
				string zong_tag = oneTag;
				if (aq.rootTag != "") {
					zong_tag = aq.rootTag + "." + oneTag;
				}
				if (as::matchTag(zong_tag, pAi->tag)) {
					bMatch = true;
					break;
				}
			}
			if (!bMatch)
				continue;
		}

		if (aq.filter_time) {
			if (false == ts.Match(pAi->time))
				continue;
		}
		if (aq.filter_type) {
			bool bMatch = false;
			for (const auto& one : aq.vecType) {
				if (as::generalMatch(one, pAi->type)) {
					bMatch = true;
					break;
				}
			}
			if (!bMatch)
				continue;
		}
		if (aq.filter_level) {
			bool bMatch = false;
			for (const auto& one : aq.vecLevel) {
				if (one == pAi->level) {
					bMatch = true;
					break;
				}
			}
			if (!bMatch)
				continue;
		}
		if (aq.filter_isAck) {
			if (aq.isAck != pAi->bAck)
				continue;
		}

		if (aq.filter_isRecover) {
			if (aq.isRecover != pAi->bRecover)
				continue;
		}

		SORT_FLAG sf;
		sf.sFlag = it->second->getSortKey(aq.sortKey);
		deList_Sort[sf] = it->second;
	}

	if (aq.ascendingSort)
	{
		for (auto it = deList_Sort.begin(); it != deList_Sort.end(); ++it) {
			dataSet.push_back(it->second);
		}
	}
	else
	{
		for (auto it = deList_Sort.rbegin(); it != deList_Sort.rend(); ++it) {
			dataSet.push_back(it->second);
		}
	}
	return dataSet;
}

string almTable::toJsonStr(const json& querier) {
	string rootTag = "";
	int pageNo = 1;
	int pageSize = 0;
	if (querier.contains("rootTag"))
		rootTag = querier["rootTag"].get<string>(); //org  or org + rootTag
	if (querier.contains("pageNo"))
	{
		if (querier["pageNo"].is_number_integer())
			pageNo = querier["pageNo"].get<int>();
	}
	if (querier.contains("pageSize"))
	{
		if (querier["pageSize"].is_number_integer())
			pageSize = querier["pageSize"].get<int>();
	}

	vector<AS_ALARM_INFO*> vec = query(querier);

	string dataSet = "";
	if (pageSize > 0)
	{
		//分页查询
		json resultObj;
		resultObj["pageNo"] = pageNo;
		resultObj["pageSize"] = pageSize;
		resultObj["pageCount"] = vec.size() / pageSize + (vec.size() % pageSize == 0 ? 0 : 1);
		resultObj["deCount"] = vec.size();
		string jDataSet = "[";
		if (vec.size() > (pageNo - 1) * pageSize)
		{
			//pageNo=1,说明从0开始,往后走pageSize个元素
			//pageNo=2,说明从pageSize开始,往后走pageSize个元素
			for (int i = (pageNo - 1) * pageSize; i < min(pageNo * pageSize, vec.size()); i++)
			{
				auto it = vec[i];
				if (jDataSet != "[")
					jDataSet += "," + it->toJsonStr(m_pAlmSrv, rootTag);
				else
					jDataSet += it->toJsonStr(m_pAlmSrv, rootTag);
			}
		}
		jDataSet += "]";
		json dataObj = json::parse(jDataSet);
		resultObj["pageData"] = dataObj;
		dataSet = resultObj.dump(2);
	}
	else
	{
		string jDataSet = "[";
		for (auto& it : vec) {
			if (jDataSet != "[")
				jDataSet += "," + it->toJsonStr(m_pAlmSrv, rootTag);
			else
				jDataSet += it->toJsonStr(m_pAlmSrv, rootTag);
		}
		jDataSet += "]";

		dataSet = jDataSet;
	}

	return dataSet;
}

void almTable::SetAlarmSrv(almServer* pSrv)
{
	m_pAlmSrv = pSrv;
}


