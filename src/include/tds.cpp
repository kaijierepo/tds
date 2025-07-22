/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu  

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "tds.h"
#include <stdarg.h>
#include <mutex>
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

int _vscprintf_cross(const char* format, va_list pargs) {
	int retval;
	va_list argcopy;
	va_copy(argcopy, pargs);
	retval = vsnprintf(NULL, 0, format, argcopy);
	va_end(argcopy);
	return retval;
}

namespace str {
	std::string format(const char* pszFmt, ...)
	{
		std::string str;
		va_list args;
		va_start(args, pszFmt);
		{
			int nLength = _vscprintf_cross(pszFmt, args);
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

	wstring gb_to_utf16(string instr)
	{
		wstring str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 2 + 2;
		WCHAR* wcharstr = new WCHAR[MAX_STRSIZE];
		memset(wcharstr, 0, MAX_STRSIZE);
		MultiByteToWideChar(CP_ACP, 0, (char*)instr.data(), -1, wcharstr, (int)MAX_STRSIZE);
		str = wcharstr;
		delete[] wcharstr;
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
	string utf16_to_utf8(wstring instr) //utf-8-->ansi
	{
		string str;
#ifdef _WIN32
		size_t MAX_STRSIZE = instr.length() * 4 + 2;
		char* charstr = new char[MAX_STRSIZE];
		memset(charstr, 0, MAX_STRSIZE);
		WideCharToMultiByte(CP_UTF8, 0, instr.c_str(), -1, charstr, (int)MAX_STRSIZE, NULL, NULL);
		str = charstr;
		delete[] charstr;
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
		delete[] charstr;
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
		delete[] wcharstr;
		delete[] charstr;
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
		delete[] wcharstr;
		delete[] charstr;
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

HMS TIME::getHMS() {
	HMS hms;
	hms.wHour = wHour;
	hms.wMinute = wMinute;
	hms.wSecond = wSecond;
	hms.wMilliseconds = wMilliseconds;
	return hms;
}

void TIME::setNow() {
	auto now = std::chrono::system_clock::now();
	unsigned short milli = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
		- std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
	time_t tt = std::chrono::system_clock::to_time_t(now);
	fromUnixTime(tt, milli);
}

string TIME::toStr(bool enableMilli)
{
	if (enableMilli) {
		return str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",wYear, wMonth, wDay,wHour, wMinute, wSecond,wMilliseconds);
	}
	else {
		return str::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",wYear, wMonth, wDay,wHour, wMinute, wSecond);
	}
}

void TIME::fromStr(string str) {
	TIME& t = *this;
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
		else if (str.length() == 7) //2022-02-02
		{
			sscanf(str.c_str(), "%4d-%2d",
				&y,
				&m);
			t.wYear = y; t.wMonth = m;
		}
	}
	else if (str::isDigits(str)) {
		time_t tt = atoi(str.c_str());
		fromUnixTime(tt);
	}
}

string TIME::toDateStr()
{
	string s = str::format("%04d-%02d-%02d", wYear, wMonth, wDay);
	return s;
}

string TIME::toTimeStr()
{
	string s = str::format("%02d:%02d:%02d", wHour, wMinute, wSecond);
	return s;
}

string TIME::toStampFull()
{
	string s = str::format("%04d-%02d-%02d %02d%02d%02d", wYear, wMonth, wDay, wHour, wMinute, wSecond);
	return s;
}

time_t  TIME::toUnixTime() {
	tm temptm = { wSecond, wMinute, wHour,wDay,wMonth - 1,wYear - 1900,wDayOfWeek, 0, 0 };
	time_t unixTime = mktime(&temptm);
	return unixTime;
}

void  TIME::fromUnixTime(time_t unixTime,int milli) {
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
	wMilliseconds = milli;
	wDayOfWeek = time_tm.tm_wday;
}

string TIME::toStampHMS()
{
	string s = str::format("%02d%02d%02d", wHour, wMinute, wSecond);
	return s;
}

string Date::toStr()
{
	string s = str::format("%04d-%02d-%02d", wYear, wMonth, wDay);
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

void HMS::setNow() {
	TIME t;
	t.setNow();
	wHour = t.wHour;
	wMinute = t.wMinute;
	wSecond = t.wSecond;
	wMilliseconds = t.wMilliseconds;
}

string HMS::toStr()
{
	string s = str::format("%02d:%02d:%02d", wHour, wMinute, wSecond);
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

string TAG::trimPrefix(string s, string prefix)
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

string TAG::trimRoot(string tag, string root)
{
	if (root == "")
		return tag;

	tag = trimPrefix(tag, root);
	tag = trimPrefix(tag, ".");
	return tag;
}

string TAG::getParentTag(string tag)
{
	size_t pos = tag.rfind(".");
	if (pos == string::npos) {
		return ""; //parent tag is rootTag
	}
	else if (pos >= 0) {
		tag = tag.substr(0, pos);
	}
	else {

	}
	return tag;
}

string TAG::userTag2sysTag(string userTag, string userOrg)
{
	return TAG::addRoot(userTag, userOrg);
}

string TAG::sysTag2userTag(string sysTag, string userOrg)
{
	return TAG::trimRoot(sysTag, userOrg);
}


string TAG::addRoot(string tag, string root)
{
	if (root == "")
		return tag;

	//tag是相对于root的相对位号
	if (tag == "")
		return root;

	return root + "." + tag;
}


int TAG::split(std::vector<std::string>& dst, const std::string& src, std::string separator)
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


size_t TAG::getMoLevel(string tag)
{
	tag = TAG::addRoot(tag, "root");
	return std::count(tag.begin(), tag.end(), '.');
}


//tagThis当前位号
//strTagExp相对与当前位号的相对位号表达式
string TAG::resolveTag(string strTagExp, string tagContext)
{
	string tagName = strTagExp;


	if (strTagExp.find("..") == 0) { //上一级位号
		string tagContextParent;
		size_t pos = tagContext.rfind(".");
		if (pos >= 0) {
			tagContextParent = tagContext.substr(0, pos);
		}


		tagName = trimPrefix(strTagExp, "..");

		if (tagContextParent != "") {
			tagName = tagContextParent + "." + tagName;
		}
	}
	else if (strTagExp.find(".") == 0) //绝对位号
	{
		tagName = trimPrefix(strTagExp, ".");
	}
	else if (strTagExp.find("::") == 0) //绝对位号
	{
		tagName = trimPrefix(strTagExp, "::");
	}
	//使用^访问上级或者祖父级，如: ^.A ^^.A
	else if (strTagExp.find("^") == 0) {
		int parentCount = 0;
		for (int i = 0; i < tagContext.size(); i++) {
			char c = tagContext[i];
			if (c ==  '^' ) {
				parentCount++;
			}
			else {
				break;
			}
		}
		vector<string> parentNodes;
		str::split(parentNodes, tagContext, ".");
		
		tagName = str::trimPrefix(tagName, "^");
		tagName = str::trimPrefix(tagName, ".");
		for (int i = 0; i < parentCount && i< parentNodes.size(); i++) {
			string node = parentNodes[parentNodes.size() - 1 - i];
			tagName = TAG::addRoot(tagName, node);
		}
	}
	else {//相对于环境位号的位号
		tagName = TAG::addRoot(strTagExp, tagContext);
	}

	return tagName;
}


string TDS_LAST_ERROR()
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
		szErrMsg = str::format("FormatMessage failed with %u\n", dwFmtErrCode);
	}

	if (lpMsgBuf)
	{
		wstring utf16msg = (LPWSTR)lpMsgBuf;
#ifdef UTF8
		string msg = str::utf16_to_utf8(utf16msg);
#else
		string msg = str::utf16_to_gb(utf16msg);
#endif
		szErrMsg = str::format("Code = %u, Mean = %s", dwErrCode, msg.c_str());
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