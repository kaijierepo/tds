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
	}
	else if (str::isDigits(str)) {
		time_t tt = atoi(str.c_str());
		fromUnixTimeStamp(tt);
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
	if (pos >= 0) {
		tag = tag.substr(0, pos);
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

bool TAG::hasTag(json& tree, string tag)
{
	vector<string> nodeNames;
	split(nodeNames, tag, ".");
	json* node = &tree;
	for (int i = 0; i < nodeNames.size(); i++)
	{
		string name = nodeNames[i];


		//查找子节点中有没有是 指定name的节点。如果有node指向该节点，继续查找node的子节点中是否有下一个name
		if ((*node)["children"] == nullptr)
			return false;
		json& jChildren = (*node)["children"];
		bool bHaveChild = false;
		if (jChildren.is_array())//具体指定
		{
			for (int j = 0; j < jChildren.size(); j++)
			{
				json& child = jChildren[j];
				if (child["name"].get<string>() == name)
				{
					bHaveChild = true;
					node = &child;
				}
			}
		}
		else if (jChildren.is_string() && jChildren.get<string>() == "*") //通配符指定，所有子节点
		{
			return true;
		}

		if (!bHaveChild)return false;
	}

	return true;
}

size_t TAG::getMoLevel(string tag)
{
	tag = TAG::addRoot(tag, "root");
	return std::count(tag.begin(), tag.end(), '.');
}

json TAG::mapTree2List(json mapTree)
{
	for (auto& [k, v] : mapTree.items())
	{

	}

	return json();
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
	else {//相对于环境位号的位号
		tagName = TAG::addRoot(strTagExp, tagContext);
	}

	return tagName;
}

