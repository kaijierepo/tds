#ifndef TDS_COMMON
#define TDS_COMMON

#include <time.h>
#include <io.h>
#include <tchar.h>
#include <queue>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <iostream>
#include <exception>
#include <WinSock2.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <mutex>
#include <condition_variable>
#pragma comment (lib, "Setupapi.lib")
#include <vector>
#include <map>
#include <regex>
#include <queue>
#define WIN32_LEAN_AND_MEAN
#ifdef WINDOWS
#include <windows.h>
#include <Commdlg.h>
#include <ShlObj_core.h>
#endif

using namespace std;

class semaphore
{
public:
	semaphore(int count_ = 0) : count(count_) {}
	inline void notify()
	{
		std::unique_lock<std::mutex> lock(mtx);
		count++;
		cv.notify_one();
	}
	inline void wait()
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

	inline bool wait_for(int milliSec)
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
	inline string& getCharCodec() {
		static string charCodec = "utf8";
		return charCodec;
	}

	inline void setCharCodec(string codec) {
		string& cc = getCharCodec();
		cc = codec;
	}


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


	inline unsigned short N_CRC16(unsigned char* updata, long long len)
	{
		unsigned char uchCRCHi = 0xff;
		unsigned char uchCRCLo = 0xff;
		long long  uindex;
		while (len--)
		{
			uindex = uchCRCHi ^ *updata++;
			uchCRCHi = uchCRCLo ^ auchCRCHi[uindex];
			uchCRCLo = auchCRCLo[uindex];
		}
		return (uchCRCHi << 8 | uchCRCLo);
	}

	inline void endianSwap(char* pData, int len)
	{
		char* pNew = new char[len];
		for (int i = 0;i<len;i++)
		{
			pNew[i] = pData[len - 1 - i];
		}
		memcpy(pData, pNew, len);
		delete pNew;
	}
}

namespace charCodec {

	inline string utf16toUtf8(wstring instr) //utf-8-->ansi
	{
		int MAX_STRSIZE = instr.length() * 4 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, instr.c_str(), -1, charstr, MAX_STRSIZE, NULL, NULL);
		string str = charstr;
		delete charstr;
		return str;
	}

	inline string utf16toAnsi(wstring instr)
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, instr.c_str(), -1, charstr, MAX_STRSIZE, NULL, NULL);
		string str = charstr;
		delete charstr;
		return str;
	}

	inline string utf8toAnsi(string instr) //utf-8-->ansi
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_ACP, 0, wcharstr, -1, charstr, MAX_STRSIZE, NULL, NULL);
		string charstrtemp(charstr);
		delete wcharstr;
		delete charstr;
		return charstrtemp;
	}

	inline wstring utf8toUtf16(string instr) //utf-8-->ansi
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_UTF8, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		wstring str = wcharstr;
		delete wcharstr;
		return str;
	}

	inline wstring ansiToUtf16(string instr)
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		wstring str = wcharstr;
		delete wcharstr;
		return str;
	}

	inline wstring autoToUtf16(string instr)
	{
		wstring w;
		if (common::getCharCodec() == "gb2312")
		{
			w = charCodec::ansiToUtf16(instr);
		}
		else
		{
			w = charCodec::utf8toUtf16(instr);
		}
		return w;
	}

	inline string utf16ToAuto(wstring instr)
	{
		string s;
		if (common::getCharCodec() == "gb2312")
		{
			s = charCodec::utf16toAnsi(instr);
		}
		else
		{
			s = charCodec::utf16toUtf8(instr);
		}
		return s;
	}


	inline string ansi2Utf8(string instr) //ansi-->utf-8
	{
		int MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, MAX_STRSIZE);
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, wcharstr, -1, charstr, MAX_STRSIZE, NULL, NULL);
		string charstrtemp(charstr);
		delete wcharstr;
		delete charstr;
		return charstrtemp;
	}

	inline string ToUtf8(LPCTSTR wstr) //-->utf-8
	{
#ifdef UNICODE
		int srcLen = lstrlen(wstr);
		string str = "";
		int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
		if (len > 0)
		{
			char* des = new char[len + 1];
			memset(des, 0, len + 1);
			WideCharToMultiByte(CP_UTF8, 0, wstr, -1, des, len, NULL, NULL);
			str = des;
		}
		return str;
#else
		string str = wstr;
		return str;
#endif
	}
}
namespace str {
	
	inline string& trimPrefix(string& s, string prefix = " ")
	{
		if (s.find(prefix) == 0)
		{
			s = s.substr(prefix.length(), s.length() - prefix.length());
		}
		return s;
	}

	inline string& trimSuffix(string& s, string suffix = " ")
	{
		int ipos = s.find(suffix);
		if (ipos + suffix.length() == s.length())
		{
			s = s.substr(0,ipos);
		}
		return s;
	}

	inline string& trim(std::string& s, string toTrim = " ")
	{
		s = trimPrefix(s, toTrim);
		s = trimSuffix(s, toTrim);
		return s;
	}


	inline string& replace(string& str, const string to_replaced, const string newchars)
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


	inline std::string format(const char* pszFmt, ...)
	{
		std::string str;
		va_list args;
		va_start(args, pszFmt);
		{
			int nLength = _vscprintf(pszFmt, args);
			nLength += 1;  //上面返回的长度是包含\0，这里加上
			std::vector<char> vectorChars(nLength);
			_vsnprintf(vectorChars.data(), nLength, pszFmt, args);
			str.assign(vectorChars.data());
		}
		va_end(args);
		return str;
	}

	inline int split(std::vector<std::string>& dst, const std::string& src, std::string separator)
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

	inline string removeChar(string str, char c)
	{
		str.erase(std::remove(str.begin(), str.end(), c), str.end());
		return str;
	}
	inline string trimFloat(string str)
	{
		while (str.at(str.length() - 1) == '0')
		{
			str = str.substr(0, str.length() - 1);
		}
		if (str.at(str.length() - 1) == '.')
		{
			str = str.substr(0, str.length() - 1);
		}
		return str;
	}
	inline string fromFloat(float f)
	{
		string s;
		s = str::format("%f", f);
		s = str::trimFloat(s);
		return s;
	}

	inline vector<char> toBytes(string str)
	{
		vector<char> bytes;
		str = removeChar(str, ' ');
		str = removeChar(str, '\t');
		str = removeChar(str, '\r');
		str = removeChar(str, '\n');

		if (0 != str.length() % 2)
		{
			str += "0";
		}

		int strLen = 0;
		strLen = str.length();

		for (int i = 0; i < strLen / 2; i++)
		{
			char cByteHigh = str.at(i * 2);
			char cByteLow = str.at(i * 2 + 1);
			cByteHigh = toupper(cByteHigh);
			cByteLow = toupper(cByteLow);
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
			bytes.push_back((char)b);
		}
		return bytes;
	}
	inline string fromBytes(vector<char>& bytes)
	{
		string str;
		for (int i = 0; i < bytes.size(); i++)
		{
			string b = format("%02X", (unsigned char)bytes[i]);
			str += b;
		}

		return str;
	}
	inline string fromBytes(char* p, int len, string splitter = " ")
	{
		string str;
		for (int i = 0; i < len; i++)
		{
			string b = format("%02X", (unsigned char)p[i]);
			str += b;
			str += splitter;
		}

		return str;
	}
	inline string fromInt(int v)
	{
		string s = str::format("%d", v);
		return s;
	}

	inline int toInt(string s)
	{
		return atoi(s.c_str());
	}

	inline bool isInteger(string s)
	{
		for (int i = 0; i < s.size(); i++)
		{
			if (s.at(i) < '0' || s.at(i) > '9')return false;
		}
		return true;
	}

	inline bool isIp(string s)
	{
		vector<string> v;
		str::split(v, s, ".");
		if (v.size() != 4)return false;
		for (int i = 0; i < v.size(); i++) 
		{
			if (!isInteger(v.at(i)))
			return false; 
		}
		for (int i = 0; i < v.size(); i++) 
		{ 
			int n = toInt(v.at(i));
			if (n > 255)return false;
		}
		return true;
	}

	inline bool parseIpPort(string s, string& ip, int& port)
	{
		int ipos = s.find(":");
		if (ipos == string::npos)
			return false;

		string sip = s.substr(0, ipos);
		string sport = s.substr(ipos + 1, s.length() - ipos - 1);

		if (!isIp(sip))
			return false;

		ip = sip;

		if (sport == "")
			return false;

		port = atoi(sport.c_str());

		return true;
	}
}
namespace timeopt {
	inline string stTimeToStr(SYSTEMTIME time)
	{
		string str = str::format("%4d-%02d-%02d %02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
		return str;
	}

	inline DWORD SysTime2Unix(SYSTEMTIME sDT)
	{
		tm temptm = { sDT.wSecond, sDT.wMinute, sDT.wHour,
			sDT.wDay, sDT.wMonth - 1, sDT.wYear - 1900, sDT.wDayOfWeek, 0, 0 };
		DWORD iReturn = (DWORD)mktime(&temptm);
		return iReturn;
	}

	inline SYSTEMTIME Unix2SysTime(DWORD iUnix)
	{
		SYSTEMTIME sDT;
		time_t tIn = (time_t)iUnix;
		tm temptm;
		localtime_s(&temptm, &tIn);
		sDT.wYear = 1900 + temptm.tm_year;
		sDT.wMonth = 1 + temptm.tm_mon;
		sDT.wDay = temptm.tm_mday;
		sDT.wDayOfWeek = temptm.tm_wday;
		sDT.wHour = temptm.tm_hour;
		sDT.wMinute = temptm.tm_min;
		sDT.wSecond = temptm.tm_sec;
		sDT.wMilliseconds = 0;
		return sDT;
	}

	inline SYSTEMTIME str2st(string str)
	{
		SYSTEMTIME t;
		int year, month, day, hour, min, sec;
		sscanf(str.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
			&year,
			&month,
			&day,
			&hour,
			&min,
			&sec);
		t.wYear = year;
		t.wMonth = month;
		t.wDay = day;
		t.wHour = hour;
		t.wMinute = min;
		t.wSecond = sec;
		t.wMilliseconds = 0;
		return t;
	}

	inline int HMS2Sec(string hms)
	{
		vector<string> v;
		str::split(v, hms, ":");
		int sec = atoi(v[0].c_str()) * 3600 + atoi(v[1].c_str()) * 60 + atoi(v[2].c_str());
		return sec;
	}

	inline bool isRelative(string time)
	{
		if (time.find("d") != string::npos || time.find("h") != string::npos
			|| time.find("m") != string::npos || time.find("s") != string::npos ||
			time.find("D") != string::npos || time.find("H") != string::npos
			|| time.find("M") != string::npos || time.find("S") != string::npos)
		{
			return true;
		}
		return false;
	}

	inline DWORD duration2sec(string strTime)
	{
		DWORD dwSecond = 0;

		std::smatch m;
		std::regex e("([0-9]*)([d,h,m,s])");
		std::string strSrc = strTime;
		transform(strSrc.begin(), strSrc.end(), strSrc.begin(), ::tolower);

		while (std::regex_search(strSrc, m, e))
		{
			if (m.size() > 2)
			{
				string strNum = m[1];
				int nNum = atoi(strNum.c_str());
				if (m[2] == "d")
				{
					dwSecond += nNum * 24 * 3600;
				}
				else if (m[2] == "h")
				{
					dwSecond += nNum * 3600;
				}
				else if (m[2] == "m")
				{
					dwSecond += nNum * 60;
				}
				else if (m[2] == "s")
				{
					dwSecond += nNum;
				}

			}
			strSrc = m.suffix().str();
		}

		return dwSecond;
	}

	inline string rel2abs(string time)
	{
		string strTime1 = time;
		if (isRelative(time)) {
			//相对时间区间模式
			string time1 = strTime1;
			string strDay = "", strH = "", strM = "", strS = "";
			int n1 = 0, n2 = 0, n3 = 0, n4 = 0;
			int pos = time1.find("d");
			if (pos == string::npos)
				pos = time1.find("D");
			if (pos != string::npos) {
				strDay = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n1 = atoi(strDay.c_str()) * 24 * 3600;
			}
			pos = time1.find("h");
			if (pos == string::npos)
				pos = time1.find("H");
			if (pos != string::npos) {
				strH = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n2 = atoi(strH.c_str()) * 3600;
			}
			pos = time1.find("m");
			if (pos == string::npos)
				pos = time1.find("M");
			if (pos != string::npos) {
				strM = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n3 = atoi(strM.c_str()) * 60;
			}
			pos = time1.find("s");
			if (pos == string::npos)
				pos = time1.find("S");
			if (pos != string::npos) {
				strS = time1.substr(0, pos);
				time1 = time1.erase(0, pos + 1);
				n4 = atoi(strS.c_str());
			}
			SYSTEMTIME stNow;
			GetLocalTime(&stNow);
			time_t endTime = timeopt::SysTime2Unix(stNow);
			time_t startTime = endTime - n1 - n2 - n3 - n4;
			SYSTEMTIME  stStart = timeopt::Unix2SysTime(startTime);
			string strNow = timeopt::stTimeToStr(stNow);
			string strStart = timeopt::stTimeToStr(stStart);
			time = strStart + "~" + strNow;
		}
		return time;
	}

	inline string st2str(SYSTEMTIME t)
	{
		string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond);
		return str;
	}

	inline string st2strWithMilli(SYSTEMTIME t)
	{
		string str = str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		return str;
	}

	inline string TimeToYMD(const SYSTEMTIME time)
	{
		string str;
		if (time.wYear > 2000 && time.wDay > 0 && time.wDay < 40 && time.wHour >= 0 && time.wHour <= 24 && time.wMinute >= 0 && time.wMinute <= 60)
		{
			str = str::format("%.4d-%.2d-%.2d", time.wYear, time.wMonth, time.wDay);
		}
		return str;
	}
	inline int CalcTimePassSecond(SYSTEMTIME lastTime)
	{
		time_t last = SysTime2Unix(lastTime);
		time_t now = time(NULL);
		time_t milli = now - last;
		return milli;
	}

	inline time_t getTick() {
		std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> tp =
			std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
		auto tmp = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
		time_t timestamp = tmp.count();
		return timestamp;
	}

	inline void setAsTimeOrg(SYSTEMTIME& st)
	{
		memset(&st, 0, sizeof(SYSTEMTIME));
		st.wYear = 1970;
		st.wMonth = 1;
		st.wDay = 1;
	}

	inline bool isValidTime(SYSTEMTIME& st)
	{
		if (st.wYear == 0 || st.wYear == 1970)
			return false;
		return true;
	}

	inline string nowStr(bool enableMS = false)
	{
		time_t timestamp = getTick();
		__int64 milli = timestamp + (__int64)8 * 60 * 60 * 1000;
		auto mTime = std::chrono::milliseconds(milli);
		auto tp = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(mTime);
		auto tt = std::chrono::system_clock::to_time_t(tp);
		std::tm now;
		::gmtime_s(&now, &tt);
		char res[64] = { 0 };
		if (enableMS)
			sprintf_s(res, _countof(res), "%4d-%02d-%02d %02d:%02d:%02d.%03d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, static_cast<int>(milli % 1000));
		else
		{
			sprintf_s(res, _countof(res), "%4d-%02d-%02d %02d:%02d:%02d", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
		}

		return std::string(res);
	}
}
namespace fs {
	//带后缀 .XXX 作为文件路径
	//不带后缀作为文件夹路径。不要输入无后缀的文件路径
	inline void createFolderOfPath(string strFile)
	{
		str::replace(strFile, "\\", "/");
		str::replace(strFile, "////", "/");
		str::replace(strFile, "///", "/");
		str::replace(strFile, "//", "/");

		int iDotPos = strFile.rfind('.');
		int iSlashPos = strFile.rfind('/');
		if (iDotPos > iSlashPos)//是一个文件
		{
			strFile = strFile.substr(0, iSlashPos);
		}

		int iStartPos = 0;
		while (1)
		{
			int iSlash = strFile.find('/', iStartPos);

			if (iSlash == string::npos)//路径为文件夹的情况
			{
				int iDot = strFile.find('.', iStartPos);
				if (iDot == string::npos)
					CreateDirectoryW(charCodec::autoToUtf16(strFile).c_str(), NULL);
				break;
			}

			if (iSlash + 1 == strFile.length())//最后字符为 \\ 的情况
				break;

			string strFolder = strFile.substr(0, iSlash);
			wstring wstrFolder = charCodec::autoToUtf16(strFolder).c_str();
			CreateDirectoryW(wstrFolder.c_str(), NULL);
			iStartPos = iSlash + 1;
		}
	}


	inline string appPath()
	{
#ifdef WINDOWS
		//windows获取到的是反斜杠，tds内统一使用斜杠
		TCHAR p[MAX_PATH] = { 0 };
		GetModuleFileName(NULL, p, MAX_PATH);//获取可执行模块的路径
		string strPath = (char*)p;
		int nEnd = strPath.rfind('\\');//取最后的"\"号之前地址
		strPath = strPath.substr(0, nEnd);
		if (common::getCharCodec() == "gb2312")
			strPath = strPath;
		else
			strPath = charCodec::ansi2Utf8(strPath);
		strPath = str::replace(strPath, "\\", "/");
		return strPath;
#elif LINUX
		return "";
#else
		return "";
#endif
	}
	inline string toAbsolutePath(string str)
	{
		string s;
		if (str.substr(0, 2) == "./")
		{
			s = str.substr(2, str.length() - 2);
			s = fs::appPath() + "/" + s;
		}
		else if (str.substr(0, 3) == "../")
		{
			s = fs::appPath() + "/" + str;
		}
		else
		{
			s = str;
		}
		return s;
	}
	inline string getExt(string strFilePath)
	{
		size_t pos = strFilePath.rfind(".");
		if (pos != strFilePath.npos)
		{
			string str = strFilePath.substr(pos, strFilePath.length() - pos);
			return str;
		}
		return "";
	}
	inline bool readFile(string path, char*& pData, int& len)
	{
		FILE* fp = _wfopen(charCodec::autoToUtf16(path).c_str(), L"rb");
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
	inline bool readFile(string path, string& data)
	{
		FILE* fp = _wfopen(charCodec::autoToUtf16(path).c_str(), L"rb");
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
			return true;
		}
		return false;
	}
	inline bool writeFile(string path, char* data, int len)
	{
		fs::createFolderOfPath(path);
		wstring wpath = charCodec::autoToUtf16(path);

		FILE* fp = _wfopen(wpath.c_str(), L"wb");
		if (fp)
		{
			fwrite(data, 1, len, fp);
			fclose(fp);
			return true;
		}
		else
		{
			int  iError = GetLastError();
			std::cout << "open file failed,error=" << iError << "," << path << std::endl;
		}
		return false;
	}

	inline bool appendFile(string path, char* data, int len)
	{
		wstring wpath = charCodec::autoToUtf16(path);
		FILE* fp = _wfopen(wpath.c_str(), L"ab");
		if (fp)
		{
			fwrite(data, 1, len, fp);
			fclose(fp);
			return true;
		}
		return false;
	}

	inline bool appendFile(string path, string data)
	{
		return appendFile(path, (char*)data.data(), data.length());
	}
	inline bool writeFile(string path, string& data)
	{
		return writeFile(path, (char*)data.c_str(), data.length());
	}
	inline bool fileExist(string pszFileName)
	{
		WIN32_FIND_DATAW FindFileData;
		HANDLE hFind;

		hFind = FindFirstFileW(charCodec::autoToUtf16(pszFileName).c_str(), &FindFileData);

		if (hFind == INVALID_HANDLE_VALUE)
			return false;
		else
		{
			FindClose(hFind);
			return true;
		}
		return false;
	}

	inline bool deleteFile(string path) {
		wstring wpath = charCodec::autoToUtf16(path);
		int iret = _wremove(wpath.c_str());
		return iret == 0;
	}

	inline  void getFileList(vector<string>& list,string strFolder,bool includeFolder = false,bool recursive = false)
	{
		wstring wstrFolder = charCodec::autoToUtf16(strFolder);
		wchar_t dirNew[200];
		wcscpy(dirNew, wstrFolder.c_str());
		wcscat(dirNew, L"\\*.*");    // 在目录后面加上"\\*.*"进行第一次搜索

		intptr_t handle;
		_wfinddata64i32_t findData;

		handle = _wfindfirst(dirNew, &findData);
		if (handle == -1)        // 检查是否成功
			return;

		do
		{
			if (findData.attrib & _A_SUBDIR)
			{
				if (wcscmp(findData.name, L".") == 0 || wcscmp(findData.name, L"..") == 0)
					continue;

				//list.push_back(charCodec::utf16toUtf8(findData.name));

				// 在目录后面加上"\\"和搜索到的目录名进行下一次搜索
				wcscpy(dirNew, wstrFolder.c_str());
				wcscat(dirNew, L"\\");
				wcscat(dirNew, findData.name);

				if (includeFolder)
					list.push_back(charCodec::utf16ToAuto(findData.name));

				if(recursive)
					getFileList(list,charCodec::utf16ToAuto(dirNew));
			}
			else
			{
				list.push_back(charCodec::utf16ToAuto(findData.name));
			}
		} while (_wfindnext(handle, &findData) == 0);

		_findclose(handle);    // 关闭搜索句柄
	}

	
	inline vector<string> fileDlg(bool isMultiSelect, bool IsOpen, bool IsPickFolder, char* filter = NULL, char* title = NULL, char* fileName = NULL,char* defExt = NULL,char* initDirectory = NULL)
	{
		vector<string> vecPath;
		//文件名
		wstring filename;
		wstring wFilter;
		vector<wstring> vecWFilter;
		if (filter)
		{
			string sFilter = filter;
			vector<string> vecFilter;
			str::split(vecFilter, filter, "|");
			for (int i = 0; i < vecFilter.size(); i++)
			{
				string s = vecFilter[i];
				vecWFilter.push_back(charCodec::autoToUtf16(s));
			}
		}
		std::wstring wDir;
		if (initDirectory)
			wDir = charCodec::autoToUtf16(initDirectory).c_str();//初始目录为默认
		std::wstring wTitle;
		if (title)
			wTitle = charCodec::autoToUtf16(title).c_str();
		std::wstring wDefExt;
		if (defExt)
			wDefExt = charCodec::autoToUtf16(defExt).c_str();
		std::wstring wFileName;
		if (fileName)
			wFileName = charCodec::autoToUtf16(fileName).c_str();


		CoInitialize(nullptr);
		if (!isMultiSelect)
		{
			IFileDialog* pfd = NULL;
			HRESULT hr = NULL;
			if (IsOpen)
				hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
			else
				hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
			if (SUCCEEDED(hr))
			{
				DWORD dwFlags;
				hr = pfd->GetOptions(&dwFlags);
				if (IsPickFolder)
					hr = pfd->SetOptions(dwFlags | FOS_PICKFOLDERS);
				else
					hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);

				COMDLG_FILTERSPEC* fileType = new COMDLG_FILTERSPEC[vecWFilter.size()/2];
				for (int i = 0; i < vecWFilter.size() / 2; i++)
				{
					COMDLG_FILTERSPEC& fs = fileType[i];
					fs.pszName = vecWFilter[i * 2].c_str();
					fs.pszSpec = vecWFilter[i * 2 + 1].c_str();
				}
				hr = pfd->SetFileTypes(vecWFilter.size()/2, fileType);
				hr = pfd->SetFileTypeIndex(1);
				

				if (!IsOpen)
				{
					hr = pfd->SetFileName(wFileName.c_str());
					hr = pfd->SetDefaultExtension(wDefExt.c_str());
				}
				

				hr = pfd->Show(GetForegroundWindow()); //Show dialog
					//if (SUCCEEDED(hr))
					//{
					//	if (!IsOpen)       //Capture user change when select differen file extension.
					//	{
					//		if (nType == 2)
					//		{
					//			UINT  unFileIndex(1);
					//			hr = pfd->GetFileTypeIndex(&unFileIndex);
					//			switch (unFileIndex)
					//			{
					//			case 0:
					//				hr = pfd->SetDefaultExtension(L"txt");
					//				break;
					//			case 1:
					//				hr = pfd->SetDefaultExtension(L"csv");
					//				break;
					//			case 2:
					//				hr = pfd->SetDefaultExtension(L"ini");
					//				break;
					//			default:
					//				hr = pfd->SetDefaultExtension(L"txt");
					//				break;
					//			}
					//		}
					//	}
					//}
					if (SUCCEEDED(hr))
					{
						IShellItem* pSelItem;
						hr = pfd->GetResult(&pSelItem);
						if (SUCCEEDED(hr))
						{
							LPWSTR pszFilePath = NULL;
							hr = pSelItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &pszFilePath);
							string sutf8 = charCodec::utf16toUtf8(pszFilePath);
							sutf8 = str::replace(sutf8, "\\", "/");
							vecPath.push_back(sutf8);
							CoTaskMemFree(pszFilePath);
						}
						pSelItem->Release();
					}
				}
				pfd->Release();
			}
		else  //Open dialog with multi select allowed;
		{
			IFileOpenDialog* pfd = NULL;
			HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
			if (SUCCEEDED(hr))
			{
				DWORD dwFlags;
				hr = pfd->GetOptions(&dwFlags);
				hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT);
					
				COMDLG_FILTERSPEC* fileType = new COMDLG_FILTERSPEC[vecWFilter.size()];
				for (int i = 0; i < vecWFilter.size(); i++)
				{
					COMDLG_FILTERSPEC& fs = fileType[i];
					fs.pszName = vecWFilter[i * 2].c_str();
					fs.pszSpec = vecWFilter[i * 2 + 1].c_str();
				}
				hr = pfd->SetFileTypes(vecWFilter.size(), fileType);
				hr = pfd->SetFileTypeIndex(1);

				hr = pfd->Show(NULL);
				if (SUCCEEDED(hr))
				{
					IShellItemArray* pSelResultArray;
					hr = pfd->GetResults(&pSelResultArray);
					if (SUCCEEDED(hr))
					{
						DWORD dwNumItems = 0; // number of items in multiple selection
						hr = pSelResultArray->GetCount(&dwNumItems);  // get number of selected items
						for (DWORD i = 0; i < dwNumItems; i++)
						{
							IShellItem* pSelOneItem = NULL;
							PWSTR pszFilePath = NULL; // hold file paths of selected items
							hr = pSelResultArray->GetItemAt(i, &pSelOneItem); // get a selected item from the IShellItemArray
							if (SUCCEEDED(hr))
							{
								hr = pSelOneItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
								vecPath.push_back(charCodec::utf16toUtf8(pszFilePath));
								if (SUCCEEDED(hr))
								{
									/*szSelected += pszFilePath;
									if (i < (dwNumItems - 1))
										szSelected += L"\n";*/
									CoTaskMemFree(pszFilePath);
								}
								pSelOneItem->Release();
							}
						}
						pSelResultArray->Release();
					}
				}
			}
			pfd->Release();
		}

		return vecPath;
	}
}
namespace path {
	inline string normalization(string& s)
	{
		s = str::replace(s, "\\\\", "/");
		s = str::replace(s, "\\", "/");
		s = str::replace(s, "//", "/");
		return s;
	}
}
namespace sys {
	struct COM_INFO {
		string portNum;
		string desc;
	};

	inline vector<string> getCOMList()
	{
		vector<string> list;
		HKEY hkey;
		int result;
		int i = 0;
		string strComName;//串口名称   
		string strDrName;//串口详细名称   
		result = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
			_T("Hardware\\DeviceMap\\SerialComm"),
			NULL,
			KEY_READ,
			&hkey);
		if (ERROR_SUCCESS == result)   //   打开串口注册表      
		{
			WCHAR portName[0x100], commName[0x100];
			DWORD dwLong, dwSize;
			do
			{
				dwSize = sizeof(portName) / sizeof(TCHAR);
				dwLong = dwSize;
				result = RegEnumValueW(hkey, i, portName, &dwLong, NULL, NULL, (LPBYTE)commName, &dwSize);
				if (ERROR_NO_MORE_ITEMS == result)
				{
					//   枚举串口   
					break;   //   commName就是串口名字"COM2"   
				}
				strComName = charCodec::utf16ToAuto(commName);
				strDrName = charCodec::utf16ToAuto(portName);
				// 从右往左边开始查找第一个'\\'，获取左边字符串的长度   
				int len = strDrName.rfind('\\');
				// 获取'\\'左边的字符串   
				string strFilePath = strDrName.substr(0,len + 1);
				// 获取'\\'右边的字符串   
				string fileName = strDrName.substr(len+1,strDrName.length() - len - 1);
				fileName = strComName + _T(": ") + fileName;
				list.push_back(fileName);
				i++;
			} while (1);
			RegCloseKey(hkey);
		}
		return list;
	}

	inline vector<COM_INFO> getCOMInfoList() {
		vector<COM_INFO> ary;
			HDEVINFO hDevInfo;
			SP_DEVINFO_DATA DeviceInfoData;
			DWORD i = 0;
			hDevInfo = SetupDiGetClassDevsW((LPGUID)&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
			/*
			GUID_DEVCLASS_FDC软盘控制器
			GUID_DEVCLASS_DISPLAY显示卡
			GUID_DEVCLASS_CDROM光驱
			GUID_DEVCLASS_KEYBOARD键盘
			GUID_DEVCLASS_COMPUTER计算机
			GUID_DEVCLASS_SYSTEM系统
			GUID_DEVCLASS_DISKDRIVE磁盘驱动器
			GUID_DEVCLASS_MEDIA声音、视频和游戏控制器
			GUID_DEVCLASS_MODEMMODEM
			GUID_DEVCLASS_MOUSE鼠标和其他指针设备
			GUID_DEVCLASS_NET网络设备器
			GUID_DEVCLASS_USB通用串行总线控制器
			GUID_DEVCLASS_FLOPPYDISK软盘驱动器
			GUID_DEVCLASS_UNKNOWN未知设备
			GUID_DEVCLASS_SCSIADAPTERSCSI 和 RAID 控制器
			GUID_DEVCLASS_HDCIDE ATA/ATAPI 控制器
			GUID_DEVCLASS_PORTS端口（COM 和 LPT）
			GUID_DEVCLASS_MONITOR监视器
			*/

			if (hDevInfo == INVALID_HANDLE_VALUE)
			{
				DWORD dwError = GetLastError();
				// Insert error handling here.   
				return ary;
			}

			// Enumerate through all devices in Set.        
			DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
			for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++)
			{
				DWORD DataT = 0;
				WCHAR buffer[256] = { 0 };
				DWORD buffersize = sizeof(buffer);

				while (!SetupDiGetDeviceRegistryPropertyW(hDevInfo,
					&DeviceInfoData,
					SPDRP_FRIENDLYNAME,
					&DataT,
					(PBYTE)buffer,
					buffersize,
					&buffersize))
				{
					if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
					{
						// Change the buffer size.   
						//if (buffer) LocalFree(buffer);   
					}
					else
					{
						// Insert error handling here. 
						break;
					}
				}

				wstring utf16str = buffer;
				string comInfo = charCodec::utf16ToAuto(utf16str);

				int iLeftBracket = comInfo.find("(");
				if (iLeftBracket == string::npos)
					continue;

				int iRightBracket = comInfo.find(")");

				string portNum = comInfo.substr(iLeftBracket + 1, iRightBracket - iLeftBracket - 1);

				//vspd 创建的虚拟串口是  COM1->COM2的格式
				int iFPos = portNum.find("->");
				if (iFPos != string::npos)
				{
					portNum = portNum.substr(iFPos + 2, portNum.size() - iFPos - 2);
				}

				COM_INFO ci;
				ci.portNum = portNum;
				ci.desc = comInfo.substr(0, iLeftBracket);

				ary.insert(ary.begin(),ci);

			
				//if (buffer)                                                                            
				//{
				//	LocalFree(buffer);
				//}
			}
			if (GetLastError() != NO_ERROR && GetLastError() != ERROR_NO_MORE_ITEMS)
			{
				return ary;
			}

			// Cleanup   
			SetupDiDestroyDeviceInfoList(hDevInfo);
			return ary;
	}
	inline string getLastError(string szReason = "")
	{
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

		string szErrMsg = "";

		if (dwLen == 0)
		{
			DWORD dwFmtErrCode = GetLastError(); //FormatMessage 引起的错误代码
			szErrMsg = str::format("FormatMessage failed with %u\n", dwFmtErrCode);
		}

		if (lpMsgBuf)
		{
			wstring utf16msg = (LPWSTR)lpMsgBuf;
			string utf8Msg = charCodec::utf16ToAuto(utf16msg);
			szErrMsg = str::format("%s\n Code = %u, Mean = %s", szReason.c_str(), dwErrCode, utf8Msg.c_str());
		}

		if (lpMsgBuf)
		{
			// Free the buffer.
			LocalFree(lpMsgBuf);
			lpMsgBuf = NULL;
		}

		return szErrMsg;
	}
}

namespace common {
	inline string guid() {
		GUID   m_guid;
		string   strGUID;
		if (S_OK == ::CoCreateGuid(&m_guid))
		{
			strGUID = str::format("%08X-%04X-%04x-%02X%02X-%02X%02X%02X%02X%02X%02X",
				m_guid.Data1, m_guid.Data2, m_guid.Data3,
				m_guid.Data4[0], m_guid.Data4[1],
				m_guid.Data4[2], m_guid.Data4[3],
				m_guid.Data4[4], m_guid.Data4[5],
				m_guid.Data4[6], m_guid.Data4[7]);
		}
		return strGUID;
	}
}

#endif
