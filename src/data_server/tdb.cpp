/*
  TDB version 1.0.0
  a minimal time series database based on json files for iot
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
#include "tdb.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "yyjson.h"
#include <stdarg.h>
#include <mutex>
#include <regex>
#include "DTW.hpp"
#include <thread>
#include "dtwrecoge.h"
#include <chrono>
#include <atomic>
#include <string>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

#if (defined(_MSVC_LANG) && _MSVC_LANG < 201703L) || (!defined(_MSVC_LANG) && defined(__cplusplus) && __cplusplus < 201703L)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

TDB db;

string replaceStr(string str, const string to_replaced, const string newchars)
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

namespace DB_STR {
	int _vscprintf_cross_db(const char* format, va_list pargs) {
		int retval;
		va_list argcopy;
		va_copy(argcopy, pargs);
		retval = vsnprintf(NULL, 0, format, argcopy);
		va_end(argcopy);
		return retval;
	}

	std::string format(const char* pszFmt, ...)
	{
		std::string str;
		va_list args;
		va_start(args, pszFmt);
		{
			int nLength = _vscprintf_cross_db(pszFmt, args);
			nLength += 1;  //上面返回的长度是包含\0，这里加上
			std::vector<char> vectorChars(nLength);
			vsnprintf(vectorChars.data(), nLength, pszFmt, args);
			str.assign(vectorChars.data());
		}
		va_end(args);
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
		//size_t outlen = 2 * inlen;

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

	bool isTime(string& s) {
		if (s.size() > 10) {
			char a = s[4];
			char b = s[7];
			if (a == '-' && b == '-') {
				return true;
			}
		}
		return false;
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
		delete [] wcharstr;
		delete [] charstr;
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
}


namespace TIME_OPT {

	DB_TIME Unix2DBTime(time_t iUnix, int milli)
	{
		static std::mutex mtx;
		mtx.lock();
		tm time_tm = *localtime(&iUnix);  //线程安全linux下推荐用localtime_r，win下推荐用localtime_s，此处为方便直接加个锁
		mtx.unlock();

		DB_TIME t;
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

	time_t calcTimePassSecond(DB_TIME& lastTime)
	{
		time_t last = lastTime.toUnixTime();
		time_t now = ::time(NULL);
		time_t milli = now - last;
		return milli;
	}

	DB_TIME now() {
		auto now = std::chrono::system_clock::now();
		//通过不同精度获取相差的毫秒数 <1000毫秒值
		unsigned short milli = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
			- std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
		time_t tt = std::chrono::system_clock::to_time_t(now);

		return Unix2DBTime(tt, milli);
	}

	bool isRelative(string time)
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

	int timeLen2seconds(string timeLen) {
		//相对时间区间模式
		string time1 = timeLen;
		string strDay = "", strH = "", strM = "", strS = "";
		int n1 = 0, n2 = 0, n3 = 0, n4 = 0;
		size_t pos = time1.find("y");
		if (pos == string::npos)
			pos = time1.find("Y");
		if (pos != string::npos) {
			string strYear = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n1 = (int)atof(strYear.c_str()) * 365 * 24 * 3600;
		}
		pos = time1.find("d");
		if (pos == string::npos)
			pos = time1.find("D");
		if (pos != string::npos) {
			strDay = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n1 = (int)atof(strDay.c_str()) * 24 * 3600;
		}
		pos = time1.find("h");
		if (pos == string::npos)
			pos = time1.find("H");
		if (pos != string::npos) {
			strH = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n2 = (int)atof(strH.c_str()) * 3600;
		}
		pos = time1.find("m");
		if (pos == string::npos)
			pos = time1.find("M");
		if (pos != string::npos) {
			strM = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n3 = (int)atof(strM.c_str()) * 60;
		}
		pos = time1.find("s");
		if (pos == string::npos)
			pos = time1.find("S");
		if (pos != string::npos) {
			strS = time1.substr(0, pos);
			time1 = time1.erase(0, pos + 1);
			n4 = (int)atof(strS.c_str());
		}
		return n1 + n2 + n3 + n4;
	}

	string rel2abs(string time)
	{
		string strTime1 = time;
		if (isRelative(time)) {
			int timeLen = timeLen2seconds(time);
			DB_TIME stNow;
			stNow.setNow();
			time_t endTime = stNow.toUnixTime();
			time_t startTime = endTime - timeLen;
			DB_TIME  stStart;
			stStart.fromUnixTime(startTime);
			string strNow = stNow.toStr(false);
			string strStart = stStart.toStr(false);
			time = strStart + "~" + strNow;
		}
		return time;
	}

	DB_TIME addTime(DB_TIME base, int h, int m, int s) {
		time_t tBase = base.toUnixTime();
		tBase += h * 3600 + m * 60 + s;
		DB_TIME st;
		st.fromUnixTime(tBase);
		return st;
	}

	string st2str(DB_TIME t, bool enableMS)
	{
		if (enableMS) {
			string str = DB_STR::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
			return str;
		}
		else {
			string str = DB_STR::format("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
				t.wYear, t.wMonth, t.wDay,
				t.wHour, t.wMinute, t.wSecond);
			return str;
		}
	}

	string nowStr(bool enableMS)
	{
		DB_TIME t = now();
		return st2str(t, enableMS);
	}
}

namespace DB_TAG {
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

	string trimRoot(string tag, string root)
	{
		if (root == "")
			return tag;

		tag = trimPrefix(tag, root);
		tag = trimPrefix(tag, ".");
		return tag;
	}

	string addRoot(string tag, string root)
	{
		if (root == "")
			return tag;

		//tag is a relative tag to root
		if (tag == "")
			return root;

		return root + "." + tag;
	}
}


namespace DB_FS {
	bool readFile(string path, string& data) {
		FILE* fp = nullptr;
		wstring wPath = DB_STR::utf8_to_utf16(path);

#ifdef _WIN32
		_wfopen_s(&fp, wPath.c_str(), L"rb");
#else
		fp = fopen(path.c_str(), "rb");
#endif

		if (fp) {
			fseek(fp, 0, SEEK_END);

			long len = ftell(fp);
			if (len > 0) {
				data.resize(len);
				char* pdata = (char*)data.data();

				fseek(fp, 0, SEEK_SET);
				fread(pdata, 1, len, fp);
			}

			fclose(fp);
			return true;
		}
		return false;
	}

	bool readFile(string path, char*& pData, int& len)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp, DB_STR::utf8_to_utf16(path).c_str(), L"rb");
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

	//do not use a file path without a suffix
	//filesystem::path use wstring utf16 ,compatible with windows and linux
	void createFolderOfPath(string strFile)
	{
		strFile = replaceStr(strFile, "\\", "/");
		strFile = replaceStr(strFile, "////", "/");
		strFile = replaceStr(strFile, "///", "/");
		strFile = replaceStr(strFile, "//", "/");

		size_t iDotPos = strFile.rfind('.');
		size_t iSlashPos = strFile.rfind('/');
		if (iDotPos != string::npos && iDotPos > iSlashPos)//is a file
		{
			strFile = strFile.substr(0, iSlashPos);
		}
#ifdef _WIN32
		//filesystem::create_directories(utf8_to_utf16(strFile));
		int iStartPos = 0;
		while (1)
		{
			int iSlash = strFile.find('/', iStartPos);
			if (iSlash == string::npos){break;}

			string strFolder = strFile.substr(0,iSlash);
			CreateDirectoryW(DB_STR::utf8_to_utf16(strFolder).c_str(), NULL);

			if (iSlash + 1 == strFile.length())//last char is /
				break;
			iStartPos = iSlash + 1;
		}
		CreateDirectoryW(DB_STR::utf8_to_utf16(strFile).c_str(), NULL);
#else
		std::filesystem::create_directories(strFile);
#endif
	}
	bool writeFile(string path, char* data, size_t len)
	{
		createFolderOfPath(path);

		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp, DB_STR::utf8_to_utf16(path).c_str(), L"wb");
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

		}
		return false;
	}
	bool writeFile(string path, unsigned char* data, size_t len)
	{
		return writeFile(path, (char*)data, len);
	}
	bool writeFile(string path, string& data)
	{
		return writeFile(path, (char*)data.c_str(), data.length());
	}
	bool appendWrite(string path, char* data, size_t len)
	{
		FILE* fp = nullptr;
#ifdef _WIN32
		_wfopen_s(&fp, DB_STR::utf8_to_utf16(path).c_str(), L"a");
#else
		fp = fopen(path.c_str(), "wb");
#endif
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			fwrite(data, 1, len, fp);
			fclose(fp);
			return true;
	}
		else
		{

		}
		return false;
	}
	bool deleteFile(string path) {
#ifdef _WIN32
		std::wstring filePath = DB_STR::utf8_to_utf16(path);

		if (DeleteFileW(filePath.c_str())) {
			return true;
		}
		else {
			return false;
		}
#else
		return std::filesystem::remove(DB_STR::utf8_to_utf16(path));
#endif

	}

	//delete children(include subdirs and files, not include dirPath itself
	void DeleteDirectoryContents(const std::string& dirPath) {
#ifdef _WIN32
		WIN32_FIND_DATA findFileData;
		HANDLE hFind;

		std::string searchPath = dirPath + "\\*";
		hFind = FindFirstFile(searchPath.c_str(), &findFileData);
		if (hFind == INVALID_HANDLE_VALUE) {
			std::cerr << "FindFirstFile failed: " << GetLastError() << std::endl;
			return;
		}

		do {
			const std::string fileName = findFileData.cFileName;

			// escape  "." , ".."
			if (fileName != "." && fileName != "..") {
				std::string fullPath = dirPath + "\\" + fileName;

				if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					// delete subdirs
					DeleteDirectoryContents(fullPath);
					// delete empty dirs
					RemoveDirectory(fullPath.c_str());
				}
				else {
					// delete file
					if (DeleteFile(fullPath.c_str())) {
						std::cout << "Deleted file: " << fullPath << std::endl;
					}
					else {
						std::cerr << "Failed to delete file: " << fullPath << ". Error: " << GetLastError() << std::endl;
					}
				}
			}
		} while (FindNextFile(hFind, &findFileData) != 0);

		FindClose(hFind);
#else
		//linux
		return ;
#endif
	}

	//delete dir_path(include itself) and children(include subdirs and files)
	void deleteDirectory(string& dirPath) {
#ifdef _WIN32
		DeleteDirectoryContents(dirPath);
		if (RemoveDirectory(dirPath.c_str())) {
			std::cout << "Deleted directory: " << dirPath << std::endl;
		}
		else {
			std::cerr << "Failed to delete directory: " << dirPath << ". Error: " << GetLastError() << std::endl;
		}
#else
		//linux
		return ;
#endif
	}

	bool copyFile(const std::string& src, const std::string& dest) {
		std::ifstream srcFile(src, std::ios::binary);
		if (!srcFile) {
			std::cerr << "Failed to open source file: " << src << std::endl;
			return false;
		}

		std::ofstream destFile(dest, std::ios::binary);
		if (!destFile) {
			std::cerr << "Failed to open destination file: " << dest << std::endl;
			return false;
		}
		destFile << srcFile.rdbuf();

		if (!destFile) {
			std::cerr << "Failed to write to destination file: " << dest << std::endl;
			return false;
		}

		srcFile.close();
		destFile.close();

		return true;
	}

	bool rename(const std::string& oldPath, const std::string& newPath) {
		return std::rename(oldPath.c_str(), newPath.c_str()) == 0;
	}

	string normalizationPath(string& s)
	{
		s = replaceStr(s, "\\\\", "/");
		s = replaceStr(s, "\\", "/");
		s = replaceStr(s, "//", "/");
		return s;
	}

	void getFolderList(vector<DB_FS::FILE_INFO>& list, string strFolder, bool recursive) {
#ifndef _WINXP
		try
		{
			wstring wstrFolder = DB_STR::utf8_to_utf16(strFolder);
			for (auto& i : fs::directory_iterator(wstrFolder)) {
				if (fs::is_directory(i.path())) {
					FILE_INFO fi;
					fi.path = DB_STR::gb_to_utf8(i.path().string());
					fi.path = replaceStr(fi.path, "\\", "/");
					size_t pos = fi.path.rfind("/");
					fi.folderPath = fi.path.substr(0, pos);
					fi.name = fi.path.substr(pos + 1, fi.path.length() - pos - 1);
					fi.len = fs::file_size(i.path());
					auto ftime = fs::last_write_time(i.path());
					// 将 file_time_type 转换为 system_clock::time_point
					auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
						);
					auto ti = std::chrono::system_clock::to_time_t(sctp);
					std::stringstream ss;
					ss << std::put_time(std::localtime(&ti), "%Y-%m-%d %H:%M:%S");
					fi.modifyTime = ss.str();

					list.push_back(fi);

					if (recursive) {
						getFolderList(list, DB_STR::gb_to_utf8(i.path().string()), recursive);
					}
				}
			}
		}
		catch (exception&) {
		}
#else
#endif
	}


	void getFileList(vector<DB_FS::FILE_INFO>& list, string strFolder, bool recursive, string suffix, vector<string>* exclude) {
#ifndef _WINXP
		try
		{
			wstring wstrFolder = DB_STR::utf8_to_utf16(strFolder);
			for (auto& i : fs::directory_iterator(wstrFolder)) {
				FILE_INFO fi;
				fi.path = DB_STR::gb_to_utf8(i.path().string());
				fi.name = DB_STR::gb_to_utf8(i.path().filename().string());
				if (exclude != nullptr) {
					bool excluded = false;
					for (int i = 0; i < exclude->size(); i++) {
						string ep = exclude->at(i);
						if (fi.name == ep) {
							excluded = true;
							break;
						}
					}

					if (excluded) {
						continue;
					}
				}

				if (fs::is_directory(i.path())) {
					if (recursive) {
						getFileList(list, DB_STR::gb_to_utf8(i.path().string()), recursive, suffix, exclude);
					}
				}
				else {
					//std::filesystem::file_time_type ft = i.last_write_time();
					//std::time_t tt = decltype(ft)::clock::to_time_t();
					fi.path = replaceStr(fi.path, "\\", "/");
					if (suffix != "*" && fi.path.find(suffix) == string::npos)
						continue;
					size_t pos = fi.path.rfind("/");
					fi.folderPath = fi.path.substr(0, pos);
					fi.len = fs::file_size(i.path());
					auto ftime = fs::last_write_time(i.path());
					// 将 file_time_type 转换为 system_clock::time_point
					auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
						);
					auto ti = std::chrono::system_clock::to_time_t(sctp);
					std::stringstream ss;
					ss << std::put_time(std::localtime(&ti), "%Y-%m-%d %H:%M:%S");
					fi.modifyTime = ss.str();
					list.push_back(fi);
				}
			}
		}
		catch (exception&) {
		}
#else
#endif
	}


	void getFileList(vector<string>& list, string strFolder, bool includeFolder, bool recursive)
	{
		vector<DB_FS::FILE_INFO> filist;
		getFileList(filist, strFolder, recursive);
		for (int i = 0; i < filist.size(); i++) {
			DB_FS::FILE_INFO& fi = filist[i];
			list.push_back(fi.path);
		}
	}
}

#define TDB_BASE64_PAD '='
#define TDB_BASE64DE_FIRST '+'
#define TDB_BASE64DE_LAST 'z'

/* BASE 64 encode table */
static const char tdb_base64en[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/',
};

/* ASCII order for BASE 64 decode, 255 in unused character */
static const unsigned char tdb_base64de[] = {
	/* nul, soh, stx, etx, eot, enq, ack, bel, */
	   255, 255, 255, 255, 255, 255, 255, 255,

	/*  bs,  ht,  nl,  vt,  np,  cr,  so,  si, */
	   255, 255, 255, 255, 255, 255, 255, 255,

	/* dle, dc1, dc2, dc3, dc4, nak, syn, etb, */
	   255, 255, 255, 255, 255, 255, 255, 255,

	/* can,  em, sub, esc,  fs,  gs,  rs,  us, */
	   255, 255, 255, 255, 255, 255, 255, 255,

	/*  sp, '!', '"', '#', '$', '%', '&', ''', */
	   255, 255, 255, 255, 255, 255, 255, 255,

	/* '(', ')', '*', '+', ',', '-', '.', '/', */
	   255, 255, 255,  62, 255, 255, 255,  63,

	/* '0', '1', '2', '3', '4', '5', '6', '7', */
	    52,  53,  54,  55,  56,  57,  58,  59,

	/* '8', '9', ':', ';', '<', '=', '>', '?', */
	    60,  61, 255, 255, 255, 255, 255, 255,

	/* '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', */
	   255,   0,   1,  2,   3,   4,   5,    6,

	/* 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', */
	     7,   8,   9,  10,  11,  12,  13,  14,

	/* 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', */
	    15,  16,  17,  18,  19,  20,  21,  22,

	/* 'X', 'Y', 'Z', '[', '\', ']', '^', '_', */
	    23,  24,  25, 255, 255, 255, 255, 255,

	/* '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', */
	   255,  26,  27,  28,  29,  30,  31,  32,

	/* 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', */
	    33,  34,  35,  36,  37,  38,  39,  40,

	/* 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', */
	    41,  42,  43,  44,  45,  46,  47,  48,

	/* 'x', 'y', 'z', '{', '|', '}', '~', del, */
	    49,  50,  51, 255, 255, 255, 255, 255
};

unsigned int
tdb_base64_encode(const unsigned char* in, unsigned int inlen, char* out)
{
	int s;
	unsigned int i;
	unsigned int j;
	unsigned char c;
	unsigned char l;

	s = 0;
	l = 0;
	for (i = j = 0; i < inlen; i++) {
		c = in[i];

		switch (s) {
		case 0:
			s = 1;
			out[j++] = tdb_base64en[(c >> 2) & 0x3F];
			break;
		case 1:
			s = 2;
			out[j++] = tdb_base64en[((l & 0x3) << 4) | ((c >> 4) & 0xF)];
			break;
		case 2:
			s = 0;
			out[j++] = tdb_base64en[((l & 0xF) << 2) | ((c >> 6) & 0x3)];
			out[j++] = tdb_base64en[c & 0x3F];
			break;
		}
		l = c;
	}

	switch (s) {
	case 1:
		out[j++] = tdb_base64en[(l & 0x3) << 4];
		out[j++] = TDB_BASE64_PAD;
		out[j++] = TDB_BASE64_PAD;
		break;
	case 2:
		out[j++] = tdb_base64en[(l & 0xF) << 2];
		out[j++] = TDB_BASE64_PAD;
		break;
	}

	out[j] = 0;

	return j;
}

unsigned int tdb_base64_decode(const char* in, unsigned int inlen, unsigned char* out)
{
	unsigned int i;
	unsigned int j;
	unsigned char c;

	if (inlen & 0x3) {
		return 0;
	}

	for (i = j = 0; i < inlen; i++) {
		if (in[i] == TDB_BASE64_PAD) {
			break;
		}
		if (in[i] < TDB_BASE64DE_FIRST || in[i] > TDB_BASE64DE_LAST) {
			return 0;
		}

		c = tdb_base64de[(unsigned char)in[i]];
		if (c == 255) {
			return 0;
		}

		switch (i & 0x3) {
		case 0:
			out[j] = (c << 2) & 0xFF;
			break;
		case 1:
			out[j++] |= (c >> 4) & 0x3;
			out[j] = (c & 0xF) << 4;
			break;
		case 2:
			out[j++] |= (c >> 2) & 0xF;
			out[j] = (c & 0x3) << 6;
			break;
		case 3:
			out[j++] |= c;
			break;
		}
	}

	return j;
}

int _db_vscprintf_cross(const char* format, va_list pargs) {
	int retval;
	va_list argcopy;
	va_copy(argcopy, pargs);
	retval = vsnprintf(NULL, 0, format, argcopy);
	va_end(argcopy);
	return retval;
}
std::string formatStr(const char* pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt);
	{
		int nLength = _db_vscprintf_cross(pszFmt, args);
		nLength += 1;  //length return contains \0 in the end
		std::vector<char> vectorChars(nLength);
		vsnprintf(vectorChars.data(), nLength, pszFmt, args);
		str.assign(vectorChars.data());
	}
	va_end(args);
	return str;
}

int _vscprintf_cross_dblog(const char* format, va_list pargs) {
	int retval;
	va_list argcopy;
	va_copy(argcopy, pargs);
	retval = vsnprintf(NULL, 0, format, argcopy);
	va_end(argcopy);
	return retval;
}

void DBLog(const char* pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt);
	{
		int nLength = _vscprintf_cross_dblog(pszFmt, args);
		nLength += 1;
		std::vector<char> vectorChars(nLength);
		vsnprintf(vectorChars.data(), nLength, pszFmt, args);
		str.assign(vectorChars.data());
	}
	va_end(args);

	DB_TIME stNow;
	stNow.setNow();
	string time = formatStr("%02d:%02d:%02d.%03d", stNow.wHour, stNow.wMinute, stNow.wSecond, stNow.wMilliseconds);
	string logline = time + " " + str;
	printf("%s\n", logline.c_str());
}


bool shouldErase(const std::pair<string, FILE_BUFF*>& pair) {

	return false;
}

void bufferManageThread(TDB* p) {
	DB_TIME lastCheck;
	lastCheck.setNow();
	while (1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (TIME_OPT::calcTimePassSecond(lastCheck) < p->m_bufferTTL / 2) {
			continue;
		}

		if (p->m_bEnableFsBuff) {
			p->m_FsBuff.m_csFsb.lock();
			std::map<string, FILE_BUFF*> mapTmp;
			for (auto& iter : p->m_FsBuff.m_mapFsBuff) {
				int bufferredTime = TIME_OPT::calcTimePassSecond(iter.second->lastActive);
				if (bufferredTime < p->m_bufferTTL) {
					mapTmp.insert(iter);
				}
				else {
					delete iter.second;
				}
			}
			p->m_FsBuff.m_mapFsBuff = mapTmp;
			p->m_FsBuff.m_csFsb.unlock();
		}
	}
}

TDB::TDB()
{
	m_getTagsByTagSelector = nullptr;
	m_isGbk = false;
	m_timeUnit = BY_DAY;
	m_bEnableFsBuff = false;
	m_bufferTTL = 3 * 3600;
	thread t(bufferManageThread, this);
	t.detach();
}

string TDB::getPath_deFile(string strTag, DB_TIME stTime)
{
	if (m_timeUnit == BY_DAY) {
		strTag = replaceStr(strTag, ".", "/");
		string strURL = formatStr("/%04d%02d/%02d/", stTime.wYear, stTime.wMonth, stTime.wDay);
		if (m_dbFmt.dbRootTag != "") {
			string dbRootTag = replaceStr(m_dbFmt.dbRootTag, ".", "/");
			strURL += dbRootTag + "/";
		}
		strURL += strTag;
		string timeStamp = formatStr("%02d%02d%02d", stTime.wHour, stTime.wMinute, stTime.wSecond);
		strURL += "/" + timeStamp;
		return strURL;
	}
	else if (m_timeUnit == BY_MONTH) {
		strTag = replaceStr(strTag, ".", "/");
		string strURL = formatStr("/%04d%02d/", stTime.wYear, stTime.wMonth);
		if (m_dbFmt.dbRootTag != "") {
			string dbRootTag = replaceStr(m_dbFmt.dbRootTag, ".", "/");
			strURL += dbRootTag + "/";
		}
		strURL += strTag;
		string timeStamp = formatStr("%02d%02d%02d", stTime.wHour, stTime.wMinute, stTime.wSecond);
		strURL += "/" + timeStamp;
		return strURL;
	}
	else if (m_timeUnit == NONE) {
		strTag = replaceStr(strTag, ".", "/");
		string strURL = m_path + "/" + strTag;
		return strURL;
	}
	return "";
}

string TDB::getPath_dataFolder(string strTag, const DB_TIME& date, const string &deType) const
{
	string pathType = "day";
	if (deType == "statisByDay" || deType == "statisDe")
	{
		pathType = "day";
	}
	else if (deType == "statisByMonth")
	{
		pathType = "month";
	}
	else if (m_timeUnit == BY_DAY) {
		pathType = "day";
	}
	else if (m_timeUnit == BY_MONTH) {
		pathType = "month";		
	}
	else
		pathType = "none";

	if (pathType == "day")
	{
		strTag = changeCharForFileName(strTag);
		strTag = replaceStr(strTag, ".", "/");
		string strURL = formatStr("/%04d%02d/%02d/", date.wYear, date.wMonth, date.wDay);
		if (m_dbFmt.dbRootTag != "") {
			string dbRootTag = replaceStr(m_dbFmt.dbRootTag, ".", "/");
			strURL += dbRootTag + "/";
		}
		strURL += strTag;
		strURL = m_path + strURL;
		return strURL;
	}
	else if (pathType == "month")
	{
		strTag = changeCharForFileName(strTag);
		strTag = replaceStr(strTag, ".", "/");
		string strURL = formatStr("/%04d%02d/", date.wYear, date.wMonth);
		if (m_dbFmt.dbRootTag != "") {
			string dbRootTag = replaceStr(m_dbFmt.dbRootTag, ".", "/");
			strURL += dbRootTag + "/";
		}
		strURL += strTag;
		strURL = m_path + strURL;
		return strURL;
	}
	else if (pathType == "none") {
		strTag = replaceStr(strTag, ".", "/");
		string strURL = m_path + "/" + strTag;
		return strURL;
	}
	return "";
}
// no '/' in bengin ,and in end
string TDB::getPath_dataFolder_NO_DB(string strTag, const DB_TIME &date) const
{
	if (m_timeUnit == BY_DAY) {
		strTag = changeCharForFileName(strTag);
		strTag = replaceStr(strTag, ".", "/");
		string strURL = formatStr("%04d%02d/%02d/", date.wYear, date.wMonth, date.wDay);
		if (m_dbFmt.dbRootTag != "") {
			string dbRootTag = replaceStr(m_dbFmt.dbRootTag, ".", "/");
			strURL += dbRootTag + "/";
		}
		strURL += strTag;
		return strURL;
	}
	else if (m_timeUnit == BY_MONTH) {
		strTag = changeCharForFileName(strTag);
		strTag = replaceStr(strTag, ".", "/");
		string strURL = formatStr("%04d%02d/", date.wYear, date.wMonth);
		if (m_dbFmt.dbRootTag != "") {
			string dbRootTag = replaceStr(m_dbFmt.dbRootTag, ".", "/");
			strURL += dbRootTag + "/";
		}
		strURL += strTag;
		return strURL;
	}
	else if (m_timeUnit == NONE) {
		strTag = replaceStr(strTag, ".", "/");
		string strURL =  strTag;
		return strURL;
	}
	return "";
}

string TDB::getPath_dbRoot()
{
	return m_path;
}

string TDB::getName_deFile(string tag, DB_TIME time)
{
	string timeStamp = formatStr("%02d%02d%02d", time.wHour, time.wMinute, time.wSecond);
	return timeStamp;
}

//8 illigal filename char
string ic1 = formatStr("[%02X]", '\\');
string ic2 = formatStr("[%02X]", ':');
string ic3 = formatStr("[%02X]", '*');
string ic4 = formatStr("[%02X]", '?');
string ic5 = formatStr("[%02X]", '\"');
string ic6 = formatStr("[%02X]", '<');
string ic7 = formatStr("[%02X]", '>');
string ic8 = formatStr("[%02X]", '|');
string ic9 = formatStr("[%02X]", '/');

//escple illigal filename char  / \ : * ? " < > |
string TDB::changeCharForFileName(string s)  const {
	string out;
	for (int i = 0; i < s.length(); i++)
	{
		char c = s[i];
		if (c == '\\')
		{
			out.append(ic1);
		}
		else if (c == ':')
		{
			out.append(ic2);
		}
		else if (c == '*')
		{
			out.append(ic3);
		}
		else if (c == '?')
		{
			out.append(ic4);
		}
		else if (c == '\"')
		{
			out.append(ic5);
		}
		else if (c == '<')
		{
			out.append(ic6);
		}
		else if (c == '>')
		{
			out.append(ic7);
		}
		else if (c == '|')
		{
			out.append(ic8);
		}
		else if (c == '/')
		{
			out.append(ic9);
		}
		else
		{
			out.append(1, c);
		}
	}
	return out;
}

//generate  or get the existted
string TDB::getPath_dbFile(string tag, string time, string deType)
{
	DB_TIME dbt;
	dbt.fromStr(time);
	return getPath_dbFile(tag, dbt, deType);
}

string TDB::getPath_dbFile(string strTag, const DB_TIME &date,string deType) const
{
	string folder = getPath_dataFolder(strTag,date, deType);
	if (deType == "") {
		//auto judge
		if (fileExist((folder + "/" + m_dbFmt.deListName).c_str()))
			return folder + "/" + m_dbFmt.deListName;

		else if (fileExist((folder + "/" + m_dbFmt.curveIdxListName).c_str()))
			return folder + "/" + m_dbFmt.curveIdxListName;
		else if (fileExist((folder + "/" + date.toStampHMS() + m_dbFmt.curveDeNameSuffix).c_str()))
			return folder + "/" + date.toStampHMS() + m_dbFmt.curveDeNameSuffix;
		else
			return folder + "/" + m_dbFmt.deListName;
	}
	else if (deType == "curveIdx") {
		return folder + "/" + m_dbFmt.curveIdxListName;
	}
	else if (deType == "statisDe" || deType == "statisByDay" || deType == "statisByMonth") {
		return folder + "/" + m_dbFmt.deListStatisticsName;
	}
	else if (deType == "curve") {
		return folder + "/" + date.toStampHMS()  + m_dbFmt.curveDeNameSuffix;
	}
	else if (deType == "image") {
		return folder + "/" + date.toStampHMS() + ".image.jpg";
	}
	else if (deType == "imageInfo") {
		return folder + "/" + date.toStampHMS() + ".imageInfo.json";
	}
	else {
		return folder + "/" + m_dbFmt.deListName;
	}
}

string TDB::getDeFilesFolder(string& deListFolder, DB_TIME& time) {
	string s;
	if (m_timeUnit == DB_TIME_UNIT::BY_DAY) {
		s = deListFolder + "/" + time.toStampHMS();
	}
	else if (m_timeUnit == DB_TIME_UNIT::BY_MONTH) {

	}
	else if(m_timeUnit == DB_TIME_UNIT::BY_MONTH) {

	}
	else if (m_timeUnit == DB_TIME_UNIT::NONE) {
		s = deListFolder + "/" + time.toStampFull();
	}
	else {
		s = deListFolder + "/" + time.toStampHMS();
	}
	return s;
}
//1. Store data element files (curves, JSON) or data element related files (images) 2. Store data element index files or data element list files
//1.存数据元文件(曲线、json)或存数据元相关文件(图片) 2.存数据元索引文件或数据元列表文件
void TDB::Insert(string strTag,  string& sDe, DB_TIME* time)
{
	DB_TIME stTime;
	if (time) {
		stTime = *time;
	}
	else {
		stTime = TIME_OPT::now();
	}

	string deListFolderPath = getPath_dataFolder(strTag, stTime);
	if (!fileExist(deListFolderPath)) {
		DB_FS::createFolderOfPath(deListFolderPath.c_str());
	}

	yyjson_doc* doc = yyjson_read(sDe.c_str(), sDe.length(), 0);
	yyjson_mut_doc* mdoc = yyjson_doc_mut_copy(doc, NULL);
	yyjson_val* yyDe = yyjson_doc_get_root(doc);
	yyjson_mut_val* yymDe = yyjson_mut_doc_get_root(mdoc);
	yyjson_mut_val* timeKey = yyjson_mut_strcpy(mdoc, "time");

	string sTime = stTime.toStr(true);
	yyjson_mut_val* timeVal = yyjson_mut_strcpy(mdoc, sTime.data());
	yyjson_mut_obj_put(yymDe, timeKey, timeVal);

	//write file data
	vector<string> fileUrl;
	string fileType;
	yyjson_val* yyv_file = yyjson_obj_get(yyDe, "file");
	if (yyv_file) {
		//save to a directory name as timestamp
		if (yyjson_is_arr(yyv_file)) {
			string deFilesFolder = getDeFilesFolder(deListFolderPath, stTime);
			if (!fileExist(deFilesFolder)) {
				DB_FS::createFolderOfPath(deFilesFolder.c_str());
			}

			size_t idx = 0;
			size_t max = 0;
			yyjson_val* item;
			yyjson_arr_foreach(yyv_file, idx, max, item) {
				string url = saveDEFile(item, deFilesFolder, stTime, fileType);
				url = url.substr(m_path.length(), url.length() - m_path.length());
				fileUrl.push_back(url);
			}
		}
		//save to a de file in the same folder as deList file
		else if (yyjson_is_obj(yyv_file)) {
			//Data element files: curves, various custom JSON (such as inspection records), data element related files: images
			saveDEFile(yyv_file, deListFolderPath, stTime, fileType);
		}
	}

	string dataListPath;
	if(fileType == "curve")
		dataListPath = deListFolderPath + "/" + m_dbFmt.curveIdxListName;
	else
		dataListPath = deListFolderPath + "/" + m_dbFmt.deListName;


	//write de
	//delete file data,only file index
	yyjson_mut_val* yymv_dataFile = yyjson_mut_obj_get(yymDe, "file");
	if (yymv_dataFile) {
		if (yyjson_mut_is_arr(yymv_dataFile)) {
			size_t idx = 0;
			size_t max = 0;
			yyjson_mut_val* item;
			yyjson_mut_arr_foreach(yymv_dataFile, idx, max, item) {
				yyjson_mut_obj_remove_key(item, "data");
				//generate url when insert data,better performance than generate when select data
				string url = fileUrl[idx];
				string urlAbs = "/db";
				if (m_name != "")
					urlAbs += "/" + m_name;
				urlAbs += url;
			
				yyjson_mut_val* urlKey = yyjson_mut_strcpy(mdoc,"url");
				yyjson_mut_val* urlVal = yyjson_mut_strcpy(mdoc, urlAbs.c_str());
				yyjson_mut_obj_put(item, urlKey, urlVal);
			}
		}
		else if (yyjson_mut_is_obj(yymv_dataFile)) {
			yyjson_mut_obj_remove_key(yymv_dataFile, "data");
		}
	}

	saveDeToDataListFile(dataListPath, yymDe);

	yyjson_mut_doc_free(mdoc);
	yyjson_doc_free(doc);
}

struct DE_TEMP {
	yyjson_mut_val* de;
	string sortVal; 

};

bool TDB::saveDeToDataListFile(string dataListPath, yyjson_mut_val* yymDe) {
	if (m_bEnableFsBuff) {
		bool bAppend = false;
		m_FsBuff.m_csFsb.lock();
		std::map<string, FILE_BUFF*>::iterator iter = m_FsBuff.m_mapFsBuff.find(dataListPath);
		if (iter != m_FsBuff.m_mapFsBuff.end()) {
			string& fileData = iter->second->data;  // can be an empty file ,length is 0

			string strDe;

			size_t len = 0;
			char* pDe = yyjson_mut_val_write(yymDe, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
			if (pDe) {
				strDe = pDe;
				free(pDe);
			}

			if (fileData.size() > 0) {
				fileData.resize(fileData.size() - 1);
				fileData += ",";
				fileData += strDe;
				fileData += "]";
			}
			else {
				fileData = strDe;
				fileData = "[" + fileData + "]";
			}				
		}
		m_FsBuff.m_csFsb.unlock();
	}


	if (!fileExist(dataListPath.c_str())) //first de to save
	{
		string fileData;

		size_t len = 0;
		char* pDe = yyjson_mut_val_write(yymDe, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
		if (pDe) {
			fileData = pDe;
			free(pDe);
		}

		fileData = "[" + fileData + "]";
		if (!DB_FS::writeFile(dataListPath, (unsigned char*)fileData.c_str(), fileData.length()))
		{
			printf("[error]save to db file fail,dataListFile path:%s,data:%s", dataListPath.c_str(), fileData.c_str());
		}
	}
	else
	{
#ifdef _WIN32
		FILE* fp = _wfopen(DB_STR::utf8_to_utf16(dataListPath).c_str(), L"rb+");
#else
		//FILE* fp = fopen(dlPath.c_str(), "rb+");
		FILE* fp = fopen(dataListPath.c_str(), "rb+");
#endif

		if (fp)
		{
			fseek(fp, 0L, SEEK_END);
			long len = ftell(fp);

			if (len > 0)
			{
				fseek(fp, len - 1, SEEK_SET);

				std::string d = ",";

				string str;

				size_t len = 0;
				char* s = yyjson_mut_val_write(yymDe, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
				if (s) {
					str = s;
					free(s);
				}

				d += str;
				d += "]";

				fwrite(d.c_str(), 1, d.length(), fp);
			}
			else
			{
				string fileData;

				size_t len = 0;
				char* pDe = yyjson_mut_val_write(yymDe, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
				if (pDe) {
					fileData = pDe;
					free(pDe);
				}

				fileData = "[" + fileData + "]";
				fwrite(fileData.c_str(), 1, fileData.length(), fp);
			}

			fclose(fp);
		}
	}

	return true;
}


//for yyjson debug, dump json string to debug
//copy mut_val before put in to a new mut_obj , otherwise the origin val will be changed
string printfTimeSection(map<string, yyjson_mut_val*>* timeSection) {
	printf("********time section dump*********\n");
	if (timeSection == nullptr)
		printf("null");
	else {
		for (auto& i : *timeSection) {
			printf("%s\t", i.first.c_str());

			char* sz = yyjson_mut_val_write(i.second, 0, nullptr);
			if (sz) {
				printf("%s\n", sz);
				free(sz);
			}
		}
	}
	return "";
}

#ifdef _DEBUG
std::string yyvalDump(yyjson_mut_val* val) {
	char* str = yyjson_mut_val_write(val, 0, nullptr);
	if (str != nullptr) {
		std::string result(str);
		std::cout << result << std::endl;
		free(str);
		return result;
	}
	return "";
}

std::string yyvalDump(yyjson_val* val) {
	char* str = yyjson_val_write(val, 0, nullptr);
	if (str != nullptr) {
		std::string result(str);
		std::cout << result << std::endl;
		free(str);
		return result;
	}
	return "";
}
#endif

bool TDB::Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, vector<DATA_SET*>& set_list, map<SORT_FLAG, yyjson_mut_val*>& mapRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc) {
	bool withTag = deSel.tagSel.getTag;
	map<string, map<string, set<yyjson_mut_val*>>> timeSectionSeries; 

	//generate output de
	for (int tagIdx = 0; tagIdx < set_list.size(); tagIdx++) {
		DATA_SET& fSet = *set_list[tagIdx];

		string& tagAlias = fSet.colKey;
		string& tag = fSet.tag;

		for (int j = 0; j < fSet.m_afterAggr.size(); j++) {
			DE_yyjson& deyy = *fSet.m_afterAggr[j];

			//create a output de
			yyjson_mut_val* jRecord;
			if (deSel.bAggr) { //create a mut obj in a aggr select mode
				jRecord = yyjson_mut_obj(mut_doc);
			}
			else if(deyy.de != nullptr) { //copy directly for speed in a none aggr select mode
				jRecord = deyy.de;
			}
			else {
				jRecord = yyjson_mut_obj(mut_doc);
			}
			
			//set time
			yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, CONST_STR::time.c_str());
			yyjson_mut_val* timeVal = yyjson_mut_str(mut_doc, deyy.deTime.data());
			yyjson_mut_obj_put(jRecord, timeKey, timeVal);

			//set keys except time
			if (deyy.items.size()>0) {
				for (auto& i : deyy.items) {
					yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, i.first.c_str());
					yyjson_mut_obj_put(jRecord, valKey, i.second);
				}
			}

			//set val
			if (deyy.val != nullptr) {
				yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, m_dbFmt.deItemKey_value.c_str());
				yyjson_mut_obj_put(jRecord, valKey, deyy.val);
			}

			//set tag
			if (withTag) {
				//in a multi tag selection ,tag must be set
				//tagAlias memory can not release before write_doc,otherwise causes crash
				yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, "tag");
				yyjson_mut_val* tagVal = yyjson_mut_str(mut_doc, tagAlias.c_str());
				yyjson_mut_obj_put(jRecord, tagKey, tagVal);
			}

		
			auto iter = timeSectionSeries.find(deyy.deTime.data());
			if (iter == timeSectionSeries.end()) {
				map<string, set<yyjson_mut_val*>> timeSection;
				timeSection[tag].insert(jRecord);
				timeSectionSeries[deyy.deTime.data()] = timeSection;
			}
			else {
				map<string, set<yyjson_mut_val*>>& timeSection = iter->second;
				timeSection[tag].insert(jRecord);
			}

			result.rowCount++;

			if (deSel.timeSel.AmountMatch(result.rowCount)) {
				break;
			}
		}
	}

	//time section fill,set time of the filled de
	if (deSel.timeFill) {
		int addDeCount = 0;
		map<string, set<yyjson_mut_val*>>* lastSection = nullptr;

		for (auto& iter : timeSectionSeries) {
			map<string, set<yyjson_mut_val*>>& timeSection = iter.second;

			for (int tagIdx = 0; tagIdx < set_list.size(); tagIdx++) {
				DATA_SET& fSet = *set_list[tagIdx];
				string& tag = fSet.tag;

				auto j = timeSection.find(tag);
				if (j == timeSection.end()) { //tag does not have data in this time section,need to be filled
					if (lastSection != nullptr) {
						auto k = lastSection->find(tag);
						if (k != lastSection->end()) {
							set<yyjson_mut_val*> jReSet;

							//a de must exist in this time section,use the first de to get the time of this time section
							for (auto jValRefRec : k->second) {
								yyjson_mut_val* jRecord = yyjson_mut_obj(mut_doc);
								yyjson_mut_val* jTimeRefRec = *(timeSection.begin()->second.begin());
								yyjson_mut_val* yyTimeSrc = yyjson_mut_obj_get(jTimeRefRec, "time");
								yyjson_mut_val* yyTime = yyjson_mut_val_mut_copy(mut_doc, yyTimeSrc);
								yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, CONST_STR::time.c_str());
								yyjson_mut_obj_put(jRecord, timeKey, yyTime);
	
								//yyjson_mut_val* jValRefRec = k->second;
								yyjson_mut_val* yyValSrc = yyjson_mut_obj_get(jValRefRec, m_dbFmt.deItemKey_value.c_str());
								yyjson_mut_val* yyVal = yyjson_mut_val_mut_copy(mut_doc, yyValSrc);  //a copy operation must be done, do not put yyValSrc into obj, it causes error in dumped json string. may be the obj the pointer pointed is a node of a linked list,if in two obj at the same time,causes error when yyjson try to dump the linked list
								yyjson_mut_val* valKey = yyjson_mut_str(mut_doc, CONST_STR::val.c_str());
								yyjson_mut_obj_put(jRecord, valKey, yyVal);
	
								yyjson_mut_val* yyTagSrc = yyjson_mut_obj_get(jValRefRec, "tag");
								yyjson_mut_val* yyTag = yyjson_mut_val_mut_copy(mut_doc, yyTagSrc);
								yyjson_mut_val* tagKey = yyjson_mut_str(mut_doc, CONST_STR::tag.c_str());
								yyjson_mut_obj_put(jRecord, tagKey, yyTag);
	
								//timeSection[tag] = jRecord;
								jReSet.insert(jRecord);
								addDeCount++;
							}

							timeSection[tag].swap(jReSet);
						}
					}
				}
			}

			lastSection = &timeSection;
		}
	}
	
	//sort de and output
	for (auto& i : timeSectionSeries) {
		map<string, set<yyjson_mut_val*>>& timeSection = i.second;

		for (auto& j : timeSection) {
			SORT_FLAG sortFlag;

			for (auto jRec : j.second) {	
				if (deSel.sortKey.length() > 0) {
					yyjson_mut_val* yyVal = yyjson_mut_obj_get(jRec, m_dbFmt.deItemKey_value.c_str());

					if (deSel.sortKey == "val") {
						if (yyjson_mut_is_str(yyVal)) {
							sortFlag.sFlag = yyjson_mut_get_str(yyVal);
						}
						else if (yyjson_mut_is_num(yyVal)) {
							sortFlag.dbFlag = yyjson_mut_get_real(yyVal);
						}
					}
					else if (yyjson_mut_is_obj(yyVal)) {
						yyjson_mut_val* yySortKey = yyjson_mut_obj_get(yyVal, deSel.sortKey.c_str());

						if (yyjson_mut_is_str(yySortKey)) {
							sortFlag.sFlag = yyjson_mut_get_str(yySortKey);
						}
						else if (yyjson_mut_is_num(yySortKey)) {
							sortFlag.dbFlag = yyjson_mut_get_real(yySortKey);
						}
					}
				}
	
				sortFlag.sFlag += i.first + j.first + std::to_string(result.rowCount);
				mapRlt[sortFlag] = jRec; 
			}
		}
	}
	
	return true;
}

//only immut select use this function
bool TDB::Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<DATA_SET*>& set_list, map<SORT_FLAG, yyjson_mut_val*>& mapRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc)
{
	//generate output de
	for (int tagIdx = 0; tagIdx < set_list.size(); tagIdx++)
	{
		DATA_SET& fSet = *set_list[tagIdx];
		SORT_FLAG sf;
		for (int j = 0; j < fSet.m_orgDe.size(); j++) {
			sf.dbFlag = j;
			yyjson_mut_val* mde = yyjson_val_mut_copy(mut_doc, fSet.m_orgDe[j]);
			mapRlt[sf] =  mde;
		}
	}
	return true;
}

bool TDB::Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<DATA_SET*>& set_list, vector<yyjson_mut_val*>& vecRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc)
{
	//generate output de
	for (int tagIdx = 0; tagIdx < set_list.size(); tagIdx++)
	{
		DATA_SET& fSet = *set_list[tagIdx];
		vecRlt.resize(fSet.m_orgDe.size());
		for (int j = 0; j < fSet.m_orgDe.size(); j++) {
			yyjson_mut_val* mde = yyjson_val_mut_copy(mut_doc, fSet.m_orgDe[j]);
			vecRlt[j] = mde;
		}
	}
	return true;
}


yyjson_val* yyjson_obj_get_recursive(yyjson_val* obj, const char* key) {
	string recurKey = key;
	vector<string> keyLink;
	DB_STR::split(keyLink, key, ".");
	yyjson_val* ret = nullptr;
	for (auto& key : keyLink) {
		ret = yyjson_obj_get(obj, key.c_str());
		if (ret == nullptr)
			return ret;
		obj = ret;
	}

	return ret;
}

struct RANGE_INCREASE {
	yyjson_val* firstDe;
	yyjson_val* lastDe;
	DB_TIME lastDeTime;
	DB_TIME firstDeTime;
	double increase;
	DB_TIME_RANGE* pTimeRange;

	RANGE_INCREASE() {
		memset(this, 0, sizeof(RANGE_INCREASE));
	}

	void clear() {
		firstDe = nullptr;
		lastDe = nullptr;
		pTimeRange = nullptr;
		increase = 0;
	}
};

//at least 2 points in one range
map<string, double> TDB::doAggrOneGroup_increase_withTimeSlots(DE_SELECTOR& deSel, string& aggrKey, string groupKey,vector<yyjson_val*>& deGroup) {
	map<string, vector<DB_TIME_RANGE>>& mapTimeSlots = deSel.mapTimeSlots;
	
	//order by range start time
	map<string, double> slotIncrease;
	map<DB_TIME, RANGE_INCREASE*> listTimeRange;
	for (auto& slotIter : mapTimeSlots) {
		slotIncrease[slotIter.first] = 0;
		for (DB_TIME_RANGE& range : slotIter.second) {
			RANGE_INCREASE* ri = new RANGE_INCREASE();
			ri->pTimeRange = &range;
			listTimeRange[range.start] = ri;

			range.p = ri;
		}
	}

	if (listTimeRange.size()==0) return slotIncrease;

	map<DB_TIME, RANGE_INCREASE*>::iterator iterTimeRange = listTimeRange.begin();
	//get first and last de in each range
	yyjson_val* pDe = nullptr;
	yyjson_val* pPreviousDe = nullptr;
	DB_TIME t;
	DB_TIME previousDeTime;
	for (int deIdx = 0; deIdx < deGroup.size(); deIdx++) {
		if (deIdx > 0) {
			pPreviousDe = pDe;
			previousDeTime = t;
		}

		pDe = deGroup.at(deIdx);
		yyjson_val* yyTime = yyjson_obj_get(pDe, "time");
		t.fromStr(yyjson_get_str(yyTime));

		RANGE_INCREASE& ri = *iterTimeRange->second;
		//find range start de
		if (ri.firstDe == nullptr) {
			if (t >= ri.pTimeRange->start) { //set as start de if de time is equal to range start time
				if (t < ri.pTimeRange->end) {
					ri.firstDe = pDe;
					ri.firstDeTime = t;
					continue;
				}
				else {
					//check next range by the same de
					iterTimeRange++;
					if (iterTimeRange == listTimeRange.end())
						break;
					deIdx--;
					continue;
				}
			}
			else {
				continue;
			}
		}
		
		if (ri.lastDe == nullptr) {
			if (deIdx == deGroup.size() - 1) { //last de
				ri.lastDe = pDe;
				ri.lastDeTime = t;
				break;
			}
			else if (t == ri.pTimeRange->end) {
				ri.lastDe = pDe;
				ri.lastDeTime = t;
				//check next range by the same de
				iterTimeRange++;
				if (iterTimeRange == listTimeRange.end())
					break;
				deIdx--;
				continue;
			}
			else if (t > ri.pTimeRange->end) {
				ri.lastDe = pPreviousDe;
				ri.lastDeTime = previousDeTime;
				//check next range by the same de
				iterTimeRange++;
				if (iterTimeRange == listTimeRange.end())
					break;
				deIdx--;
				continue;
			}
		}
		else {
			//
		}
	}

	//sum each range incease
	bool aggrKeyUndefined = false;
	for (auto& slotIter : mapTimeSlots) {
		double dbIncrease = 0;
		for (DB_TIME_RANGE& iter : slotIter.second) {
			RANGE_INCREASE* pri = (RANGE_INCREASE*)iter.p;
			if (!pri->firstDe || !pri->lastDe) { //no de in this time range
				continue;
			}

			yyjson_val* pValFirst = yyjson_obj_get_recursive(pri->firstDe, aggrKey.c_str());
			if (pValFirst == nullptr) {
				aggrKeyUndefined = true;
				break;
			}
			yyjson_val* pValLast = yyjson_obj_get_recursive(pri->lastDe, aggrKey.c_str());
			if (pValLast == nullptr) {
				aggrKeyUndefined = true;
				break;
			}

			double dbFirst = 0;
			double dbLast = 0;

			if (yyjson_is_num(pValFirst)) {
				dbFirst = yyjson_get_num(pValFirst);
			}
			else if (deSel.isValTypeNumber() && yyjson_get_type(pValFirst) == YYJSON_TYPE_STR)
			{
				string valStr = yyjson_get_str(pValFirst);
				dbFirst = atof(valStr.data());
			}
			else {
				continue;
			}

			if (yyjson_is_num(pValLast)) {
				dbLast = yyjson_get_num(pValLast);
			}
			else if (deSel.isValTypeNumber() && yyjson_get_type(pValLast) == YYJSON_TYPE_STR)
			{
				string valStr = yyjson_get_str(pValLast);
				dbLast = atof(valStr.data());
			}
			else {
				continue;
			}
			dbIncrease += dbLast - dbFirst;
		}

		slotIncrease[slotIter.first] = dbIncrease;
	}

	for (auto& iter : listTimeRange) {
		delete iter.second;
	}

	if (aggrKeyUndefined) {
		string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group key is " + groupKey + ",group size is " + DB_STR::format(" % d", deGroup.size());
		db_exception e;
		e.m_error = sErr;
		throw e;
	}


	return slotIncrease;
}

double TDB::doAggrOneGroup_sum(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup) {
	double dbSum = 0;
	bool aggrKeyUndefined = true;
	for (int j = 0; j < deGroup.size(); j++) {
		yyjson_val* pDeSrc = deGroup.at(j);
		yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());
		if (pDeSrcVal == nullptr) {
			continue;
		}
		aggrKeyUndefined = false;
		double db = 0;

		if (yyjson_is_num(pDeSrcVal)) {
			db = yyjson_get_num(pDeSrcVal);
		}
		else if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR)
		{
			string valStr = yyjson_get_str(pDeSrcVal);
			db = atof(valStr.data());
		}
		else {
			continue;
		}
		dbSum += db;
	}

	if (aggrKeyUndefined) {
		string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group size is " + DB_STR::format("%d", deGroup.size());
		db_exception e;
		e.m_error = sErr;
		throw e;
	}

	return dbSum;
}

//enum DB_VAL_TYPE {
//	DVT_UNKNOWN,
//	DVT_BOOL,
//	DVT_INT,
//	DVT_FLOAT
//};

struct DURATION_INFO {
	time_t duration;
	int percentage;
	DURATION_INFO() {
		duration = 0;
		percentage = 0;
	}
};

struct DURATION_CALC {
	bool calcBoolVal;
	int calcIntVal;
	map<bool, DURATION_INFO> boolDurations;
	map<int, DURATION_INFO> intDurations;
	DB_TIME startTime;
	bool startCalc;

	DURATION_CALC() {
		startCalc = false;
	}
};

void TDB::doAggrOneGroup_duration(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup,yyjson_mut_val*& pAggrRlt,yyjson_mut_doc* yydoc) {
	bool aggrKeyUndefined = true;

	DURATION_CALC calc;
	for (int j = 0; j < deGroup.size(); j++) {
		yyjson_val* pDeSrc = deGroup.at(j);
		yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());
		if (pDeSrcVal == nullptr) {
			continue;
		}
		aggrKeyUndefined = false;

		if (yyjson_is_int(pDeSrcVal)) {
			int iVal = yyjson_get_int(pDeSrcVal);

			if (!calc.startCalc) {
				calc.startCalc = true;
				yyjson_val* yyTime = yyjson_obj_get(pDeSrc, "time");
				calc.startTime.fromStr(yyjson_get_str(yyTime));
				calc.calcIntVal = iVal;
				DURATION_INFO di;
				calc.intDurations[iVal] = di;
			}
			else {
				if (iVal != calc.calcIntVal || j == deGroup.size() - 1) //last val or val changed , sum up 
				{
					//get duration time len
					yyjson_val* yyTime = yyjson_obj_get(pDeSrc, "time");
					DB_TIME endTime;
					endTime.fromStr(yyjson_get_str(yyTime));
					time_t utEnd,utStart;
					utEnd = endTime.toUnixTime();
					utStart = calc.startTime.toUnixTime();
					time_t durationInSeconds = utEnd - utStart;

					//sum up to corresponding int val duration statis
					DURATION_INFO& durInfo = calc.intDurations[calc.calcIntVal];
					durInfo.duration += durationInSeconds;

					//start a new val statis
					if (calc.intDurations.find(iVal) == calc.intDurations.end()) {
						DURATION_INFO di;
						calc.intDurations[iVal] = di;
					}
					calc.calcIntVal = iVal;
					calc.startTime = endTime;
				}
				else {
					continue;
				}
			}
		}
		else if(yyjson_is_bool(pDeSrcVal)) {
			if (calc.boolDurations.size() == 0) { //always have true&false two slots
				DURATION_INFO di;
				calc.boolDurations[true] = di;
				calc.boolDurations[false] = di;
			}
			bool bVal = yyjson_get_bool(pDeSrcVal);

			if (!calc.startCalc) {
				calc.startCalc = true;
				yyjson_val* yyTime = yyjson_obj_get(pDeSrc, "time");
				calc.startTime.fromStr(yyjson_get_str(yyTime));
				calc.calcBoolVal = bVal;
			}
			else {
				if (bVal != calc.calcBoolVal || j == deGroup.size() - 1) //last val or val changed , sum up 
				{
					//get duration time len
					yyjson_val* yyTime = yyjson_obj_get(pDeSrc, "time");
					DB_TIME endTime;
					endTime.fromStr(yyjson_get_str(yyTime));
					time_t utEnd, utStart;
					utEnd = endTime.toUnixTime();
					utStart = calc.startTime.toUnixTime();
					time_t durationInSeconds = utEnd - utStart;

					//sum up to corresponding int val duration statis
					DURATION_INFO& durInfo = calc.boolDurations[calc.calcBoolVal];
					durInfo.duration += durationInSeconds;

					//start a new val statis
					if (calc.boolDurations.find(bVal) == calc.boolDurations.end()) {
						DURATION_INFO di;
						calc.boolDurations[bVal] = di;
					}
					calc.calcBoolVal = bVal;
					calc.startTime = endTime;
				}
				else {
					continue;
				}
			}
		}
		else {
			break;
		}
	}

	pAggrRlt = yyjson_mut_arr(yydoc);
	if (calc.intDurations.size() > 0) {
		time_t totalRangeTime = 0;
		for (auto& di : calc.intDurations) {
			totalRangeTime += di.second.duration;
		}
		for (auto& di : calc.intDurations) {
			yyjson_mut_val* oneSlot = yyjson_mut_obj(yydoc);

			yyjson_mut_val* yyKey = yyjson_mut_strcpy(yydoc, "slot");
			yyjson_mut_val* yyVal = yyjson_mut_int(yydoc, di.first);
			yyjson_mut_obj_put(oneSlot, yyKey, yyVal);
			yyKey = yyjson_mut_strcpy(yydoc, "duration");
			yyVal = yyjson_mut_int(yydoc, di.second.duration);
			yyjson_mut_obj_put(oneSlot, yyKey, yyVal);
			yyKey = yyjson_mut_strcpy(yydoc, "percentage");
			if (totalRangeTime == 0) {
				yyVal = yyjson_mut_real(yydoc, 0);
			}
			else {
				yyVal = yyjson_mut_real(yydoc, (double)di.second.duration / (double)totalRangeTime);
			}
			yyjson_mut_obj_put(oneSlot, yyKey, yyVal);
			yyjson_mut_arr_append(pAggrRlt, oneSlot);
		}
	}

	if (calc.boolDurations.size() > 0) {
		time_t totalRangeTime = 0;
		for (auto& di : calc.boolDurations) {
			totalRangeTime += di.second.duration;
		}
		for (auto& di : calc.boolDurations) {
			yyjson_mut_val* oneSlot = yyjson_mut_obj(yydoc);

			yyjson_mut_val* yyKey = yyjson_mut_strcpy(yydoc, "slot");
			yyjson_mut_val* yyVal = yyjson_mut_bool(yydoc, di.first);
			yyjson_mut_obj_put(oneSlot, yyKey, yyVal);

			yyKey = yyjson_mut_strcpy(yydoc, "duration");
			yyVal = yyjson_mut_int(yydoc, di.second.duration);
			yyjson_mut_obj_put(oneSlot, yyKey, yyVal);

			yyKey = yyjson_mut_strcpy(yydoc, "percentage");
			if (totalRangeTime == 0) {
				yyVal = yyjson_mut_real(yydoc, 0);
			}
			else {
				yyVal = yyjson_mut_real(yydoc, (double)di.second.duration / (double)totalRangeTime);
			}
			
			yyjson_mut_obj_put(oneSlot, yyKey, yyVal);

			yyjson_mut_arr_append(pAggrRlt, oneSlot);
		}
	}

	if (aggrKeyUndefined) {
		string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group size is " + DB_STR::format("%d", deGroup.size());
		db_exception e;
		e.m_error = sErr;
		throw e;
	}

	return;
}

double TDB::doAggrOneGroup_diff(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup) {
	double dbMax = -DBL_MAX;
	double dbMin = DBL_MAX;
	bool aggrKeyUndefined = true;
	for (int j = 0; j < deGroup.size(); j++) {
		yyjson_val* pDeSrc = deGroup.at(j);
		yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());
		if (pDeSrcVal == nullptr) {
			continue;
		}
		aggrKeyUndefined = false;
		double db = 0;
		if (yyjson_is_num(pDeSrcVal)) {
			db = yyjson_get_num(pDeSrcVal);
		}
		else if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR)
		{
			string valStr = yyjson_get_str(pDeSrcVal);
			db = atof(valStr.data());
		}
		else {
			continue;
		}
		if (db > dbMax)
			dbMax = db;
		if (db < dbMin)
			dbMin = db;
	}

	if (aggrKeyUndefined) {
		string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group size is " + DB_STR::format("%d", deGroup.size());
		db_exception e;
		e.m_error = sErr;
		throw e;
	}
	double dbDiff = dbMax - dbMin;
	string sDbDiff = formatStr("%lf", dbDiff);
	dbDiff = atof(sDbDiff.c_str());

	return dbDiff;
}

double TDB::doAggrOneGroup_avg(DE_SELECTOR& deSel, string& aggrKey,vector<yyjson_val*>& deGroup) {
	double dbTotal = 0;
	long long count = 0;

	bool aggrKeyUndefined = true;
	for (int j = 0; j < deGroup.size(); j++) {
		yyjson_val* pDeSrc = deGroup.at(j);
		yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());
		if (pDeSrcVal == nullptr) {
			continue;
		}
		aggrKeyUndefined = false;
		yyjson_mut_val* pAggrVal = nullptr;
		double db = 0;
		if (yyjson_is_num(pDeSrcVal)) {
			db = yyjson_get_num(pDeSrcVal);
		}
		else if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR)
		{
			string valStr = yyjson_get_str(pDeSrcVal);
			db = atof(valStr.data());
		}
		else {
			continue;
		}

		dbTotal += db;
		count++;
	}
	if (aggrKeyUndefined) {
		string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group size is " + DB_STR::format("%d", deGroup.size());
		db_exception e;
		e.m_error = sErr;
		throw e;
	}
	double avg = dbTotal / count;

	return avg;
}

bool TDB::doAggregateOneGroup(DE_SELECTOR& deSel, std::map<string,vector<string>> aggrKeyType,string groupKey,vector<yyjson_val*>& deGroup, DE_yyjson& aggrRlt, yyjson_mut_doc* mut_doc)
{
	//aggrKeyType supports 2 modes.   multi keys multi aggr types mode is not supported
	//1:  single key single aggr type
	//2:  multi keys single aggr type
	//3:  single key multi aggr types


	//which key to aggr
	//if val type is basic types,aggrKeyType.size()==1, aggr "val" key
	//if val type is json,  aggrKeyType has multiple items,each key corresponding to key of the json object. only support one level of json keys 
	for (auto& i : aggrKeyType) {
		string aggrKey = i.first;
		// aggr type for this key,
		// if only one aggr type is specified, "val" key holds the aggr result val
		// if multiple aggr type is specified ,use aggr type as key to hold the aggr result val
		vector<string>& aggrTypes = i.second;
		for (string& aggrType : aggrTypes) {
			yyjson_mut_val* pAggrVal = nullptr; //val after aggr
			if (aggrType == "first") {
				yyjson_val* pDeSrc = deGroup.at(0);
				yyjson_val* pDeSrcTime = yyjson_obj_get(pDeSrc, "time");
				yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());

				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR) //output val is specified
				{
					string valStr = yyjson_get_str(pDeSrcVal);
					pAggrVal = yyjson_mut_real(mut_doc, atof(valStr.data()));
				}
				else {
					pAggrVal = yyjson_val_mut_copy(mut_doc, pDeSrcVal);
				}

				aggrRlt.deTime = yyjson_get_str(pDeSrcTime);
				//des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
			}
			else if (aggrType == "last") {
				yyjson_val* pDeSrc = deGroup.at(deGroup.size() - 1);
				yyjson_val* pDeSrcTime = yyjson_obj_get(pDeSrc, "time");
				yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());

				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR)
				{
					string valStr = yyjson_get_str(pDeSrcVal);
					pAggrVal = yyjson_mut_real(mut_doc, atof(valStr.data()));
				}
				else {
					pAggrVal = yyjson_val_mut_copy(mut_doc, pDeSrcVal);
				}
				aggrRlt.deTime = yyjson_get_str(pDeSrcTime);
				//des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
			}
			else if (aggrType == "diff.first-last" || aggrType == "diff.last-first") {
				yyjson_val* pDeSrcFirst = deGroup.at(0);
				yyjson_val* pDeSrcTimeFisrt = yyjson_obj_get(pDeSrcFirst, "time");
				yyjson_val* pDeSrcValFirst = yyjson_obj_get_recursive(pDeSrcFirst, aggrKey.c_str());
				yyjson_val* pDeSrcLast = deGroup.at(deGroup.size() - 1);
				yyjson_val* pDeSrcTimeLast = yyjson_obj_get(pDeSrcLast, "time");
				yyjson_val* pDeSrcValLast = yyjson_obj_get_recursive(pDeSrcLast, aggrKey.c_str());

				double dbFirst, dbLast = 0;
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcValFirst) == YYJSON_TYPE_STR)
				{
					string valStr = yyjson_get_str(pDeSrcValFirst);
					dbFirst = atof(valStr.data());
				}
				else {
					dbFirst = yyjson_get_num(pDeSrcValFirst);
				}
				if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcValLast) == YYJSON_TYPE_STR)
				{
					string valStr = yyjson_get_str(pDeSrcValLast);
					dbLast = atof(valStr.data());
				}
				else {
					dbLast = yyjson_get_num(pDeSrcValLast);
				}

				double dbDiff = 0;
				if (aggrType == "diff.first-last") {
					dbDiff = dbFirst - dbLast;
				}
				else if (aggrType == "diff.last-first") {
					dbDiff = dbLast - dbFirst;
				}
				//double substraction caused loss of accuracy,use formatStr to fix this problem
				string sDbDiff = formatStr("%lf", dbDiff);
				dbDiff = atof(sDbDiff.c_str());
				pAggrVal = yyjson_mut_real(mut_doc, dbDiff);
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}
			else if (aggrType == "avg") {
				double avg = doAggrOneGroup_avg(deSel,aggrKey,deGroup);
				pAggrVal = yyjson_mut_real(mut_doc, avg);
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}
			else if (aggrType == "max") {
				double dbMax = -DBL_MAX;
				yyjson_val* pSelRowDeSrc = nullptr;

				bool aggrKeyUndefined = true;
				for (int j = 0; j < deGroup.size(); j++) {
					yyjson_val* pDeSrc = deGroup.at(j);
					yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());
					if (pDeSrcVal == nullptr) {
						continue;
					}
					aggrKeyUndefined = false;
					double db = 0;

					if(yyjson_is_num(pDeSrcVal)){
						db = yyjson_get_num(pDeSrcVal);
					}
					else if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR)
					{
						string valStr = yyjson_get_str(pDeSrcVal);
						db = atof(valStr.data());
					}
					else {
						continue;
					}
					if (db > dbMax) {
						pSelRowDeSrc = pDeSrc;
						dbMax = db;
					}
				}
				if (aggrKeyUndefined) {
					string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group size is " + DB_STR::format("%d", deGroup.size());
					db_exception e;
					e.m_error = sErr;
					throw e;
				}
				yyjson_val* pDeSrcTime = yyjson_obj_get(pSelRowDeSrc, "time");
				//des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
				aggrRlt.deTime = yyjson_get_str(pDeSrcTime);
				pAggrVal = yyjson_mut_real(mut_doc, dbMax);
			}
			else if (aggrType == "min") {
				double dbMin = DBL_MAX;
				yyjson_val* pSelRowDeSrc = nullptr;
				bool aggrKeyUndefined = true;
				for (int j = 0; j < deGroup.size(); j++) {
					yyjson_val* pDeSrc = deGroup.at(j);
					yyjson_val* pDeSrcVal = yyjson_obj_get_recursive(pDeSrc, aggrKey.c_str());
					if (pDeSrcVal == nullptr) {
						continue;
					}
					aggrKeyUndefined = false;
					double db = 0;

					if (yyjson_is_num(pDeSrcVal)) {
						db = yyjson_get_num(pDeSrcVal);
					}
					else if (deSel.isValTypeNumber() && yyjson_get_type(pDeSrcVal) == YYJSON_TYPE_STR)
					{
						string valStr = yyjson_get_str(pDeSrcVal);
						db = atof(valStr.data());
					}
					else {
						continue;
					}

					if (db < dbMin) {
						pSelRowDeSrc = pDeSrc;
						dbMin = db;
					}
				}
				if (aggrKeyUndefined) {
					string sErr = "aggr key " + aggrKey + " is undefined in every data element of aggr group,group size is " + DB_STR::format("%d", deGroup.size());
					db_exception e;
					e.m_error = sErr;
					throw e;
				}
				yyjson_val* pDeSrcTime = yyjson_obj_get(pSelRowDeSrc, "time");
				//des.time = yyjson_val_mut_copy(mut_doc, pDeSrcTime);
				aggrRlt.deTime = yyjson_get_str(pDeSrcTime);
				pAggrVal = yyjson_mut_real(mut_doc, dbMin);
			}
			else if (aggrType == "sum") {
				double dbSum = doAggrOneGroup_sum(deSel, aggrKey, deGroup);
				pAggrVal = yyjson_mut_real(mut_doc, dbSum);
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}
			else if (aggrType == "diff") {
				double dbDiff = doAggrOneGroup_diff(deSel, aggrKey, deGroup);
				pAggrVal = yyjson_mut_real(mut_doc, dbDiff);
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}
			else if (aggrType == "count") {
				int count = deGroup.size();
				pAggrVal = yyjson_mut_int(mut_doc, count);
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}
			else if (aggrType == "increase") {
				map<string, double> aggrRltTs = doAggrOneGroup_increase_withTimeSlots(deSel, aggrKey,groupKey, deGroup);
				pAggrVal = yyjson_mut_obj(mut_doc);
				for (auto& iter : aggrRltTs) {
					yyjson_mut_val* yySlotName = yyjson_mut_strcpy(mut_doc, iter.first.c_str());
					yyjson_mut_val* yySlotIncrease = yyjson_mut_real(mut_doc, iter.second);
					yyjson_mut_obj_put(pAggrVal, yySlotName, yySlotIncrease);
				}
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}
			else if (aggrType == "duration") {
				doAggrOneGroup_duration(deSel, aggrKey, deGroup, pAggrVal, mut_doc);
				aggrRlt.deTime = deSel.timeSel.atomSelList[0].selector;
			}


			if (aggrType == "count") {
				aggrRlt.items["count"] = pAggrVal;
			}

			
			if (aggrKeyType.size() == 1 && aggrKey == m_dbFmt.deItemKey_value) {
				// single key single type mode
				if (aggrTypes.size() == 1) {
					aggrRlt.val = pAggrVal;
					aggrRlt.items[aggrKey] = pAggrVal;
				}
				//single key multi type mode
				else {
					aggrRlt.items[aggrType] = pAggrVal;
				}
			}
			//multi key single type mode
			else {
				aggrRlt.items[aggrKey] = pAggrVal;
			}
		}
	}

	return true;
}

bool TDB::Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc)
{
	map<SORT_FLAG, yyjson_mut_val*>& mapRlt = result.rltDataSet;

	
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		DATA_SET& fSet = *tagDBFileSet[tagIdx];

		//aggr result of one tag. 
		for (int j = 0; j < fSet.m_afterAggr.size(); j++) {
			DE_yyjson& deyy = *fSet.m_afterAggr[j];

			//check if data row of this time is already exist
			yyjson_mut_val* jRecord;
			SORT_FLAG sf;
			sf.sFlag = deyy.deTime.data();
			map<SORT_FLAG, yyjson_mut_val*>::iterator itRecord = mapRlt.find(sf);
			if (itRecord != mapRlt.end()) { //get this row if exist
				jRecord = itRecord->second;
			}
			else  //create row if not exist
			{
				jRecord = yyjson_mut_obj(mut_doc);
				//time
				yyjson_mut_val* timeKey = yyjson_mut_str(mut_doc, CONST_STR::time.c_str());
				yyjson_mut_val* timeVal = yyjson_mut_str(mut_doc, deyy.deTime.data());
				yyjson_mut_obj_put(jRecord, timeKey, timeVal);
				//tag
				for (int i = 0; i < tagDBFileSet.size(); i++) {
					yyjson_mut_val* tagColKeyInit = yyjson_mut_str(mut_doc, tagDBFileSet[i]->colKey.c_str());
					yyjson_mut_val* tagColValInit = yyjson_mut_null(mut_doc);
					bool putted = yyjson_mut_obj_put(jRecord, tagColKeyInit, tagColValInit);
					if (!putted)
					{
						printf("can not insert key to yyjson obj");
					}
				}

				std::pair<SORT_FLAG, yyjson_mut_val*> recPair;
				SORT_FLAG sftmp;
				sftmp.sFlag = deyy.deTime;
				recPair.first = sftmp;
				recPair.second = jRecord;
				auto insertRet = mapRlt.insert(recPair);
				itRecord = insertRet.first;
				mapRlt[sftmp] = jRecord; 
			}

			//set colume data of this row, colume name is fSet.colKey,colume data is de value
			yyjson_mut_val* tagColKey = yyjson_mut_str(mut_doc, fSet.colKey.c_str());
			yyjson_mut_obj_put(jRecord, tagColKey, deyy.val);

			
			result.rowCount++;
		}
	}


	if (deSel.timeFill) {
		//time section Fill
		yyjson_mut_val* lastRec = nullptr;
		yyjson_mut_val* curRec = nullptr;
		for (auto& rec : mapRlt) {
			curRec = rec.second;
			if (curRec && lastRec) {
				size_t idx, max;
				yyjson_mut_val* key, * val;
				yyjson_mut_obj_foreach(curRec, idx, max, key, val) {
					if (yyjson_mut_is_null(val)) {
						string szKey = yyjson_mut_get_str(key);
						yyjson_mut_val* lastVal = yyjson_mut_obj_get(lastRec, szKey.data());
						yyjson_mut_val* curKey = yyjson_mut_val_mut_copy(mut_doc, key);
						yyjson_mut_val* curVal = yyjson_mut_val_mut_copy(mut_doc, lastVal);
						yyjson_mut_obj_put(curRec, curKey, curVal);
					}
				}
			}
			lastRec = curRec;
		}
	}

	return true;
}

void TDB::rpc_db_select(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language)
{
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	rpc_db_select(yyv_params, rlt, err, queryInfo, org,language);
	yyjson_doc_free(doc);
}

void TDB::rpc_db_select(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language) {
	DE_SELECTOR deSel;

	string dbName;
	TDB* tdb = nullptr;
	yyjson_val* yyv_db = yyjson_obj_get(params, "db");
	if (yyjson_is_str(yyv_db)) {
		dbName = yyjson_get_str(yyv_db);
		tdb = db.getChildDB(dbName);
		if (tdb == nullptr) {
			err = JSON_STR_VAL("specified db not found");
			return;
		}
	}

	string rootTag = "";
	yyjson_val* yyv_rootTag = yyjson_obj_get(params, "rootTag");
	if (yyv_rootTag) {
		rootTag = yyjson_get_str(yyv_rootTag);
		if (m_isGbk) {
			rootTag = DB_STR::gb_to_utf8(rootTag);
		}
	}

	deSel.tagSel.m_org = org;
	deSel.tagSel.selLanguage = language;
	deSel.tagSel.rltLanguage = m_dbFmt.language;  //get tags in specified language for further db operation
	parseDESelector(params, deSel, err);

	if (err != "") {
		err = JSON_STR_VAL(err);
		return;
	}

	yyjson_val* yyv_calc = yyjson_obj_get(params, "calc");
	if (yyv_calc) {
		if (yyjson_is_str(yyv_calc)) {
			deSel.calc = yyjson_get_str(yyv_calc);
		}
		else if (yyjson_is_obj(yyv_calc)) {
			yyjson_val* yyv_calc_alg = yyjson_obj_get(yyv_calc, "alg");
			if (yyv_calc_alg) {
				deSel.calc = yyjson_get_str(yyv_calc_alg);
			}

			yyjson_val* yyv_calc_baseCurve = yyjson_obj_get(yyv_calc, "baseCurve");
			if (yyv_calc_baseCurve) {
				deSel.baseCurve = yyjson_get_str(yyv_calc_baseCurve);
			}

			yyjson_val* yyv_calc_aggr = yyjson_obj_get(yyv_calc, "aggr");
			if (yyv_calc_aggr) {
				deSel.curvePtAggr = yyjson_get_str(yyv_calc_aggr);
			}
		}
	}

	yyjson_val* yyv_timeFill = yyjson_obj_get(params, "timeFill");
	if (yyv_timeFill && yyjson_is_bool(yyv_timeFill)) {
		deSel.timeFill = yyjson_get_bool(yyv_timeFill);
	}

	//check if need tag parse
	bool needParseTag = false;
	if (deSel.tagSel.fuzzyMatchExp.size() != 0) {
		needParseTag = true; //parse fuzzy tag exp to exact tags
	}
	else if (deSel.tagSel.selLanguage!="" && deSel.tagSel.selLanguage != m_dbFmt.language) {
		needParseTag = true; //parse tags in sel language to db language
	}

	if (needParseTag)
		m_getTagsByTagSelector(deSel.tagSel.tagSet, deSel.tagSel);
	else {
		for (int i = 0; i < deSel.tagSel.exactMatchExp.size(); i++) {
			string& exp = deSel.tagSel.exactMatchExp[i];
			deSel.tagSel.tagSet.push_back(exp);
			deSel.tagSel.dbFileTagSet.push_back(exp);
		}
	}

	yyjson_val* yyv_selfParams = yyjson_obj_get(params, "self_params");
	if (yyv_selfParams) {
		if (yyjson_is_obj(yyv_selfParams)) {
			if (yyjson_obj_get(yyv_selfParams, "n")) {
				deSel.theLimit = yyjson_get_int(yyjson_obj_get(yyv_selfParams, "n"));
			}

			if (yyjson_obj_get(yyv_selfParams, "interval")) {
				deSel.self_interval = yyjson_get_int(yyjson_obj_get(yyv_selfParams, "interval"));
			}
		}
	}

	auto oldTimeUint = m_timeUnit;

	yyjson_val* yyv_timeUint = yyjson_obj_get(params, "timeUint");
	if (yyv_timeUint && yyjson_is_str(yyv_timeUint)) {
		string timeUnit = yyjson_get_str(yyv_timeUint);
		if (timeUnit == "month") m_timeUnit = BY_MONTH;
		else if (timeUnit == "year") m_timeUnit = BY_YEAR;
		else if (timeUnit == "day") m_timeUnit = BY_DAY;
		else if (timeUnit == "none") m_timeUnit = NONE;
	}

	SELECT_RLT result;
	if (deSel.tagSel.tagSet.size() == 0) {
		err = JSON_STR_VAL("specified tag not found");
	}
	else {
		try {
			if (dbName != "") {
				tdb->Select(deSel, result);
			}
			else {
				Select(deSel, result);
			}

			if (result.error != "") {
				err = result.error;
			}
			else {
				if (deSel.calc != "") {
					rlt = result.calcResult;
				}
				else {
					rlt = result.dataList;
				}
			}
		}
		catch (std::exception& e) {
			string sErr = e.what();
			err = JSON_STR_VAL(sErr);
		}
	}

	m_timeUnit = oldTimeUint;

	queryInfo = result.info;
	queryInfo = "tags:" + DB_STR::format("%d", deSel.tagSel.tagSet.size()) + ",files:" + DB_STR::format("%d", result.fileCount) + ",data elements:" + DB_STR::format("%d", result.deCount) + ",rows:" + DB_STR::format("%d", result.rowCount);
}

bool TDB::Select(DE_SELECTOR& deSel, SELECT_RLT& result) {
	//deType is curve but time sel is range,do a curveIdx select to get curve time points before curve select
	if (deSel.timeSel.isRange() && deSel.deType == "curve") {
		DE_SELECTOR deSelIdx = deSel;
		deSelIdx.deType = "curveIdx";

		SELECT_RLT idxRlt;
		Select(deSelIdx, idxRlt);

		deSel.timeSel.atomSelList.clear();

		//get all different time point
		map<string, string> timePointList;
		for (auto& iter : idxRlt.rltDataSet) {
			yyjson_mut_val* yyv_time = yyjson_mut_obj_get(iter.second, "time");

			string time = yyjson_mut_get_str(yyv_time);
			timePointList[time] = time;
		}

		//generate time atom selector
		for (auto& timePoint : timePointList) {
			TIME_SELECTOR_ATOM tsa;
			tsa.init(timePoint.first);
			deSel.timeSel.atomSelList.push_back(tsa);
		}
	}

	bool bRet = true;
	vector<string> tagSet = deSel.tagSel.tagSet;

	//load file data
	vector<TAG_FILE_SET*>& tagFileSet = result.tagFileSet;
	for (int i = 0; i < tagSet.size(); i++) {
		TAG_FILE_SET& fSet = *(new TAG_FILE_SET());
		fSet.tag = deSel.tagSel.tagSet[i];
		fSet.dbFileTag = deSel.tagSel.dbFileTagSet[i];

		tagFileSet.push_back(&fSet);
	}

	Select_Step_loadFile(deSel, tagFileSet, result);

	//data buff in processing steps, all will be released in the end
	vector<vector<DATA_SET*>*>&  dataSetBuff = result.dataSetBuff;

	map<SORT_FLAG, yyjson_mut_val*>* pCalcResult = nullptr;
	string sCalcResult; //calc result dumped to string

	yyjson_mut_doc* rlt_mut_doc = result.rlt_mut_doc;

	if (deSel.deType == "curve") {
		size_t sortIdx = 0;
		for (int tagIdx = 0; tagIdx < tagFileSet.size(); tagIdx++) {
			TAG_FILE_SET& fSet = *tagFileSet[tagIdx];
			string& tag = fSet.tag; // yyjson do not copy string,src string can not be release,use string& instead of a local variant
			
			for (int i = 0; i < fSet.fileList.size(); i++) {
				DB_FILE* pdf = fSet.fileList[i];

				SORT_FLAG sf;
				sf.dbFlag = sortIdx++;

				auto p = yyjson_mut_obj(rlt_mut_doc);
				yyjson_mut_obj_add_strcpy(rlt_mut_doc, p, "tag", pdf->tag.c_str());
				yyjson_mut_obj_add_strcpy(rlt_mut_doc,p,"time",(pdf->time.toStr(false)).c_str());
				yyjson_mut_obj_add_val(rlt_mut_doc, p, "curve", yyjson_val_mut_copy(rlt_mut_doc, pdf->root));

				result.rltDataSet[sf] = p;
			}
		}
	}
	else {
		vector<DATA_SET*>* set_list;  //current processing dataset ,stores current processed result

		//orgin data set    dataSet1 
		vector<DATA_SET*>* dataSet1 = new vector<DATA_SET*>;

		set_list = dataSet1;
		dataSetBuff.push_back(dataSet1);//add to buff list when create a new dataset,will be released in the end

		for (int i = 0; i < tagSet.size(); i++) {
			//get relTag
			DATA_SET& fSet = *(new DATA_SET());
			fSet.tag = tagSet[i];
			fSet.relTag = DB_TAG::trimRoot(tagSet[i], deSel.tagSel.m_rootTag);

			//generate tag name
			if (deSel.vecTagLable.size() == tagSet.size()) {
				fSet.colKey = deSel.vecTagLable[i];
			}
			else {
				if (deSel.tagLabel == "tag") {
					fSet.colKey = fSet.relTag;
				}
				else {
					size_t pos = fSet.relTag.rfind(".");
					if (pos != string::npos) {
						fSet.mpName = fSet.relTag.substr(pos + 1, fSet.relTag.size() - pos - 1);
					}
					else {
						fSet.mpName = fSet.relTag;
					}

					fSet.colKey = fSet.mpName;
				}
			}

			//query param of this tag
			if (deSel.vecAggregate.size() == tagSet.size()) { //muti tag aggr mode
				fSet.aggregate = deSel.vecAggregate[i];
			}
			else if (deSel.aggregate.size() > 0) {//single tag aggr mode
				fSet.aggregate = deSel.aggregate;
			}

			set_list->push_back(&fSet);
		}

		//get seleted de, parse files in to de dataset
		bRet = Select_Step_loadDataElem(deSel, tagFileSet, *set_list, result, rlt_mut_doc);
		if (!bRet) {
			return false;
		}

		//do when selector after all de is selected
		if (deSel.whenSel.tag != "") {
			vector<DATA_SET*>* dataSet_afterWhen = new vector<DATA_SET*>;
			DATA_SET& fSet = *(new DATA_SET());

			dataSet_afterWhen->push_back(&fSet);
			dataSetBuff.push_back(dataSet_afterWhen);

			Select_Step_FilterByRelation(deSel, *set_list, *dataSet_afterWhen);
		}

		//if not groupby tag, merge multiple dataset into one data set  (groupby tag is the default behavior)
		vector<DATA_SET*>* in_set_list = set_list;
		vector<DATA_SET*>* out_set_list = nullptr;

		if (!deSel.groupByTag) {
			DATA_SET& out_set = *(new DATA_SET());
			out_set.tag = "*";
			out_set.aggregate = deSel.aggregate;  //not groupby tag is like single tag aggr,treat all tag as one tag

			if (deSel.groupByTime) {
				for (int i = 0; i < in_set_list->size(); i++) {
					DATA_SET& in_set = *in_set_list->at(i);

					//find out time group of all tag,and merge them
					for (auto& g : in_set.m_origDeGrouped) {
						if (out_set.m_origDeGrouped.find(g.first) != out_set.m_origDeGrouped.end()) {
							vector<yyjson_val*>& vec = out_set.m_origDeGrouped[g.first];
							vec.insert(vec.begin(), g.second.begin(), g.second.end());
						}
						else {
							out_set.m_origDeGrouped[g.first] = g.second;
						}
					}
				}
			}
			else {
				for (int i = 0; i < in_set_list->size(); i++) {
					DATA_SET& in_set = *in_set_list->at(i);

					out_set.m_orgDe.insert(out_set.m_orgDe.end(), in_set.m_orgDe.begin(), in_set.m_orgDe.end());
				}
			}

			out_set_list = new vector<DATA_SET*>;
			out_set_list->push_back(&out_set);
			dataSetBuff.push_back(out_set_list);
		}
		else {
			out_set_list = in_set_list;
		}

		set_list = out_set_list; //use out_set_list as current data set, will be used in next steps

		//do aggr
		Select_Step_doAggregate(deSel, *set_list, rlt_mut_doc);

		//output result rows
		if (deSel.tagAsColume) {
			Select_Step_outputRows_MultiCol(deSel, *set_list, result, rlt_mut_doc);
		}
		else {
			if (deSel.splitBy == "tag") {
				for (int i = 0; i < set_list->size(); i++)
				{
					DATA_SET* pSet = set_list->at(i);
					vector<DATA_SET*> splitted_set_list;
					splitted_set_list.push_back(pSet);
					vector<yyjson_mut_val*> tmp;
					result.rltDataSetVecList[pSet->tag] = tmp;
					vector<yyjson_mut_val*>& rltDataSet = result.rltDataSetVecList[pSet->tag];
					Select_Step_outputRows_SingleCol(deSel, splitted_set_list, rltDataSet,result,rlt_mut_doc);
				}
			}
			else {
				if(deSel.bAggr || deSel.tagSel.getTag){
					Select_Step_outputRows_SingleCol_timeFill(deSel, *set_list, result.rltDataSet, result, rlt_mut_doc);
				}
				else {
					Select_Step_outputRows_SingleCol(deSel, *set_list, result.rltDataSetVec, result, rlt_mut_doc);
				}
			}
		}

		//limit
		if (deSel.limit > 0) {
			map<SORT_FLAG, yyjson_mut_val*> temp_mapRlt;

			int i = 0;
			for (auto& it : result.rltDataSet) {
				if (i >= deSel.offset && i < deSel.offset + deSel.limit) {
					temp_mapRlt.insert(it);
					i++;

					continue;
				}
				else if (i< deSel.offset) {
					i++;
					continue;
				}
				else if (i>= deSel.offset + deSel.limit) {
					break;
				}
			}

			result.rltDataSet.swap(temp_mapRlt);
		}
 
		if (deSel.deType == "curveIdx" && deSel.theLimit > 0) {
			int max = result.rltDataSet.size();
			int idx = 0;

			map<SORT_FLAG, yyjson_mut_val*> mapRlt0;
			if (deSel.self_interval > 0) {
				for (auto it = result.rltDataSet.begin(); it != result.rltDataSet.end(); it++, idx++) {
					//"No this param" 、interval=1 is the same thing
					if (deSel.self_interval > 1) {
						bool reachInterval = idx % deSel.self_interval == 0;
						if (!reachInterval) {
							continue;
						}
					}

					if (deSel.self_interval > max) {
						continue;
					}

					mapRlt0[it->first] = it->second;
				}
			}

			result.rltDataSet.swap(mapRlt0);

			map<SORT_FLAG, yyjson_mut_val*> mapRlt1;
			for (auto& i : result.rltDataSet) {
				yyjson_mut_val* yyv_curve_de = i.second;

				yyjson_mut_val* yyv_time = yyjson_mut_obj_get(yyv_curve_de, "time");
				string time = yyjson_mut_get_str(yyv_time);

				yyjson_mut_val* yyv_tag = yyjson_mut_obj_get(yyv_curve_de, "tag");
				string tag = yyjson_mut_get_str(yyv_tag);

				bool bDropIt = false;

				yyjson_mut_val* data_attr = yyjson_mut_obj_get(yyv_curve_de, "data_attr");
				size_t size = yyjson_mut_arr_size(data_attr);

				for (int k = size - 1; k > 0; k--) {
					auto element = yyjson_mut_arr_get(data_attr, k);
					size_t size1 = yyjson_mut_arr_size(element);

					if (size1 != 3) {
						continue;
					}

					auto name = yyjson_mut_arr_get(element, 0);
					string strName = yyjson_mut_get_str(name);

					if (strName == "lastDi") {
						auto val = yyjson_mut_arr_get(element, 2);
						string strVal = yyjson_mut_get_str(val);

						float fVal = atof(strVal.c_str());
						if (fVal >= deSel.theLimit) {
							bDropIt = true;
							break;
						}
					}
				}

				if (!bDropIt) {
					mapRlt1[i.first] = i.second;
				}
			}

			result.rowCount = mapRlt1.size();
			result.deCount = mapRlt1.size();

			result.rltDataSet.swap(mapRlt1);
		}

		//calc
		if (deSel.calc == "diff") {
			int idx = 0;
			yyjson_mut_val* lastVal;
			yyjson_mut_val* curVal;
			double dbLast;
			double dbCur;
			for (auto& i : result.rltDataSet)
			{
				curVal = yyjson_mut_obj_get(i.second, m_dbFmt.deItemKey_value.c_str());
				if (!yyjson_mut_is_num(curVal)) {
					break;
				}


				dbCur = yyjson_mut_get_real(curVal);

				if (idx > 0) {
					double diff = dbCur - dbLast;
					yyjson_mut_set_real(curVal, diff);
				}
				idx++;
				dbLast = dbCur;
				lastVal = curVal;
			}
			if (result.rltDataSet.size() > 0)
				result.rltDataSet.erase(result.rltDataSet.begin());
			pCalcResult = &result.rltDataSet;
		}
		else if (deSel.calc == "sum") {
			yyjson_mut_val* curVal;
			double dbSum = 0;
			double dbCur;
			for (auto& i : result.rltDataSet)
			{
				curVal = yyjson_mut_obj_get(i.second, m_dbFmt.deItemKey_value.c_str());
				if (!yyjson_mut_is_num(curVal)) {
					break;
				}
				dbCur = yyjson_mut_get_real(curVal);
				dbSum += dbCur;
			}
			sCalcResult = formatStr("%f", dbSum);
		}
		else if (deSel.deType == "curveIdx" && deSel.calc != "") { //Absolute Error Sum
			if (deSel.calc == "curvePtAggr") {
				std::vector<std::string> aggrList;
				std::string src = deSel.curvePtAggr;
				std::string separator = ",";
				std::string temp;
				size_t pos = 0, offset = 0;

				// 分割第1~n-1个
				while ((pos = src.find(separator, offset)) != std::string::npos)
				{
					temp = src.substr(offset, pos - offset);
					if (temp.length() > 0) {
						aggrList.push_back(temp);
					}
					else
					{
						aggrList.push_back("");
					}
					offset = pos + separator.size();
				}

				// 分割第n个
				temp = src.substr(offset, src.length() - offset);
				if (temp.length() > 0) {
					aggrList.push_back(temp);
				}

				if (aggrList.size() > 0)
				{
					double sortIdx = 0;
					for (auto& i : result.rltDataSet)
					{
						yyjson_mut_val* yyv_curve_de = i.second;
						yyjson_mut_val* yyv_time = yyjson_mut_obj_get(yyv_curve_de, "time");
						string time = yyjson_mut_get_str(yyv_time);
						yyjson_mut_val* yyv_tag = yyjson_mut_obj_get(yyv_curve_de, "tag");
						string tag = yyjson_mut_get_str(yyv_tag);
						DB_TIME dbtime;
						dbtime.fromStr(time);
						DB_FILE dbfile(dbtime, tag, this);
						dbfile.deType = "curve";
						if (dbfile.loadFile())
						{
							yyjson_val* yyv_curve = dbfile.root;
							yyjson_val* yyv_pt_list = yyjson_obj_get(yyv_curve, "data");
							size_t idx = 0;
							size_t count = 0;
							yyjson_val* item;
							double min = DBL_MAX;
							double max = -DBL_MAX;
							double first = 0;
							double last = 0;
							yyjson_arr_foreach(yyv_pt_list, idx, count, item) {
								int temp;
								if (yyjson_is_int(item))
									temp = (double)yyjson_get_int(item);
								else if (yyjson_is_real(item))
									temp = (double)yyjson_get_real(item);
								else if (yyjson_is_str(item))
									temp = (double)atof(yyjson_get_str(item));

								if (idx == 0) first = temp;
								if (idx == count-1) last = temp;
								if (temp < min) min = temp;
								if (temp > max)max = temp;
							}

							yyjson_mut_val* curvePtAggr_obj = yyjson_mut_obj(rlt_mut_doc);
							yyjson_mut_obj_add_val(rlt_mut_doc, yyv_curve_de, "curvePtAggr", curvePtAggr_obj);

							for (int j = 0; j < aggrList.size(); j++)
							{
								string aggr = aggrList[j];
								if (aggr == "first")
								{
									yyjson_mut_obj_add_int(rlt_mut_doc, curvePtAggr_obj, "first", (int)first);
								}
								else if (aggr == "last")
								{
									yyjson_mut_obj_add_int(rlt_mut_doc, curvePtAggr_obj, "last", (int)last);
								}
								else if (aggr == "min")
								{
									yyjson_mut_obj_add_int(rlt_mut_doc, curvePtAggr_obj, "min", (int)min);
								}
								else if (aggr == "max")
								{
									yyjson_mut_obj_add_int(rlt_mut_doc, curvePtAggr_obj, "max", (int)max);
								}
								else if (aggr == "diff")
								{
									yyjson_mut_obj_add_int(rlt_mut_doc, curvePtAggr_obj, "diff", (int)(max - min));
								}
							}

						}
					}
				}

				pCalcResult = &result.rltDataSet;
			}
			else {
				map<DB_TIME, vector<double>*> curveList;
				vector<double> refCurvePt;
				vector<double> specifyCurvePt;
				string tag;
				for (auto& i : result.rltDataSet)
				{
					yyjson_mut_val* yyv_curve_de = i.second;
					yyjson_mut_val* yyv_time = yyjson_mut_obj_get(yyv_curve_de, "time");
					string time = yyjson_mut_get_str(yyv_time);
					yyjson_mut_val* yyv_tag = yyjson_mut_obj_get(yyv_curve_de, "tag");
					tag = yyjson_mut_get_str(yyv_tag);
					vector<double>* pPtList = new vector<double>();
					DB_TIME dbtime;
					dbtime.fromStr(time);
					DB_FILE dbfile(dbtime, tag, this);
					dbfile.deType = "curve";
					if (dbfile.loadFile()) {
						yyjson_val* yyv_curve = dbfile.root;
						yyjson_val* yyv_pt_list = yyjson_obj_get(yyv_curve, "data");
						size_t idx = 0;
						size_t max = 0;
						yyjson_val* item;
						yyjson_arr_foreach(yyv_pt_list, idx, max, item) {
							if (yyjson_is_obj(item)) {
								yyjson_val* yyv_y = yyjson_obj_get(item, "y");
								double db;
								if (yyjson_is_int(yyv_y)) {
									int ival = yyjson_get_int(yyv_y);
									db = ival;
								}
								else
									db = yyjson_get_real(yyv_y);
								pPtList->push_back(db);
							}
							else if (yyjson_is_arr(item)) {
								size_t size = yyjson_arr_size(item);
								if (size == 2) {
									yyjson_val* yyv_y = yyjson_arr_get(item, 1);

									double db;
									if (yyjson_is_int(yyv_y)) {
										int ival = yyjson_get_int(yyv_y);
										db = ival;
									}
									else
										db = yyjson_get_real(yyv_y);
									pPtList->push_back(db);
								}
							}
						}
						curveList[dbfile.time] = pPtList;
					}
				}

				result.rltDataSet.clear();
				vector<double>* pBase = nullptr;
				if (deSel.baseCurve == "refCurve") {
					string path = m_confPath + "/refCurve/";
					string subPath = DB_STR::replace(tag, ".", "/");
					path += subPath + "/refCurve.json";
					string s;
					DB_FS::readFile(path, s);
					if (s == "") {
						db_exception dbe;
						dbe.m_error = "refCurve not found";
						throw dbe;
					}

					yyjson_doc* doc = yyjson_read(s.data(), s.size(), 0);
					if (!doc) {
						s = DB_STR::gb_to_utf8(s);
						doc = yyjson_read(s.data(), s.size(), 0);
					}

					if (!doc) {
						db_exception dbe;
						dbe.m_error = "refCurve not found";
						throw dbe;
					}

					yyjson_val* yyv_curve = yyjson_doc_get_root(doc);
					if (!yyv_curve) {
						db_exception dbe;
						dbe.m_error = "refCurve not found";
						throw dbe;
					}

					yyjson_val* yyv_pt_list = yyjson_obj_get(yyv_curve, "data");
					size_t idx = 0;
					size_t max = 0;
					yyjson_val* item;
					yyjson_arr_foreach(yyv_pt_list, idx, max, item) {
						if (yyjson_is_obj(item)) {
							yyjson_val* yyv_y = yyjson_obj_get(item, "y");
							double db;
							if (yyjson_is_int(yyv_y)) {
								int ival = yyjson_get_int(yyv_y);
								db = ival;
							}
							else
								db = yyjson_get_real(yyv_y);
							refCurvePt.push_back(db);
						}
						else if (yyjson_is_arr(item)) {
							size_t size = yyjson_arr_size(item);
							if (size == 2) {
								yyjson_val* yyv_y = yyjson_arr_get(item, 1);

								double db;
								if (yyjson_is_int(yyv_y)) {
									int ival = yyjson_get_int(yyv_y);
									db = ival;
								}
								else
									db = yyjson_get_real(yyv_y);
								refCurvePt.push_back(db);
							}
						}
					}

					pBase = &refCurvePt;

					yyjson_doc_free(doc);
				}
				else if (DB_STR::isTime(deSel.baseCurve)) {
					string path = getPath_dbFile(tag, deSel.baseCurve, "curve");
					string s;
					DB_FS::readFile(path, s);
					yyjson_doc* doc = yyjson_read(s.data(), s.size(), 0);
					if (!doc) {
						s = DB_STR::gb_to_utf8(s);
						doc = yyjson_read(s.data(), s.size(), 0);
					}

					if (!doc) {
						db_exception dbe;
						dbe.m_error = "specified curve not found";
						throw dbe;
					}

					yyjson_val* yyv_curve = yyjson_doc_get_root(doc);
					if (!yyv_curve) {
						db_exception dbe;
						dbe.m_error = "specified curve not found";
						throw dbe;
					}

					yyjson_val* yyv_pt_list = yyjson_obj_get(yyv_curve, "data");
					size_t idx = 0;
					size_t max = 0;
					yyjson_val* item;
					yyjson_arr_foreach(yyv_pt_list, idx, max, item) {
						if (yyjson_is_obj(item)) {
							yyjson_val* yyv_y = yyjson_obj_get(item, "y");
							double db;
							if (yyjson_is_int(yyv_y)) {
								int ival = yyjson_get_int(yyv_y);
								db = ival;
							}
							else
								db = yyjson_get_real(yyv_y);
							specifyCurvePt.push_back(db);
						}
						else if (yyjson_is_arr(item)) {
							size_t size = yyjson_arr_size(item);
							if (size == 2) {
								yyjson_val* yyv_y = yyjson_arr_get(item, 1);

								double db;
								if (yyjson_is_int(yyv_y)) {
									int ival = yyjson_get_int(yyv_y);
									db = ival;
								}
								else
									db = yyjson_get_real(yyv_y);
								specifyCurvePt.push_back(db);
							}
						}
					}

					pBase = &specifyCurvePt;

					yyjson_doc_free(doc);
				}

				vector<double>* pCur = nullptr;
				size_t sortIdx = 0;
				for (auto& iter : curveList) {
					pCur = iter.second;

					if (pCur && pBase && pBase->size() > 0) {
						if (deSel.calc == "aes" || deSel.calc == "aea") {
							double aes = 0;
							int cnt = 0;
							for (int i = 0; i < pBase->size() && i < pCur->size(); i++) {
								aes += abs((*pBase)[i] - (*pCur)[i]);
								cnt++;
							}
							if (cnt > 0 && deSel.calc == "aea")
								aes /= cnt;

							yyjson_mut_val* yyv_aes_de = yyjson_mut_obj(rlt_mut_doc);
							yyjson_mut_val* yyv_time_key = yyjson_mut_strcpy(rlt_mut_doc, "time");
							yyjson_mut_val* yyv_time_val = yyjson_mut_strcpy(rlt_mut_doc, iter.first.toStr().c_str());
							yyjson_mut_obj_put(yyv_aes_de, yyv_time_key, yyv_time_val);

							yyjson_mut_val* yyv_val_key = yyjson_mut_strcpy(rlt_mut_doc, "val");
							yyjson_mut_val* yyv_val_val = yyjson_mut_real(rlt_mut_doc, aes);
							yyjson_mut_obj_put(yyv_aes_de, yyv_val_key, yyv_val_val);

							SORT_FLAG sf;
							sf.dbFlag = sortIdx++;
							result.rltDataSet[sf] = yyv_aes_de;
						}
						else if (deSel.calc == "dtw") {
							if (pBase->size() != pCur->size()) {
								DBLog("dtw calc use different length,%d,%d\r\n", pBase->size(), pCur->size());
							}

							size_t len = pBase->size() < pCur->size() ? pBase->size() : pCur->size();
							double dtw = DTWDistanceFun(pCur->data(), pCur->size(), pBase->data(), pBase->size(), pBase->size() / 10);

							yyjson_mut_val* yyv_dtw_de = yyjson_mut_obj(rlt_mut_doc);
							yyjson_mut_val* yyv_time_key = yyjson_mut_strcpy(rlt_mut_doc, "time");
							yyjson_mut_val* yyv_time_val = yyjson_mut_strcpy(rlt_mut_doc, iter.first.toStr().c_str());
							yyjson_mut_obj_put(yyv_dtw_de, yyv_time_key, yyv_time_val);

							yyjson_mut_val* yyv_val_key = yyjson_mut_strcpy(rlt_mut_doc, "val");
							yyjson_mut_val* yyv_val_val = yyjson_mut_real(rlt_mut_doc, dtw);
							yyjson_mut_obj_put(yyv_dtw_de, yyv_val_key, yyv_val_val);

							SORT_FLAG sf;
							sf.dbFlag = sortIdx++;
							result.rltDataSet[sf] = yyv_dtw_de;
						}
						else if (deSel.calc == "dtw2") {
							vector<vector<double>> base;
							for (int i = 0; i < pBase->size() && i < pCur->size(); i++) {
								vector<double> pt;
								pt.push_back((*pBase)[i]);
								base.push_back(pt);
							}
							vector<vector<double>> cur;
							for (int i = 0; i < pBase->size() && i < pCur->size(); i++) {
								vector<double> pt;
								pt.push_back((*pCur)[i]);
								cur.push_back(pt);
							}
							double dtw = DTW::dtw_distance_only(base, cur, 2);

							yyjson_mut_val* yyv_dtw_de = yyjson_mut_obj(rlt_mut_doc);
							yyjson_mut_val* yyv_time_key = yyjson_mut_strcpy(rlt_mut_doc, "time");
							yyjson_mut_val* yyv_time_val = yyjson_mut_strcpy(rlt_mut_doc, iter.first.toStr().c_str());
							yyjson_mut_obj_put(yyv_dtw_de, yyv_time_key, yyv_time_val);

							yyjson_mut_val* yyv_val_key = yyjson_mut_strcpy(rlt_mut_doc, "val");
							yyjson_mut_val* yyv_val_val = yyjson_mut_real(rlt_mut_doc, dtw);
							yyjson_mut_obj_put(yyv_dtw_de, yyv_val_key, yyv_val_val);

							SORT_FLAG sf;
							sf.dbFlag = sortIdx++;
							result.rltDataSet[sf] = yyv_dtw_de;
						}
					}

					if (deSel.baseCurve == "previous") {
						pBase = pCur;
					}
				}

				for (auto& i : curveList) {
					delete i.second;
				}
				pCalcResult = &result.rltDataSet;
			}
		}
	}

	//use new yyjson doc to output. merge data of multi tag,multi time range into a json result
	if (result.rltDataSetList.size()>0) {
		yyjson_mut_val* rlt_mut_root = yyjson_mut_obj(rlt_mut_doc);
		yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);
		for (auto& iter : result.rltDataSetList) {
			map<SORT_FLAG, yyjson_mut_val*>& mapRlt = iter.second;
			yyjson_mut_val* rlt_mut_data_set = yyjson_mut_arr(rlt_mut_doc);
			for (auto& i : mapRlt)
			{
				if (deSel.ascendingSort)
					yyjson_mut_arr_append(rlt_mut_data_set, i.second);
				else
					yyjson_mut_arr_prepend(rlt_mut_data_set, i.second);
			}
			yyjson_mut_obj_add_val(rlt_mut_doc,rlt_mut_root, iter.first.c_str(), rlt_mut_data_set);
			result.rowCount += mapRlt.size();
		}
	}
	else if(result.rltDataSet.size() > 0){
		yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(rlt_mut_doc);
		yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);
		map<SORT_FLAG, yyjson_mut_val*>& mapRlt = result.rltDataSet;
		for (auto& i : mapRlt)
		{
			if (deSel.ascendingSort)
				yyjson_mut_arr_append(rlt_mut_root, i.second);
			else
				yyjson_mut_arr_prepend(rlt_mut_root, i.second);
		}
		result.rowCount = mapRlt.size();
	}
	else if (result.rltDataSetVecList.size() > 0) {
		yyjson_mut_val* rlt_mut_root = yyjson_mut_obj(rlt_mut_doc);
		yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);
		for (auto& iter : result.rltDataSetVecList) {
			vector<yyjson_mut_val*>& vecRlt = iter.second;
			yyjson_mut_val* rlt_mut_data_set = yyjson_mut_arr(rlt_mut_doc);
			for (auto& i : vecRlt)
			{
				if (deSel.ascendingSort)
					yyjson_mut_arr_append(rlt_mut_data_set, i);
				else
					yyjson_mut_arr_prepend(rlt_mut_data_set, i);
			}
			yyjson_mut_obj_add_val(rlt_mut_doc, rlt_mut_root, iter.first.c_str(), rlt_mut_data_set);
			result.rowCount += vecRlt.size();
		}
	}
	else if (result.rltDataSetVec.size() > 0) {
		yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(rlt_mut_doc);
		yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);
		vector<yyjson_mut_val*>& vecRlt = result.rltDataSetVec;
		for (auto& i : vecRlt)
		{
			if (deSel.ascendingSort)
				yyjson_mut_arr_append(rlt_mut_root, i);
			else
				yyjson_mut_arr_prepend(rlt_mut_root, i);
		}
		result.rowCount = vecRlt.size();
	}
	else {
		yyjson_mut_val* rlt_mut_root = yyjson_mut_arr(rlt_mut_doc);
		yyjson_mut_doc_set_root(rlt_mut_doc, rlt_mut_root);
		result.rowCount = 0;
	}

	size_t len = 0;
	if(deSel.calc != ""){
		if (pCalcResult != nullptr) {
			char* p = yyjson_mut_write(rlt_mut_doc, 0, &len);
			if (p) {
				result.calcResult = p;
				free(p);
			}
		}
		else if (sCalcResult != "") {
			result.calcResult = sCalcResult;
		}
		else {
			result.calcResult = "";
		}
	}
	else {
		//if p==null，maybe int rlt_mut_doc,some string type pointed to local variable and is already released
		char* p = yyjson_mut_write(rlt_mut_doc, 0, &len);
		if (p) {
			result.dataList = p;
			free(p);
		}
	}

	return true;
}

void TDB::parseDESelector(yyjson_val* yyParams, DE_SELECTOR& deSel, string& err)
{
	//parse time selector
	if (deSel.timeSel.enable) {
		std::string strTime = "";
		std::string strStartDate, strEndDate;
		DB_TIME stStartDate, stEndDate;
		yyjson_val* yyv_time = yyjson_obj_get(yyParams, "time");
		if (yyv_time == nullptr) {
			err = "param missing: time";
			return;
		}

		yyjson_val* yyv_snapshot = yyjson_obj_get(yyParams, "snapshot");
		if (yyv_snapshot != nullptr) {
			deSel.timeSel.snapShot = true;
		}

		if (yyjson_is_str(yyv_time)) {
			strTime = yyjson_get_str(yyv_time);
			if (!deSel.timeSel.init(strTime)) {
				err = "time selector format error:" + deSel.timeSel.error;
				return;
			}
			for (int i = 0; i < deSel.timeSel.atomSelList.size(); i++) {
				deSel.timeSel.atomSelList[i].snapShot = deSel.timeSel.snapShot;
			}
		}
		else if (yyjson_is_arr(yyv_time)) {
			size_t idx = 0;
			size_t max = 0;
			yyjson_val* item;
			vector<string> timeSelList;
			yyjson_arr_foreach(yyv_time, idx, max, item) {
				if (yyjson_is_str(item)) {
					string s = yyjson_get_str(item);
					timeSelList.push_back(s);
				}
			}
			if (!deSel.timeSel.init(timeSelList)) {
				err = "time selector format error:" + deSel.timeSel.error;
				return;
			}
		}
		else {
			err = "param time must be string type or array type";
			return;
		}

		yyjson_val* yyv_timeFmt = yyjson_obj_get(yyParams, "timeFmt");
		if (yyv_timeFmt && yyjson_is_str(yyv_timeFmt)) {
			deSel.timeSel.timeFmt = yyjson_get_str(yyv_timeFmt);
		}
	}
	
	//parse tag selector
	std::string strRootTag;
	std::vector<string> tagList;
	yyjson_val* yyv_tag = yyjson_obj_get(yyParams, "tag");
	yyjson_val* yyv_colume = yyjson_obj_get(yyParams, "colume");
	if (yyv_tag && yyjson_is_str(yyv_tag))
	{
		string tag = yyjson_get_str(yyv_tag);
		if (m_isGbk) {
			tag = DB_STR::gb_to_utf8(tag);
		}
		tagList.push_back(tag);
	}
	else if (yyv_tag && yyjson_is_arr(yyv_tag)) {
		size_t idx = 0;
		size_t max = 0;
		yyjson_val* item;
		yyjson_arr_foreach(yyv_tag, idx, max, item) {
			if (!yyjson_is_str(item)) {
				err = "tag must be string type";
				return;
			}
			string tag = yyjson_get_str(item);
			tagList.push_back(tag);
		}
	}
	else if (yyv_colume) {//colume only support exact tag , fuzzy tag not supported
		deSel.tagAsColume = true;
		yyjson_val* yyv_colList = yyv_colume;
		size_t idx = 0;
		size_t max = 0;
		yyjson_val* item;
		yyjson_arr_foreach(yyv_colList, idx, max, item) {
			if (yyjson_is_obj(item)) {
				yyjson_val* yyv_tag = yyjson_obj_get(item, "tag");
				string tag = yyjson_get_str(yyv_tag);
				tagList.push_back(tag);
				yyjson_val* yyv_aggr = yyjson_obj_get(item, "aggregate");
				deSel.vecAggregate.push_back(getAggrOpt(yyv_aggr));
				deSel.bAggr = true;
				yyjson_val* yyv_tagLabel = yyjson_obj_get(item, "label");
				string tagLabel = yyjson_get_str(yyv_tagLabel);
				if (tagLabel != "")
					deSel.vecTagLable.push_back(tagLabel);
				else {
					deSel.vecTagLable.push_back(tag);
				}
			}
			else if (yyjson_is_str(item)) {
				string tag = yyjson_get_str(item);
				deSel.vecTagLable.push_back(tag);
				tagList.push_back(tag);
			}
		}
	}
	else {
		err = " tag or colume must be specified";
		return;
	}

	yyjson_val* yyv_rootTag = yyjson_obj_get(yyParams, "rootTag");
	if (yyv_rootTag && yyjson_is_str(yyv_rootTag))
	{
		strRootTag = yyjson_get_str(yyv_rootTag);
	}

	if (!deSel.tagSel.init(tagList, strRootTag)) {
		err = "tag selector format error:" + deSel.tagSel.error;
		return;
	}

	yyjson_val* yyv_getTag = yyjson_obj_get(yyParams, "getTag");
	if (yyv_getTag) {
		deSel.tagSel.getTag = yyjson_get_bool(yyv_getTag);
	}


	//obj type
	yyjson_val* yyv_type = yyjson_obj_get(yyParams, "type");
	if (yyv_type && yyjson_is_str(yyv_type)) {
		deSel.tagSel.type = yyjson_get_str(yyv_type);
	}

	yyjson_val* yyv_deType = yyjson_obj_get(yyParams, "deType");
	if (yyv_deType && yyjson_is_str(yyv_deType)) {
		deSel.deType = yyjson_get_str(yyv_deType);
	}

	//parse downsampling selector
	yyjson_val* yyv_interval = yyjson_obj_get(yyParams, "interval");
	if (yyv_interval && yyjson_is_int(yyv_interval))
	{
		deSel.downSamplingSel.intervalType = INTERVAL_DOWN_SAMPLING_TYPE::DST_Count;
		deSel.downSamplingSel.dsi = yyjson_get_int(yyv_interval);
		//interval: every xxx, take the first 
		//"No this param" 、interval=1 is the same thing
		if (deSel.downSamplingSel.dsi == 0) {
			err = "when interval is num, it must large than 0 ";
			return;
		}
	}
	else if (yyv_interval && yyjson_is_str(yyv_interval))
	{
		string sDsti = yyjson_get_str(yyv_interval);
		deSel.downSamplingSel.dsti = dhmsSpan2Seconds(sDsti);
		if (deSel.downSamplingSel.dsti > 0)
			deSel.downSamplingSel.intervalType = INTERVAL_DOWN_SAMPLING_TYPE::DST_Time;
	}
	yyjson_val* yyv_minDiff = yyjson_obj_get(yyParams, "minDiff");
	if (yyv_minDiff && yyjson_is_num(yyv_minDiff)) {
		deSel.downSamplingSel.minDiffDownSampling = true;
		deSel.downSamplingSel.minDiff = yyjson_get_num(yyv_minDiff);
	}

	//parse condition selector
	string filter;
	yyjson_val* yyv_match = yyjson_obj_get(yyParams, "match");
	if (yyv_match) {
		filter = yyjson_get_str(yyv_match);
		deSel.condition.init(filter);
	}

	yyjson_val* yyv_aSort = yyjson_obj_get(yyParams, "a-sort");
	yyjson_val* yyv_dSort = yyjson_obj_get(yyParams, "d-sort");
	if (yyv_aSort) {
		deSel.ascendingSort = true;
		deSel.sortKey = yyjson_get_str(yyv_aSort);
	}
	else if (yyv_dSort) {
		deSel.ascendingSort = false;
		deSel.sortKey = yyjson_get_str(yyv_dSort);
	}

	yyjson_val* yyv_tagAsColume = yyjson_obj_get(yyParams, "tagAsColume");
	if (yyjson_is_bool(yyv_tagAsColume)) {
		deSel.tagAsColume = yyjson_get_bool(yyv_tagAsColume);
	}


	yyjson_val* yyv_valType = yyjson_obj_get(yyParams, "valType");
	if (yyv_valType && yyjson_is_str(yyv_valType)) {
		deSel.valType = yyjson_get_str(yyv_valType);
	}


	//name mode or  tag mode  colLabel
	yyjson_val* yyv_colLabel = yyjson_obj_get(yyParams, "columeLabel");
	if (yyv_colLabel && yyjson_is_str(yyv_colLabel)) {
		deSel.tagLabel = yyjson_get_str(yyv_colLabel);

		//use name or tag as label,or specified label ,only supported when only one tag
		if (deSel.tagLabel != "name" && deSel.tagLabel != "tag") {
			deSel.vecTagLable.push_back(deSel.tagLabel);
		}
	}
	//custom columeLabel
	else if (yyv_colLabel && yyjson_is_arr(yyv_colLabel)) {
		size_t idx = 0;
		size_t max = 0;
		yyjson_val* item;
		yyjson_arr_foreach(yyv_colLabel, idx, max, item) {
			deSel.vecTagLable.push_back(yyjson_get_str(item));
		}
	}

	yyjson_val* yyv_groupby = yyjson_obj_get(yyParams, "groupby");
	if (yyv_groupby && yyjson_is_str(yyv_groupby)) {
		deSel.groupby = yyjson_get_str(yyv_groupby);

		//if aggr by time
		if (deSel.groupby.find("day") != string::npos) {
			deSel.groupByTime = true;
			deSel.timeGroupBy = "day";
		}
		else if (deSel.groupby.find("year") != string::npos) {
			deSel.groupByTime = true;
			deSel.timeGroupBy = "year";
		}
		else if (deSel.groupby.find("month") != string::npos) {
			deSel.groupByTime = true;
			deSel.timeGroupBy = "month";
		}
		else if (deSel.groupby.find("hour") != string::npos) {
			deSel.groupByTime = true;
			deSel.timeGroupBy = "hour";
		}
		else if (deSel.groupby.find("minute") != string::npos) {
			deSel.groupByTime = true;
			deSel.timeGroupBy = "minute";
		}
		else if (deSel.groupby.find("week") != string::npos) {
			deSel.groupByTime = true;
			deSel.timeGroupBy = "week";
		}
		else {
			deSel.groupByTime = false;
		}

		//if aggr by tag
		if (deSel.groupby.find("tag") != string::npos) {
			deSel.groupByTag = true;
		}
		else {
			deSel.groupByTag = false;
		}
	}

	yyjson_val* yyv_aggr = yyjson_obj_get(yyParams, "aggregate");
	if (yyv_aggr == nullptr) {
		yyv_aggr = yyjson_obj_get(yyParams, "aggr");
	}

	if (yyjson_is_arr(yyv_aggr)) { //muti tag aggr
		size_t idx = 0;
		size_t max = 0;
		yyjson_val* item;
		yyjson_arr_foreach(yyv_aggr, idx, max, item) {
			deSel.vecAggregate.push_back(getAggrOpt(item));
		}
		deSel.bAggr = true;
		deSel.groupByTag = true; //group by tag by defaut,equals to aggr by each column
	}
	else if (yyjson_is_obj(yyv_aggr) || yyjson_is_str(yyv_aggr)) {
		deSel.aggregate = getAggrOpt(yyv_aggr);
		deSel.bAggr = true;
	}

	yyjson_val* yyv_timeSlots = yyjson_obj_get(yyParams, "timeSlot");
	if (yyjson_is_obj(yyv_timeSlots)) {
		size_t idx = 0;
		size_t maxIdx = 0;
		yyjson_val* key;
		yyjson_val* val;
		yyjson_obj_foreach(yyv_timeSlots, idx, maxIdx, key, val) {
			string slotName = yyjson_get_str(key);
			vector<DB_TIME_RANGE> rangeSeries;

			size_t timeIdx = 0;
			size_t maxTimeIdx = 0;
			yyjson_val* yyTimeRange;
			yyjson_arr_foreach(val, timeIdx, maxTimeIdx, yyTimeRange) {
				string timeRange = yyjson_get_str(yyTimeRange);
				DB_TIME_RANGE t = parseTimeRange(timeRange);
				rangeSeries.push_back(t);
			}

			deSel.mapTimeSlots[slotName] = rangeSeries;
		}
		if (deSel.mapTimeSlots.size() == 0)
		{
			err = "timeSlot is obj,but null";
			return;
		}
	}

	yyjson_val* yyv_offset = yyjson_obj_get(yyParams, "offset");
	if (yyv_offset) {
		if (yyjson_is_int(yyv_offset))
		{
			deSel.offset = yyjson_get_int(yyv_offset);
		}
	}
	yyjson_val* yyv_limit = yyjson_obj_get(yyParams, "limit");
	if (yyv_limit) {
		if (yyjson_is_int(yyv_limit))
		{
			deSel.limit = yyjson_get_int(yyv_limit);
		}
	}

	yyjson_val* yyv_when = yyjson_obj_get(yyParams, "when");
	if (yyv_when) {
		if (yyjson_is_obj(yyv_when))
		{
			yyjson_val* yyv_when_tag = yyjson_obj_get(yyv_when, "tag");
			yyjson_val* yyv_when_match = yyjson_obj_get(yyv_when, "match");
			yyjson_val* yyv_when_relation = yyjson_obj_get(yyv_when, "relation");
			if (yyjson_is_str(yyv_limit)){
				deSel.whenSel.tag = yyjson_get_str(yyv_when_tag);
			}
			if (yyjson_is_str(yyv_when_match)) {
				deSel.whenSel.match = yyjson_get_str(yyv_when_match);
				deSel.whenSel.condition.init(deSel.whenSel.match);
			}
			if (yyjson_is_arr(yyv_when_relation)) {
				size_t idx = 0;
				size_t max = 0;
				yyjson_val* item;
				yyjson_arr_foreach(yyv_when_relation, idx, max, item) {
					yyjson_val* rela_type = yyjson_obj_get(item,"type");
					TIME_RELATION tr;
					tr.type = yyjson_get_str(rela_type);
					yyjson_val* rela_offset = yyjson_obj_get(item, "offset");
					tr.offset = yyjson_get_int(rela_offset);
					yyjson_val* rela_count = yyjson_obj_get(item, "count");
					tr.count = yyjson_get_int(rela_count);
					deSel.whenSel.relation.push_back(tr);
				}
			}
		}
	}

	yyjson_val* yyv_splitBy = yyjson_obj_get(yyParams, "splitBy");
	if (yyv_splitBy) {
		if (yyjson_is_str(yyv_splitBy))
		{
			deSel.splitBy = yyjson_get_str(yyv_splitBy);
		}
	}

	yyjson_val* yyv_pageNo = yyjson_obj_get(yyParams, "pageNo");
	if (yyv_pageNo) {
		if (yyjson_is_int(yyv_pageNo))
		{
			deSel.pageNo = yyjson_get_int(yyv_pageNo);
		}
	}
	yyjson_val* yyv_pageSize = yyjson_obj_get(yyParams, "pageSize");
	if (yyv_pageSize) {
		if (yyjson_is_int(yyv_pageSize))
		{
			deSel.pageSize = yyjson_get_int(yyv_pageSize);
		}
	}

	//need add tag region into de
	if (deSel.tagSel.tagSet.size() > 1 || deSel.splitBy != "tag" || deSel.deType == "curveIdx") {
		deSel.tagSel.getTag = true;
	}
}

void TDB::Insert(string strTag, bool bVal, DB_TIME* stTime) {
	DB_TIME dbt;
	if (stTime != nullptr) {
		dbt = *stTime;
	}
	else {
		dbt.setNow();
	}
	string s = bVal ? "true" : "false";
	InsertValJsonStr(strTag, dbt, s);
}

void TDB::Insert(string strTag, string& sDeIdx, string& sDeCurve, DB_TIME* time) {
	DB_TIME stTime;
	if (time) {
		stTime = *time;
	}
	else {
		stTime = TIME_OPT::now();
	}

	string deListFolderPath = getPath_dataFolder(strTag, stTime);
	if (!fileExist(deListFolderPath))
		DB_FS::createFolderOfPath(deListFolderPath.c_str());

	yyjson_doc* doc = yyjson_read(sDeIdx.c_str(), sDeIdx.length(), 0);
	yyjson_mut_doc* mdoc = yyjson_doc_mut_copy(doc, NULL);
	yyjson_val* yyDe = yyjson_doc_get_root(doc);
	yyjson_mut_val* yymDe = yyjson_mut_doc_get_root(mdoc);
	//if (yyjson_obj_get(yyDe, "time") == nullptr) {
	yyjson_mut_val* timeKey = yyjson_mut_strcpy(mdoc, "time");
	yyjson_mut_val* timeVal;
	string sTime = stTime.toStr(true);
	timeVal = yyjson_mut_strcpy(mdoc, sTime.data());
	yyjson_mut_obj_put(yymDe, timeKey, timeVal);
	//}

	string deFilePath = deListFolderPath + "/" + stTime.toStampHMS() + m_dbFmt.curveDeNameSuffix;
	DB_FS::writeFile(deFilePath, (char*)sDeCurve.c_str(), sDeCurve.length());

	string dataListPath;
	dataListPath = deListFolderPath + "/" + m_dbFmt.curveIdxListName;
	saveDeToDataListFile(dataListPath, yymDe);

	yyjson_mut_doc_free(mdoc);
	yyjson_doc_free(doc);
}

bool TDB::Select_Step_loadFile(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, SELECT_RLT& result) {
	if (m_timeUnit == BY_DAY) {
		for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++) {
			TAG_FILE_SET& fSet = *tagDBFileSet[tagIdx];

			for (int timeSelAtomIdx = 0; timeSelAtomIdx < deSel.timeSel.atomSelList.size(); timeSelAtomIdx++) {
				TIME_SELECTOR_ATOM& tsa = deSel.timeSel.atomSelList[timeSelAtomIdx];

				if (deSel.deType == "curve") { //time select must be a time point
					DB_FILE* pdf = new DB_FILE(tsa.startTime, fSet.dbFileTag, this);
					pdf->deType = deSel.deType;

					if (!pdf->loadFile()) {
						delete pdf;
						continue;
					}

					fSet.fileList.push_back(pdf);
				}
				else {
					//use date only to iterator db file. use time plus 24*60*60 to iterator will cause time not in timerange
					//2024-12-12 17:00:00~2024-12-13 05:00:00, iterator by adding 24*60*60,2024-12-13 17:00:00 is not in time range,file will not be selected
					DB_TIME dbtStartDate = tsa.stStart; dbtStartDate.clearHMS();
					DB_TIME dbtEndDate = tsa.stEnd; dbtEndDate.clearHMS();

					time_t startDate = dbtStartDate.toUnixTime();
					time_t endDate = dbtEndDate.toUnixTime();

					if (tsa.timeSetType == TSM_First) {
						time_t loadDate = startDate;
						for (; loadDate <= endDate; loadDate += 24 * 60 * 60) {
							DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
							pdf->deType = deSel.deType;

							if (!pdf->loadFile()) {
								delete pdf;
								continue;
							}

							fSet.fileList.push_back(pdf);
							break;
						}
					}
					else if (tsa.timeSetType == TSM_Last) {
						time_t loadDate = endDate;
						for (; loadDate >= startDate; loadDate -= 24 * 60 * 60) {
							DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
							pdf->deType = deSel.deType;

							if (!pdf->loadFile()) {
								delete pdf;
								continue;
							}

							fSet.fileList.push_back(pdf);
							break;
						}
					}
					else {
						bool bFirstLastAggr = false;
						if (deSel.aggregate.size() > 0) {
							map<string, vector<string>>::iterator aggrOpt = deSel.aggregate.begin();
							vector<string>& aggrTypes = aggrOpt->second;

							if (deSel.groupByTime == false) {//groupby entire time range,optimize performance in this kind of query
								if (aggrTypes.size() == 1) {
									string& aggrType = aggrTypes[0];
									if (aggrType == "diff.first-last" || aggrType == "diff.last-first") {
										bFirstLastAggr = true;
									}
								}
							}
						}

						if (bFirstLastAggr) {
							//read first file
							time_t loadDate = startDate;
							for (; loadDate <= endDate; loadDate += 24 * 60 * 60) {
								DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
								pdf->deType = deSel.deType;

								if (!pdf->loadFile()) {
									delete pdf;
									continue;
								}

								fSet.fileList.push_back(pdf);
								break;
							}

							//read last file
							loadDate = endDate;
							for (; loadDate >= startDate; loadDate -= 24 * 60 * 60) {
								DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
								pdf->deType = deSel.deType;

								if (!pdf->loadFile()) {
									delete pdf;
									continue;
								}

								fSet.fileList.push_back(pdf);
								break;
							}
						}
						else {
							time_t loadDate = endDate;
							for (; loadDate >= startDate; loadDate -= 24 * 60 * 60) {
								DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
								pdf->deType = deSel.deType;

								if (!pdf->loadFile()) {
									delete pdf;
									continue;
								}

								fSet.fileList.insert(fSet.fileList.begin(), pdf);
							}
						}
					}
				}
			}

			if (fSet.fileList.size() == 0) {
				continue;
			}

			//the first and last file need time range check when load de,the middles do not need
			fSet.fileList[0]->boundaryFile = true;
			fSet.fileList[fSet.fileList.size() - 1]->boundaryFile = true;

			result.fileCount += fSet.fileList.size();
		}
	}
	else if (m_timeUnit == BY_MONTH) {
		for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
		{
			TAG_FILE_SET& fSet = *tagDBFileSet[tagIdx];

			for (int timeSelAtomIdx = 0; timeSelAtomIdx < deSel.timeSel.atomSelList.size(); timeSelAtomIdx++) {
				TIME_SELECTOR_ATOM& tsa = deSel.timeSel.atomSelList[timeSelAtomIdx];
				if (deSel.deType == "curve") { //time select must be a time point
					DB_FILE* pdf = new DB_FILE(tsa.startTime, fSet.dbFileTag, this);
					pdf->deType = deSel.deType;
					if (!pdf->loadFile()) {
						delete pdf;
						continue;
					}
					fSet.fileList.push_back(pdf);
				}
				else {
					//use date only to iterator db file. use time plus 24*60*60 to iterator will cause time not in timerange
					//2024-12-12 17:00:00~2024-12-13 05:00:00, iterator by adding 24*60*60,2024-12-13 17:00:00 is not in time range,file will not be selected
					DB_TIME dbtStartDate = tsa.stStart; dbtStartDate.clearHMS();
					DB_TIME dbtEndDate = tsa.stEnd; dbtEndDate.clearHMS();
					//time_t startDate = dbtStartDate.toUnixTime();
					//time_t endDate = dbtEndDate.toUnixTime();
					if (tsa.timeSetType == TSM_First) {
						DB_TIME loadDate = dbtStartDate;
						for (; loadDate <= dbtEndDate && loadDate.wMonth < 13; loadDate.wMonth++)
						{
							DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
							pdf->deType = deSel.deType;
							if (!pdf->loadFile()) {
								delete pdf;
								continue;
							}
							fSet.fileList.push_back(pdf);
							break;
						}
					}
					else if (tsa.timeSetType == TSM_Last) {
						DB_TIME loadDate = dbtStartDate;
						for (; loadDate <= dbtEndDate && loadDate.wMonth < 13; loadDate.wMonth++)
						{
							DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
							pdf->deType = deSel.deType;
							if (!pdf->loadFile()) {
								delete pdf;
								continue;
							}
							fSet.fileList.push_back(pdf);
							break;
						}
					}
					else {

						bool bFirstLastAggr = false;
						if (deSel.aggregate.size() > 0) {
							map<string, vector<string>>::iterator aggrOpt = deSel.aggregate.begin();
							vector<string>& aggrTypes = aggrOpt->second;
							if (deSel.groupByTime == false) //groupby entire time range,optimize performance in this kind of query
							{
								if (aggrTypes.size() == 1) {
									string& aggrType = aggrTypes[0];
									if (aggrType == "diff.first-last" || aggrType == "diff.last-first") {
										bFirstLastAggr = true;
									}
								}
							}
						}

						if (bFirstLastAggr) {
							//read first file
							DB_TIME loadDate = dbtStartDate;
							for (; loadDate <= dbtEndDate && loadDate.wMonth < 13; loadDate.wMonth++)
							{
								DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
								pdf->deType = deSel.deType;
								if (!pdf->loadFile()) {
									delete pdf;
									continue;
								}
								fSet.fileList.push_back(pdf);
								break;
							}
							//read last file
							//loadDate = endDate;
							//for (; loadDate >= startDate; loadDate -= 24 * 60 * 60)
							loadDate = dbtEndDate;
							auto wDay = dbtEndDate.wDay;
							for (; loadDate >= dbtStartDate && loadDate.wMonth > 0; loadDate.wMonth--)
							{
								loadDate.wDay = wDay;
								auto lastDayOfMonth = loadDate.getMaxDayOfMonth();
								if (loadDate.wDay > lastDayOfMonth) {
									loadDate.wDay = lastDayOfMonth;
								}

								DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
								pdf->deType = deSel.deType;
								if (!pdf->loadFile()) {
									delete pdf;
									continue;
								}
								fSet.fileList.push_back(pdf);
								break;
							}
						}
						else {
							DB_TIME loadDate = dbtEndDate;
							auto wDay = dbtEndDate.wDay;
							for (; loadDate >= dbtStartDate && loadDate.wMonth > 0; loadDate.wMonth--)
							{
								loadDate.wDay = wDay;
								auto lastDayOfMonth = loadDate.getMaxDayOfMonth();
								if (loadDate.wDay > lastDayOfMonth) {
									loadDate.wDay = lastDayOfMonth;
								}

								DB_FILE* pdf = new DB_FILE(loadDate, fSet.dbFileTag, this);
								pdf->deType = deSel.deType;
								if (!pdf->loadFile()) {
									delete pdf;
									continue;
								}
								fSet.fileList.insert(fSet.fileList.begin(), pdf);
							}
						}
					}
				}
			}

			if (fSet.fileList.size() == 0)
				continue;
			//the first and last file need time range check when load de,the middles do not need
			fSet.fileList[0]->boundaryFile = true;
			fSet.fileList[fSet.fileList.size() - 1]->boundaryFile = true;

			//do not down sample when time is short than one day 
			//if (fSet.fileList.size() <= 1)
				//deSel.interval.type = DOWN_SAMPLING_TYPE::DST_None;

			result.fileCount += fSet.fileList.size();
		}
	}
	else if (m_timeUnit == NONE) {
		for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
		{
			TAG_FILE_SET& fSet = *tagDBFileSet[tagIdx];
			time_t t = 0;
			DB_FILE* pdf = new DB_FILE(t, fSet.dbFileTag, this);
			pdf->deType = deSel.deType;
			if (!pdf->loadFile()) {
				delete pdf;
				continue;
			}
			fSet.fileList.push_back(pdf);
		}
	}

	return true;
}

void TDB::getDeTime(yyjson_mut_val* yyTime, string& deTime) {
	if (m_timeUnit == BY_DAY) {
		string szTime = yyjson_mut_get_str(yyTime);
		const char* pHms = nullptr;
		int hmsLen = 0;
		if (szTime.length() == 19) //2020-02-02 02:02:02
		{
			pHms = szTime.data() + 11;//get hour min sec
			hmsLen = 8;
		}
		else if (szTime.length() == 23) {//2020-02-02 02:02:02.222
			pHms = szTime.data() + 11;
			hmsLen = 12;
		}
		else
		{
			pHms = szTime.data();
			hmsLen = 8;
		}
		memcpy((char*)deTime.data() + 11, pHms, hmsLen);//get hour min sec
	}
	else if (m_timeUnit == NONE) {
		deTime = yyjson_mut_get_str(yyTime);
		if (deTime.length() == 19) {
			deTime += ".000";
		}
	}
	else {
		//assert(false);
	}
}

struct AutoLastDeSet {
	yyjson_val** de;
	yyjson_val** lastDe;
	AutoLastDeSet(yyjson_val** de, yyjson_val** lastDe) {
		this->de = de;
		this->lastDe = lastDe;
	}
	~AutoLastDeSet() {
		*lastDe = *de;
	}
};

bool TDB::Select_Step_loadDataElem(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, vector<DATA_SET*>& outputDataSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc)
{
	bool needMut = false;
	for (int tagIdx = 0; tagIdx < tagDBFileSet.size(); tagIdx++)
	{
		TAG_FILE_SET& fSet = *tagDBFileSet[tagIdx];
		DATA_SET& fSetOut = *outputDataSet[tagIdx];
		string& tag = fSet.tag; // string& instead of local var. yyjson do not copy
		string& relTag = fSetOut.relTag;


		for (int i = 0; i < fSet.fileList.size(); i++)
		{
			DB_FILE* pdf = fSet.fileList[i];

			size_t idx, max;
			yyjson_val* de = nullptr;
			yyjson_val* lastDe = nullptr;
			yyjson_val* deSnapshot = nullptr;
			string snapshotDeTime = "";
			int lastDeTime = 0;
			int currDeTime = 0;
			string deTime = pdf->ymd + " 00:00:00.000";
			DB_TIME dbT;
			string tmpTime;
			string groupKeyVal;
			bool ymdEqualityCheckedInDe = false; //check ymd in one de ,if not equal to db folder ymd throw exception
			bool hasTimeStamp = (deSel.deType != "statisDe" && deSel.deType != "statisByDay" && deSel.deType != "statisByMonth");
			yyjson_val* deList = nullptr;
			yyjson_type type = yyjson_get_type(pdf->root);
			if (type == YYJSON_TYPE_OBJ) { //file with desc
				deList = yyjson_obj_get(pdf->root, "data");
				if (deList == nullptr) { //copatible with sap  data_list[0].data mode
					deList = yyjson_obj_get(pdf->root, "data_list");
					if (yyjson_is_arr(deList) && yyjson_arr_size(deList) > 0) {
						deList = yyjson_arr_get(deList, 0);
						if (yyjson_is_obj(deList)) {
							deList = yyjson_obj_get(deList, "data");
						}
					}
				}
			}
			else if (type == YYJSON_TYPE_ARR) {
				deList = pdf->root;
			}
			else {
				continue;
			}

			DE_JSON_TYPE deJsonType = DE_J_OBJ;

			if (deSel.timeSel.atomSelList[0].timeSetType = TSM_Last)
			{
				idx = yyjson_arr_size(deList) - 1;
			}
			yyjson_arr_foreach(deList, idx, max, de) {
				//compatible with number save as a string,when in a aggr query,auto cast to number,info tips returneds
				AutoLastDeSet autoSet(&de, &lastDe);
				if (idx == 0)
				{
					if (deSel.downSamplingSel.minDiffDownSampling)
					{
						yyjson_val* yyVal = nullptr;
						if (deJsonType == DE_J_OBJ) {
							yyVal = yyjson_obj_get(de, m_dbFmt.deItemKey_value.c_str());
						}
						else {
							yyVal = yyjson_arr_get(de, 1);
						}

						if (!yyjson_is_num(yyVal))
						{
							db_exception e;
							e.m_error = "val type must be number when use minDiff query";
							throw e;
						}
					}

					if (yyjson_is_arr(de)) {
						deJsonType = DE_J_ARR;
						if (yyjson_arr_size(de) != 2) {
							db_exception e;
							e.m_error = "tds now only support 2 element array de type";
							throw e;
						}
					}

					if (yyjson_get_type(de) == YYJSON_TYPE_STR) {
						if (deSel.isValTypeNumber()) {
							needMut = true;
						}
					}

					if (deSel.bAggr) {
						yyjson_val* yyVal = nullptr;
						if (deJsonType == DE_J_OBJ) {
							yyVal = yyjson_obj_get(de, m_dbFmt.deItemKey_value.c_str());
						}
						else {
							yyVal = yyjson_arr_get(de, 1);
						}


						if (yyVal && yyjson_is_str(yyVal) && deSel.valType == "")
						{
							for (auto& aggrParam : fSetOut.aggregate) {
								vector<string>& aggrTypes = aggrParam.second;
								string aggrType = aggrTypes[0];
								if (aggrType == "diff" || aggrType == "avg" || aggrType == "sum" || aggrType == "max" || aggrType == "min" || aggrType == "diff.first-last" || aggrType == "diff.lao") {
									//string err = "data element type is: string, does not support aggregate type:" + aggrType;
									//err += ",use valType=number to cast string value to number value";
									//json jErr = err;
									//result.error = jErr.dump();
									//return false;
									deSel.valType = "number";
									result.info = "aggregate type is " + aggrType + ",auto cast value type string to number";
									break;
								}
							}
						}
					}
				}

				bool selBy_minDiff = true;
				bool selBy_interval = true;
				if (deSel.downSamplingSel.minDiffDownSampling) {
					if (de && lastDe) {
						yyjson_val* yyVal = nullptr;
						if (deJsonType == DE_J_OBJ) {
							yyVal = yyjson_obj_get(de, m_dbFmt.deItemKey_value.c_str());
						}
						else {
							yyVal = yyjson_arr_get(de, 1);
						}
						double currVal = yyjson_get_num(yyVal);

						yyjson_val* lastYyVal = nullptr;
						if (deJsonType == DE_J_OBJ) {
							lastYyVal = yyjson_obj_get(lastDe, m_dbFmt.deItemKey_value.c_str());
						}
						else {
							lastYyVal = yyjson_arr_get(lastDe, 1);
						}
						double lastVal = yyjson_get_num(lastYyVal);

						if (fabs(currVal - lastVal) < deSel.downSamplingSel.minDiff)
							selBy_minDiff = false;
					}
				}

				//every downsampling interval output one de; dsi=3,output 0 3 6...
				if (deSel.downSamplingSel.intervalType == INTERVAL_DOWN_SAMPLING_TYPE::DST_Count)
				{
					//"No this param" 、interval=1 is the same thing
					if (deSel.downSamplingSel.dsi > 1) {
						bool reachInterval = idx % deSel.downSamplingSel.dsi == 0 ;
						if (!reachInterval)
							selBy_interval = false;
					}
					if(deSel.downSamplingSel.dsi > max)
						selBy_interval = false;

					if (deSel.downSamplingSel.minDiffDownSampling) {
						if (!selBy_minDiff && !selBy_interval) {
							continue;
						}
					}
					else {
						if (!selBy_interval)
							continue;
					}
				}
				else if (deSel.downSamplingSel.intervalType == INTERVAL_DOWN_SAMPLING_TYPE::DST_None) {
					if (deSel.downSamplingSel.minDiffDownSampling) {
						if (!selBy_minDiff)
							continue;
					}
				}



				//generate standard time stamp, then do match
				if (hasTimeStamp) //has time stamp with HourMinSecond. statis de has no hms info,skip this process
				{
				yyjson_val* yyTime = nullptr;
				if (deJsonType == DE_J_OBJ) {
					yyTime = yyjson_obj_get(de, "time");
				}
				else {
					yyTime = yyjson_arr_get(de, 0);
				}
				const char* szTime;
				if (yyjson_is_int(yyTime)) {
					time_t t = yyjson_get_uint(yyTime);
					dbT.fromUnixTime(t);
					tmpTime = dbT.toStr();
					szTime = tmpTime.c_str();
				}
				else {
					szTime = yyjson_get_str(yyTime);
				}

				if (m_timeUnit == BY_DAY) {

					const char* pHms = nullptr;
					int hmsLen = 0;
					if (strlen(szTime) == 19) //2020-02-02 02:02:02
					{
						pHms = szTime + 11;//get hour min sec
						hmsLen = 8;

						if (ymdEqualityCheckedInDe == false) {
							ymdEqualityCheckedInDe = true;
							if (memcmp(szTime, pdf->ymd.data(), 10) != 0) {
								db_exception dbe;
								dbe.m_error = "db corruption,ymd in one de not equal to db folder path ymd";
								throw dbe;
							}
						}
					}
					else if (strlen(szTime) == 23) {//2020-02-02 02:02:02.222
						pHms = szTime + 11;
						hmsLen = 12;

						if (ymdEqualityCheckedInDe == false) {
							ymdEqualityCheckedInDe = true;
							if (memcmp(szTime, pdf->ymd.data(), 10) != 0) {
								db_exception dbe;
								string s = szTime;
								dbe.m_error = "db corruption,ymd " + s + " in one de not equal to db folder path ymd " + pdf->ymd + ",db file path:" + pdf->path;
								throw dbe;
							}
						}
					}
					else
					{
						pHms = szTime;
						hmsLen = 8;
					}
					memcpy((char*)deTime.data() + 11, pHms, hmsLen);//get hour min sec

					if (deSel.downSamplingSel.intervalType == INTERVAL_DOWN_SAMPLING_TYPE::DST_Time) {
						selBy_interval = true;
						HMS_STR* p = (HMS_STR*)pHms;
						currDeTime = p->getTotalSec();
						if (currDeTime - lastDeTime < deSel.downSamplingSel.dsti) {
							selBy_interval = false;
							if (deSel.downSamplingSel.minDiffDownSampling) {
								if (!selBy_minDiff && !selBy_interval) {
									continue;
								}
							}
							else {
								if (!selBy_interval)
									continue;
							}
						}
						lastDeTime = currDeTime;
					}
				}
				else if (m_timeUnit == NONE) {
					deTime = yyjson_get_str(yyTime);
				}
				else {
					//assert(false);
				}

				//boundary file is the first and last file of selected files,all de of files in the middle is selected,do not need to do time match
				if (pdf->boundaryFile) {
					if (!deSel.timeSel.Match(deTime)) {
						if (deSel.timeSel.snapShot) {
							de = deSnapshot;  //last de before not match is snapshot de. timeSel set to 00:00:00~snapshotTime
							deTime = snapshotDeTime;
							if (de == nullptr)
								break;
						}
						else { //normal mode
							continue;
						}
					}
					else {
						if (deSel.timeSel.snapShot) { //make desnapshot last de
							deSnapshot = de;
							snapshotDeTime = deTime;
							if (idx < max - 1) {  //not the last de ,check next; otherwise load this de as snapshot de 
								continue;
							}
						}
					}
				}
				}


				//use javascript to filter
				if (deSel.condition.bEnable && !deSel.condition.match(de))
				{
					continue;
				}

				if (deSel.bAggr) {
					if (deSel.timeGroupBy == "day") {
						groupKeyVal = deTime.substr(0, 10);
						map<string, vector<yyjson_val*>>::iterator it = fSetOut.m_origDeGrouped.find(groupKeyVal);
						if (it != fSetOut.m_origDeGrouped.end()) {
							it->second.push_back(de);
						}
						else {
							vector<yyjson_val*> newVec;
							newVec.push_back(de);
							fSetOut.m_origDeGrouped[groupKeyVal] = newVec;
						}
					}
					else if (deSel.timeGroupBy == "month") {
						groupKeyVal = deTime.substr(0, 7);
						map<string, vector<yyjson_val*>>::iterator it = fSetOut.m_origDeGrouped.find(groupKeyVal);
						if (it != fSetOut.m_origDeGrouped.end()) {
							it->second.push_back(de);
						}
						else {
							vector<yyjson_val*> newVec;
							newVec.push_back(de);
							fSetOut.m_origDeGrouped[groupKeyVal] = newVec;
						}
					}
					else if (deSel.timeGroupBy == "hour") {
						groupKeyVal = deTime.substr(0, 13);
						map<string, vector<yyjson_val*>>::iterator it = fSetOut.m_origDeGrouped.find(groupKeyVal);
						if (it != fSetOut.m_origDeGrouped.end()) {
							it->second.push_back(de);
						}
						else {
							vector<yyjson_val*> newVec;
							newVec.push_back(de);
							fSetOut.m_origDeGrouped[groupKeyVal] = newVec;
						}
					}
					else if (deSel.timeGroupBy == "minute") { //2020-02-03 11:12:14
						groupKeyVal = deTime.substr(0, 16);
						map<string, vector<yyjson_val*>>::iterator it = fSetOut.m_origDeGrouped.find(groupKeyVal);
						if (it != fSetOut.m_origDeGrouped.end()) {
							it->second.push_back(de);
						}
						else {
							vector<yyjson_val*> newVec;
							newVec.push_back(de);
							fSetOut.m_origDeGrouped[groupKeyVal] = newVec;
						}
					}
					else {
						fSetOut.m_orgDe.push_back(de);
					}
				}
				else {
					//need to add region tag in de
					if (deSel.tagSel.getTag || needMut) {
						DE_yyjson& deyy = *(new DE_yyjson());
						deyy.deTime = deTime;

						//set time
						//deyy.time = yyjson_mut_str(rlt_mut_doc, deyy.deTime.data());

						//set val
						yyjson_val* yyVal = nullptr;
						if (deJsonType == DE_JSON_TYPE::DE_J_OBJ) {
							yyVal = yyjson_obj_get(de, m_dbFmt.deItemKey_value.c_str());
						}
						else {
							yyVal = yyjson_arr_get(de, 1);
						}
						if (yyVal)
							deyy.val = yyjson_val_mut_copy(rlt_mut_doc, yyVal);

						//set all fields except time,val
						if (deJsonType == DE_JSON_TYPE::DE_J_OBJ) {
							deyy.de = yyjson_val_mut_copy(rlt_mut_doc, de);
						}

						if (yyjson_mut_get_type(deyy.val) == YYJSON_TYPE_STR) {
							if (deSel.isValTypeNumber())
							{
								string valStr = yyjson_mut_get_str(deyy.val);
								deyy.val = yyjson_mut_real(rlt_mut_doc, atof(valStr.data()));
							}
						}

						fSetOut.m_afterAggr.push_back(&deyy);
					}
					else {
						fSetOut.m_orgDe.push_back(de);
					}
				}

				result.deCount++;

				if (deSel.timeSel.snapShot) { //running to here means snapshot de already founded
					break;
				}

				if (deSel.timeSel.AmountMatch(result.deCount)) {
					break;
				}
			}
		}
	}
	return true;
}

bool TDB::Select_Step_FilterByRelation(DE_SELECTOR& deSel, vector<DATA_SET*>& inputDataSet, vector<DATA_SET*>& outputDataSet)
{
	DE_SELECTOR whenSel;
	whenSel.tagSel.init(deSel.whenSel.tag, deSel.tagSel.m_rootTag);
	whenSel.timeSel = deSel.timeSel;
	
	for (int i = 0; i < inputDataSet.size(); i++) {
		DATA_SET& ids = *inputDataSet[i];
		DATA_SET& ods = *outputDataSet[i];

		SELECT_RLT relTagData;
		Select(whenSel, relTagData);

		//parse sel rlt to time slot
		if (deSel.whenSel.whenStatus && deSel.whenSel.status.type == DBV_BOOL){
			bool lastStatus = !deSel.whenSel.status.bVal;
			bool inStatus = false;
			DB_TIME_SPAN timespan;
			for (auto& i : relTagData.rltDataSet) {
				yyjson_mut_val* yyv_val = yyjson_mut_obj_get(i.second, "val");
				if (yyjson_mut_is_bool(yyv_val)) {
					bool curStatus = yyjson_mut_get_bool(yyv_val);
					if (curStatus == deSel.whenSel.status.bVal && lastStatus != deSel.whenSel.status.bVal) {
						yyjson_mut_val* yyv_time = yyjson_mut_obj_get(i.second, "time");
						string time = yyjson_mut_get_str(yyv_time);
						timespan.start.fromStr(time);
						inStatus = true;
					}

					if (inStatus && curStatus != deSel.whenSel.status.bVal) {
						yyjson_mut_val* yyv_time = yyjson_mut_obj_get(i.second, "time");
						string time = yyjson_mut_get_str(yyv_time);
						timespan.end.fromStr(time);
						inStatus = false;
						deSel.whenSel.eventTimeSlot.push_back(timespan);
					}
				}
				else {
					return false;
				}
			}
		}


		//select only relate to timespan
		for (int i = 0; i < ids.m_afterAggr.size(); i++) {
			DE_yyjson* yyde = ids.m_afterAggr[i];
			//yyde->val
		}
	}

	return false;
}

bool TDB::Select_Step_doAggregate(DE_SELECTOR& deSel, vector<DATA_SET*>& inputData, yyjson_mut_doc* rlt_mut_doc)
{
	if (deSel.bAggr) {
		if (deSel.groupByTime) {
			for (int tagIdx = 0; tagIdx < inputData.size(); tagIdx++)
			{
				DATA_SET& fSet = *inputData[tagIdx];
				//every group aggr into a de
				for (auto& i : fSet.m_origDeGrouped) {
					DE_yyjson& aggrRltDe = *(new DE_yyjson());
					doAggregateOneGroup(deSel, fSet.aggregate, i.first, i.second, aggrRltDe, rlt_mut_doc);
					aggrRltDe.deTime = i.first.data(); //set as time group key such as 2024-08-13 2024-08-14
					fSet.m_afterAggr.push_back(&aggrRltDe);
				}
			}
		}
		else {//aggr in entire time selector.time in aggr result set as time selector 
			for (int tagIdx = 0; tagIdx < inputData.size(); tagIdx++)
			{
				DATA_SET& fSet = *inputData[tagIdx];
				if (fSet.m_orgDe.size() > 0) {
					DE_yyjson& aggrRltDe = *(new DE_yyjson());
					doAggregateOneGroup(deSel, fSet.aggregate, "all-time-range", fSet.m_orgDe, aggrRltDe, rlt_mut_doc);
					fSet.m_afterAggr.push_back(&aggrRltDe);
				}
			}
		}
	}
	return true;
}


bool TDB::handleRpc(const string& method, yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language)
{
	bool handled = true;
	if (method.find("db.") != string::npos) {
		yyjson_val* yyv_table = yyjson_obj_get(params, "table");
		if (yyv_table) {
			yyjson_val* yyv_tableType = yyjson_obj_get(params, "tableType");
			if (!yyv_tableType) {
				err = JSON_STR_VAL("must specify tableType");
				return true;
			}

			if (method == "db.insert") {
				db.rpc_db_table_insert(params, rlt, err, queryInfo, org, language);
			}
			else if (method == "db.select") {
				db.rpc_db_table_select(params, rlt, err, queryInfo, org, language);
			}
			else if (method == "db.update") {
				db.rpc_db_table_update(params, rlt, err, queryInfo, org, language);
			}
			else if (method == "db.delete") {
				db.rpc_db_table_delete(params, rlt, err, queryInfo, org, language);
			}
			else {
				handled = false;
			}
		}
		else {
			if (method == "db.insert") {
				db.rpc_db_insert(params, rlt, err, queryInfo, org, language);
			}
			else if (method == "db.select") {
				db.rpc_db_select(params, rlt, err, queryInfo, org, language);
			}
			else if (method == "db.update") {
				db.rpc_db_update(params, rlt, err, queryInfo, org, language);
			}
			else if (method == "db.delete") {
				db.rpc_db_delete(params, rlt, err, queryInfo, org, language);
			}
			else {
				handled = false;
			}
		}
	}
	else {
		handled = false;
	}
	
	return handled;
}

void TDB::rpc_db_table_insert(yyjson_val* params, string& rlt, string& err, string& queryInfo,const string& org, const string& language) {
	yyjson_val* yyv_tableType = yyjson_obj_get(params, "tableType");
	yyjson_val* yyv_table = yyjson_obj_get(params, "table");
	yyjson_val* yyv_row = yyjson_obj_get(params, "row");
	if (!yyv_row) {
		err = JSON_STR_VAL("must specify row");
		return;
	}
	string tableType = yyjson_get_str(yyv_tableType);
	string table = yyjson_get_str(yyv_table);

	string path = m_confPath + "/" + table;

	string data;
	DB_FS::readFile(path, data);
	if (data == "")
		data = "[]";

	yyjson_read_err yy_err = { 0 };
	yyjson_mut_doc* yy_mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_doc* yy_doc = yyjson_read_opts(
		(char*)data.data(),
		data.length(),
		YYJSON_READ_NOFLAG,
		NULL,
		&yy_err
	);
	if (!yy_doc) {
		err = JSON_STR_VAL("wrong table format,json parse error");
		return;
	}
	yy_mdoc = yyjson_doc_mut_copy(yy_doc,nullptr);
	yyjson_mut_val* yy_mroot = yyjson_mut_doc_get_root(yy_mdoc);

	if (yyjson_is_obj(yyv_row)) {
		yyjson_mut_val* yy_mut_row = yyjson_val_mut_copy(yy_mdoc, yyv_row);
		yyjson_mut_arr_append(yy_mroot, yy_mut_row);
	}
	else if(yyjson_is_arr(yyv_row)){
		size_t len = yyjson_arr_size(yyv_row);
        for (size_t i = 0; i < len; i++) {
			yyjson_val* yyv_oneRow = yyjson_arr_get(yyv_row, i);
			if (yyjson_is_obj(yyv_oneRow)) {
				yyjson_mut_val* yy_mut_row = yyjson_val_mut_copy(yy_mdoc, yyv_oneRow);
				yyjson_mut_arr_append(yy_mroot, yy_mut_row);
			}
		}
	}
	else {
		err = JSON_STR_VAL("row must be an object or an array");
		return;
	}


	size_t len = 0;
	char* p = yyjson_mut_val_write(yy_mroot, YYJSON_WRITE_PRETTY_TWO_SPACES,&len);
	if (p) {
		DB_FS::writeFile(path, p, len);
		free(p);
	}
	yyjson_doc_free(yy_doc);
    yyjson_mut_doc_free(yy_mdoc);

	rlt = DB_OK;
}

void TDB::rpc_db_table_delete(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language)
{
	yyjson_val* yyv_tableType = yyjson_obj_get(params, "tableType");
	yyjson_val* yyv_table = yyjson_obj_get(params, "table");
	yyjson_val* yyv_match = yyjson_obj_get(params, "match");
	if (!yyv_match) {
		err = JSON_STR_VAL("must specify match");
		return;
	}
	string tableType = yyjson_get_str(yyv_tableType);
	string table = yyjson_get_str(yyv_table);
	string match = yyjson_get_str(yyv_match);
	string path = m_confPath + "/" + table;

	string data;
	DB_FS::readFile(path, data);
	if (data == "")
		data = "[]";

	yyjson_read_err yy_err = { 0 };
	yyjson_mut_doc* yy_mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_doc* yy_doc = yyjson_read_opts(
		(char*)data.data(),
		data.length(),
		YYJSON_READ_NOFLAG,
		NULL,
		&yy_err
	);
	if (!yy_doc) {
		err = JSON_STR_VAL("wrong table format,json parse error");
		return;
	}

	yy_mdoc = yyjson_doc_mut_copy(yy_doc, nullptr);
	yyjson_mut_val* yy_mroot = yyjson_mut_doc_get_root(yy_mdoc);

	CONDITION_SELECTOR cs;
	cs.init(match);
	
	size_t len = yyjson_mut_arr_size(yy_mroot);
	for (size_t i = len - 1; i != (size_t)-1; i--) {
		yyjson_mut_val* obj = yyjson_mut_arr_get(yy_mroot, i);
		if (cs.match(obj)) {
			yyjson_mut_arr_remove(yy_mroot, i);
		}
	}

	char* p = yyjson_mut_val_write(yy_mroot, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
	if (p) {
		DB_FS::writeFile(path, p, len);
		free(p);
	}
	yyjson_doc_free(yy_doc);
	yyjson_mut_doc_free(yy_mdoc);
	rlt = DB_OK;
}


void merge_recursive_yyjson_obj(yyjson_mut_val* yy_mut_obj,yyjson_mut_doc* yy_mut_doc, yyjson_val* yyv_obj) {
	yyjson_val* key, * val;
	size_t idx, max;
	yyjson_obj_foreach(yyv_obj, idx, max, key, val) {
		const char* sk = yyjson_get_str(key);
		yyjson_mut_val* yy_mut_val = yyjson_val_mut_copy(yy_mut_doc, val);
		yyjson_mut_val* yy_mut_key = yyjson_mut_strcpy(yy_mut_doc, sk);
		if (yyjson_is_obj(val)) {
			yyjson_mut_val* orig_obj = yyjson_mut_obj_get(yy_mut_obj,sk);
			if (yyjson_mut_is_obj(orig_obj)) {
				merge_recursive_yyjson_obj(orig_obj, yy_mut_doc, val);
			}
			else {
				yyjson_mut_obj_put(yy_mut_obj, yy_mut_key, yy_mut_val);
			}
		}
		else
			yyjson_mut_obj_put(yy_mut_obj, yy_mut_key, yy_mut_val);
	}
}

void TDB::rpc_db_table_update(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language)
{
	yyjson_val* yyv_tableType = yyjson_obj_get(params, "tableType");
	yyjson_val* yyv_table = yyjson_obj_get(params, "table");
	yyjson_val* yyv_match = yyjson_obj_get(params, "match");
	yyjson_val* yyv_row = yyjson_obj_get(params, "row");
	if (!yyv_row) {
		err = JSON_STR_VAL("must specify row");
		return;
	}
	if (!yyv_match) {
		err = JSON_STR_VAL("must specify match");
		return;
	}
	string tableType = yyjson_get_str(yyv_tableType);
	string table = yyjson_get_str(yyv_table);
	string match = yyjson_get_str(yyv_match);
	string path = m_confPath + "/" + table;

	string data;
	DB_FS::readFile(path, data);
	if (data == "")
		data = "[]";

	yyjson_read_err yy_err = { 0 };
	yyjson_mut_doc* yy_mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_doc* yy_doc = yyjson_read_opts(
		(char*)data.data(),
		data.length(),
		YYJSON_READ_NOFLAG,
		NULL,
		&yy_err
	);
	if (!yy_doc) {
		err = JSON_STR_VAL("wrong table format,json parse error");
		return;
	}
	yy_mdoc = yyjson_doc_mut_copy(yy_doc, nullptr);
	yyjson_mut_val* yy_mroot = yyjson_mut_doc_get_root(yy_mdoc);

	CONDITION_SELECTOR cs;
	cs.init(match);

	size_t len = yyjson_mut_arr_size(yy_mroot);
	for (size_t i = len - 1; i != (size_t)-1; i--) {
		yyjson_mut_val* obj = yyjson_mut_arr_get(yy_mroot, i);
		if (cs.match(obj)) {
			merge_recursive_yyjson_obj(obj,yy_mdoc, yyv_row);
		}
	}

	char* p = yyjson_mut_val_write(yy_mroot, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
	if (p) {
		DB_FS::writeFile(path, p, len);
		free(p);
	}
	yyjson_doc_free(yy_doc);
	yyjson_mut_doc_free(yy_mdoc);
	rlt = DB_OK;
}

void TDB::rpc_db_table_select(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language)
{
	yyjson_val* yyv_tableType = yyjson_obj_get(params, "tableType");
	yyjson_val* yyv_table = yyjson_obj_get(params, "table");
	yyjson_val* yyv_match = yyjson_obj_get(params, "match");
	
	string tableType = yyjson_get_str(yyv_tableType);
	string table = yyjson_get_str(yyv_table);
	string match;
	if(yyv_match)
	 match = yyjson_get_str(yyv_match);
	string path = m_confPath + "/" + table;

	string data;
	DB_FS::readFile(path, data);
	if (data == "")
		data = "[]";

	yyjson_read_err yy_err = { 0 };
	yyjson_mut_doc* yy_mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_doc* yy_doc = yyjson_read_opts(
		(char*)data.data(),
		data.length(),
		YYJSON_READ_NOFLAG,
		NULL,
		&yy_err
	);
	if (!yy_doc) {
		err = JSON_STR_VAL("wrong table format,json parse error");
		return;
	}
	yy_mdoc = yyjson_doc_mut_copy(yy_doc, nullptr);
	yyjson_mut_val* yy_mroot = yyjson_mut_doc_get_root(yy_mdoc);
	yyjson_mut_val* yy_selected = yyjson_mut_arr(yy_mdoc);

	if (match != "") {
		CONDITION_SELECTOR cs;
		cs.init(match);
		size_t len = yyjson_mut_arr_size(yy_mroot);
		for (size_t i = len - 1; i != (size_t)-1; i--) {
			yyjson_mut_val* obj = yyjson_mut_arr_get(yy_mroot, i);
			if (cs.match(obj)) {
				yyjson_mut_arr_append(yy_selected, obj);
			}
		}
	}
	else {
        yy_selected = yy_mroot;
	}

	size_t len;
	char* p = yyjson_mut_val_write(yy_selected, YYJSON_WRITE_PRETTY_TWO_SPACES, &len);
	if (p) {
        rlt = p;
		free(p);
	}

	yyjson_doc_free(yy_doc);
	yyjson_mut_doc_free(yy_mdoc);
}

void TDB::rpc_db_insert(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language) {
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	rpc_db_insert(yyv_params, rlt, err, queryInfo, org,language);
	yyjson_doc_free(doc);
}

void TDB::rpc_db_insert(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language) {
	yyjson_val* yyv_val = yyjson_obj_get(params, "val");
	yyjson_val* yyv_file = yyjson_obj_get(params, "file");
	if (yyv_val == nullptr && yyv_file == nullptr)
	{
		err = JSON_STR_VAL("one of param val or file must be specified");
	}
	else
	{
		yyjson_val* yyv_tag = yyjson_obj_get(params, "tag");
		string tag = yyjson_get_str(yyv_tag);
		DB_TIME tNow;
		yyjson_val* yyv_time = yyjson_obj_get(params, "time");
		if (yyv_time) {
			string time = yyjson_get_str(yyv_time);
			if (time.length() == 10) { // 2020-11-11 11:11:11 支持按照日期插入，按日期插入时，当作0点时候插入
				time += " 00:00:00";
			}

			if (!tNow.fromStr(time)) {
				err = JSON_STR_VAL("param time invalid format.");
				return;
			}
		}
		else {
			tNow.setNow();
		}

		yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* yymv_params = yyjson_val_mut_copy(mut_doc, params);
		yyjson_mut_obj_remove_key(yymv_params, "tag");

		string sDe;

		size_t len = 0;
		char* json_str = yyjson_mut_val_write(yymv_params, YYJSON_WRITE_NOFLAG, &len);
		if (json_str) {
			sDe = json_str;
			free(json_str);
		}

		yyjson_mut_doc_free(mut_doc);

		yyjson_val* yyv_db = yyjson_obj_get(params, "db");

		if (yyjson_is_str(yyv_db)) {
			string dbName = yyjson_get_str(yyv_db);
			TDB* tdb = db.getChildDB(dbName);
			tdb->Insert(tag, sDe, &tNow);
		}
		else
			Insert(tag, sDe, &tNow);
		rlt = "\"ok\"";
	}
}


void TDB::InsertValJsonStr(string strTag, DB_TIME stTime, string& sVal)
{
	string folderPath = getPath_dataFolder(strTag, stTime);
	string dlPath = folderPath + "/" + m_dbFmt.deListName;
	if (!fileExist(folderPath))
		DB_FS::createFolderOfPath(folderPath.c_str());



	if (m_bEnableFsBuff) {
		bool bAppend = false;
		m_FsBuff.m_csFsb.lock();
		std::map<string, FILE_BUFF*>::iterator iter = m_FsBuff.m_mapFsBuff.find(dlPath);
		if (iter != m_FsBuff.m_mapFsBuff.end()) {
			string& fileData = iter->second->data;  // can be an empty file ,length is 0
			if (fileData.size() > 0) {
				fileData.resize(fileData.size() - 1);
				fileData += ",{\n  \"time\":\"" + stTime.toStr() + "\",\n    \"" + m_dbFmt.deItemKey_value + "\":" + sVal + "\n}]";;
			}
			else {
				fileData = "[{\n  \"time\":\"" + stTime.toStr() + "\",\n  \"" + m_dbFmt.deItemKey_value + "\":" + sVal + "\n}\n]";
			}
		}
		m_FsBuff.m_csFsb.unlock();
	}



	bool bAppend = false;
	if (fileExist(dlPath))
	{
		string appendData = ",{\n  \"time\":\"" + stTime.toStr() + "\",\n    \"" + m_dbFmt.deItemKey_value + "\":" + sVal + "\n}]";
#ifdef _WIN32
		FILE* fp = _wfopen(DB_STR::utf8_to_utf16(dlPath).c_str(), L"rb+");
#else
		FILE* fp = fopen(dlPath.c_str(), "rb+");
#endif
		if (fp)
		{
			fseek(fp, 0L, SEEK_END);
			long len = ftell(fp);
			if (len > 0)
			{
				fseek(fp, len - 1, SEEK_SET);  //overwrite last ] charactor
				fwrite(appendData.c_str(), 1, appendData.length(), fp);
				bAppend = true;
			}
			fclose(fp);
		}
	}
	if (!bAppend) {
		string s = "[{\n  \"time\":\"" + stTime.toStr() + "\",\n  \"" + m_dbFmt.deItemKey_value + "\":" + sVal + "\n}\n]";
		if (!DB_FS::writeFile(dlPath, (unsigned char*)s.c_str(), s.length()))
		{
			printf("[error]save to db file fail,path:%s,data:%s", dlPath.c_str(), s.c_str());
		}
	}
}

void TDB::Insert(string strTag, DB_TIME stTime, int& iVal)
{
	string s = formatStr("%d", iVal);
	InsertValJsonStr(strTag, stTime, s);
}

void TDB::Insert(string strTag, DB_TIME stTime, long long iVal)
{
	string s = formatStr("%d", iVal);
	InsertValJsonStr(strTag, stTime, s);
}

void TDB::Insert(string strTag, DB_TIME stTime, double& dbVal)
{
	string s = formatStr("%f", dbVal);
	InsertValJsonStr(strTag, stTime, s);
}
void TDB::rpc_db_merge(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language) {
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	rpc_db_merge(yyv_params, rlt, err, queryInfo, org, language);
	yyjson_doc_free(doc);
}

void TDB::rpc_db_merge(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language) {
	string dbName;
	TDB* tdb = nullptr;
	yyjson_val* yyv_db = nullptr;
	yyjson_val* yyTag = nullptr;
	yyjson_val* yyTime = nullptr;

	size_t idx, maxIdx;
	yyjson_val* key, * value;
	std::multimap<string, yyjson_val*> mapParams;
	yyjson_obj_foreach(params, idx, maxIdx, key, value) {
		string sKey = yyjson_get_str(key);
		if (sKey == "db") yyv_db = value;
		else if (sKey == "tag") yyTag = value;
		else if (sKey == "time") yyTime = value;
		else mapParams.insert(std::pair<string, yyjson_val*>(sKey, value));
	}

	if (yyjson_is_str(yyv_db)) {
		dbName = yyjson_get_str(yyv_db);
		tdb = db.getChildDB(dbName);
		if (tdb == nullptr) {
			err = "specified db not found";
			return;
		}
	}

	if (!yyjson_is_str(yyTag)) {
		err = JSON_STR_VAL("specify tag in string format");
		return;
	}

	if (!yyjson_is_str(yyTime)) {
		err = JSON_STR_VAL("specify time in string format");
		return;
	}

	string tag = yyjson_get_str(yyTag);
	string timerange = yyjson_get_str(yyTime);
	string time[2];
	DB_TIME dbTime[2];

	auto pos = timerange.find('~');
	if (pos != string::npos)
	{
		time[0] = timerange.substr(0, pos);
		time[1] = timerange.substr(pos + 1);
		dbTime[0].fromStr(timerange.substr(0, pos));
		dbTime[1].fromStr(timerange.substr(pos + 1));
	}
	else
	{
		time[0] = time[1] = timerange;
		dbTime[1].fromStr(timerange);
		dbTime[0] = dbTime[1];
	}

	if (time[0].length() != 19 && time[0].length() != 23 && time[1].length() != 19 && time[1].length() != 23) {
		err = JSON_STR_VAL("wrong timerange format,should be XXXX-XX-XX XX:XX:XX or XXXX-XX-XX XX:XX:XX.XXX or with '~'");
		return;
	}
	if (time[0] > time[1]) {
		err = JSON_STR_VAL("wrong timerange format,should be mintime~maxtime");
		return;
	}

	int mergeRet = 0;

	DB_TIME dtBegin = dbTime[0];
	DB_TIME dtEnd = dbTime[1];
	dtBegin.clearHMS();
	dtEnd.setMaxHMS();

	DB_TIME oneDay{ 0, 0, 1, 0, 0, 0, 0 };

	for (DB_TIME dt = dtBegin; dt <= dtEnd; dt += oneDay)
	{
		if (tdb) {
			mergeRet = tdb->Merge(tag, dt, dbTime[0], dbTime[1], mapParams);
		}
		else {
			mergeRet = Merge(tag, dt, dbTime[0], dbTime[1], mapParams);
		}
	}

	if (mergeRet == 0) {
		rlt = JSON_STR_VAL("ok");
	}
	else {
		err = JSON_STR_VAL("merge fail,code: " + to_string(mergeRet));
	}
}

int TDB::Merge(string tag, const DB_TIME& stTime, const DB_TIME& stTimeRange1, const DB_TIME& stTimeRange2, const std::multimap<string, yyjson_val*>& mMergeParams)
{
	string dbFile = getPath_dbFile(tag, stTime);
	string dbData;
	DB_FS::readFile(dbFile, dbData);
	if (dbData == "")
		return -1;

	yyjson_doc* doc = yyjson_read(dbData.c_str(), dbData.length(), 0);

	yyjson_mut_doc* mut_doc = yyjson_doc_mut_copy(doc, nullptr);
	yyjson_mut_val* mut_root = yyjson_mut_doc_get_root(mut_doc);

	yyjson_mut_val* deList = nullptr;
	yyjson_type type = yyjson_mut_get_type(mut_root);
	if (type == YYJSON_TYPE_OBJ) { //file with desc
		deList = yyjson_mut_obj_get(mut_root, "data");
	}
	else if (type == YYJSON_TYPE_ARR) {
		deList = mut_root;
	}
	else {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return -2;
	}

	bool findDE = false;
	string deTime = stTime.toYMD() + " 00:00:00.000";
	string updateTime1 = stTimeRange1.toStr();
	string updateTime2 = stTimeRange2.toStr();
	size_t idx, max;
	yyjson_mut_val* de;
	//the file contont and url to be updated
	yyjson_mut_arr_foreach(deList, idx, max, de) {
		yyjson_mut_val* yyTime = yyjson_mut_obj_get(de, "time");
		getDeTime(yyTime, deTime);
		if (updateTime1 <= deTime && deTime <= updateTime2) {
			//replace the "val", update the file urls, refresh the file dir
			for (auto &it : mMergeParams)
			{
				yyjson_mut_val* yyValKey = yyjson_mut_strcpy(mut_doc, it.first.c_str());
				//param is null, remove the key
				if (yyjson_is_null(it.second))
				{
					yyjson_mut_obj_remove(de, yyValKey);
				} 
				else
				{
					yyjson_mut_val* yyToMergeVal = yyjson_val_mut_copy(mut_doc, it.second);
		
					yyjson_mut_obj_put(de, yyValKey, yyToMergeVal);
				}
			}
			findDE = true;
		}
	}
	if (!findDE) {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return -3;
	}

	size_t len = 0;
	char* p = yyjson_mut_write(mut_doc, 0, &len);
	if (p) {
		DB_FS::writeFile(dbFile, p, len);
		free(p);
	}

	yyjson_mut_doc_free(mut_doc);
	yyjson_doc_free(doc);

	return 0;
}



void TDB::rpc_db_update(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language) {
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	rpc_db_update(yyv_params, rlt, err, queryInfo, org,language);
	yyjson_doc_free(doc);
}
void TDB::rpc_db_update(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language) {
	string dbName;
	TDB* tdb = nullptr;
	yyjson_val* yyv_db = yyjson_obj_get(params, "db");
	if (yyjson_is_str(yyv_db)) {
		dbName = yyjson_get_str(yyv_db);
		tdb = db.getChildDB(dbName);
		if (tdb == nullptr) {
			err = "specified db not found";
			return;
		}
	}

	yyjson_val* yyTag = yyjson_obj_get(params, "tag");
	if (!yyjson_is_str(yyTag)) {
		err = JSON_STR_VAL("specify tag in string format");
		return;
	}

	yyjson_val* yyTime = yyjson_obj_get(params, "time");
	if (!yyjson_is_str(yyTime)) {
		err = JSON_STR_VAL("specify time in string format");
		return;
	}

	yyjson_val* updateVal = yyjson_obj_get(params, "val");

	string tag = yyjson_get_str(yyTag);
	string time = yyjson_get_str(yyTime);

	if (time.length() != 19 && time.length() != 23) {
		err = JSON_STR_VAL("wrong time format,should be XXXX-XX-XX XX:XX:XX or XXXX-XX-XX XX:XX:XX.XXX");
		return;
	}

	yyjson_val* updateFile = yyjson_obj_get(params, "file");

	DB_TIME dbTime;
	dbTime.fromStr(time);
	int updateRet = 0;

	if (tdb) {
		updateRet =tdb->Update(tag, dbTime, updateVal, updateFile);
	}
	else {
		updateRet = Update(tag, dbTime, updateVal, updateFile);
	}

	if (updateRet==0) {
		rlt = JSON_STR_VAL("ok");
	}
	else {
		err = JSON_STR_VAL("update fail,code: "+to_string(updateRet));
	}
}

int TDB::Update(string tag, DB_TIME stTime, yyjson_val* yyVal, yyjson_val* updateFileParam)
{
	string dbFile = getPath_dbFile(tag, stTime);
	string dbData;
	DB_FS::readFile(dbFile, dbData);
	if (dbData == "")
		return -1;

	yyjson_doc* doc = yyjson_read(dbData.c_str(), dbData.length(), 0);

	yyjson_mut_doc* mut_doc = yyjson_doc_mut_copy(doc,nullptr);
	yyjson_mut_val* mut_root = yyjson_mut_doc_get_root(mut_doc);

	yyjson_mut_val* deList = nullptr;
	yyjson_type type = yyjson_mut_get_type(mut_root);
	if (type == YYJSON_TYPE_OBJ) { //file with desc
		deList = yyjson_mut_obj_get(mut_root, "data");
	}
	else if (type == YYJSON_TYPE_ARR) {
		deList = mut_root;
	}
	else {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return -2;
	}

	bool findDE = false;
	string deTime = stTime.toYMD() + " 00:00:00.000";
	string updateTime = stTime.toStr();
	size_t idx, max;
	yyjson_mut_val* de;
	//the file contont and url to be updated
	//yyjson_val* yyFileToUpdate = NULL;  string dbFile1;
	struct SToBeUpdatedFile { yyjson_val* yyFileToUpdate = NULL;  string dbFile1; };
	vector<SToBeUpdatedFile > vecToBeUpdatedFile; string theDir;
	int nSomeWrong = 0;
	bool bEmptyAry = false;
	yyjson_mut_arr_foreach(deList, idx, max, de) {
		yyjson_mut_val* yyTime = yyjson_mut_obj_get(de, "time");
		getDeTime(yyTime, deTime);
		if (updateTime == deTime) {
			//replace the "val", update the file urls, refresh the file dir
			yyjson_mut_val* yyValKey = yyjson_mut_strcpy(mut_doc, "val");
			yyjson_mut_val* yyToUpdateValNew = yyjson_val_mut_copy(mut_doc, yyVal);
			yyjson_mut_val* yyFileKey = yyjson_mut_strcpy(mut_doc, "file");
			yyjson_mut_val* yyToUpdateFiNew = yyjson_mut_obj_get(de, "file");

			
			if (updateFileParam && !findDE) { //only once
				if (yyjson_is_arr(updateFileParam)) { //jpg or other files 
					
					int pos = dbFile.rfind("/");
					string folder;
					if (pos > 0) {
						if (m_timeUnit== BY_DAY) { //db.json path
							folder = dbFile.substr(0, pos + 1) + stTime.toStampHMS() + "/";
						}
						else {
							folder = dbFile.substr(0, pos + 1) + stTime.toStampFull() + "/";
						}
						theDir = folder;
					}
					else {
						nSomeWrong = -10;
						break;
					}

					size_t size = yyjson_arr_size(updateFileParam);
					if (size > 0) {
						yyjson_mut_arr_clear(yyToUpdateFiNew);
						size_t idx1, max1;
						yyjson_val* val1;
						yyjson_arr_foreach(updateFileParam, idx1, max1, val1) {
							SToBeUpdatedFile one;
							one.yyFileToUpdate = yyjson_obj_get(val1, "data");
							string name = yyjson_get_str(yyjson_obj_get(val1, "name"));
							string type = yyjson_get_str(yyjson_obj_get(val1, "type"));
							one.dbFile1 = folder + name;
							vecToBeUpdatedFile.push_back(one);

							string strURL = getPath_dataFolder_NO_DB(tag, stTime);
							if (m_timeUnit == BY_DAY) { //db.json path
								strURL = "/" + strURL + "/" + stTime.toStampHMS() + "/" + name;
							}
							else {
								strURL = "/" + strURL + "/" + stTime.toStampFull() + "/" + name;
							}

							auto jOne = yyjson_mut_obj(mut_doc);
							yyjson_mut_obj_add_strcpy(mut_doc, jOne, "name", name.c_str());
							yyjson_mut_obj_add_strcpy(mut_doc, jOne, "type", type.c_str());
							string urlAbs = "/db";
							if (m_name != "")
								urlAbs += "/" + m_name;
							urlAbs += strURL;
							yyjson_mut_val* urlKey = yyjson_mut_strcpy(mut_doc, "url");
							yyjson_mut_val* urlVal = yyjson_mut_strcpy(mut_doc, urlAbs.c_str());
							yyjson_mut_obj_put(jOne, urlKey, urlVal);

							yyjson_mut_arr_add_val(yyToUpdateFiNew, jOne);
						}
						yyjson_mut_obj_put(de, yyFileKey, yyToUpdateFiNew);
					}
					else {
						bEmptyAry = true;
						yyjson_mut_arr_clear(yyToUpdateFiNew);
						yyjson_mut_obj_put(de, yyFileKey, yyToUpdateFiNew);
					}
				}
				else if (yyjson_is_obj(updateFileParam)) {
					SToBeUpdatedFile one;
					one.yyFileToUpdate = yyjson_obj_get(updateFileParam, "data");
					int pos =dbFile.rfind("/"); //the "db.json" url
					string folder;
					if (pos > 0) {
						folder = dbFile.substr(0, pos+1);
					} else{
						nSomeWrong = -10;
						break;
					}
					if ((int)dbFile.rfind(m_dbFmt.curveIdxListName)>0) {
						one.dbFile1 = folder + stTime.toStampHMS() + m_dbFmt.curveDeNameSuffix;
					} 
					else {
						nSomeWrong = -11;
						break;
					}
					vecToBeUpdatedFile.push_back(one);

					yyjson_mut_val* yymv_dataFile = yyjson_mut_obj_get(de, "file");
					yyjson_mut_obj_remove_key(yymv_dataFile, "data");
					
				} 
				else {
					nSomeWrong = -12;
				}
			}
			yyjson_mut_obj_put(de, yyValKey, yyToUpdateValNew);
			findDE = true;
		}
	}
	if (!findDE) {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return -3;
	}
	else if (nSomeWrong != 0) {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return nSomeWrong;
	}

	size_t len = 0;
	char* p = yyjson_mut_write(mut_doc, 0, &len);
	if (p) {
		DB_FS::writeFile(dbFile, p, len);
		free(p);
	}

	if (vecToBeUpdatedFile.size()>0) {
		//refresh the entire files dir  or one file ,  update the file urls
		if (theDir != "") {
			theDir = DB_STR::utf8_to_gb(theDir);
			DB_FS::deleteDirectory(theDir);
			for (auto one : vecToBeUpdatedFile) {
				string p;
				if (yyjson_is_str(one.yyFileToUpdate))
					p = yyjson_get_str(one.yyFileToUpdate);
				else continue;

				size_t buffLen = p.length() * 2;
				unsigned char* out = new unsigned char[buffLen];
				memset(out, 0, buffLen);
				int outLen = tdb_base64_decode(p.c_str(), p.length(), out);
				DB_FS::writeFile(one.dbFile1, out, outLen);
				delete[] out;
			}
		}
		else {
			p = yyjson_val_write(vecToBeUpdatedFile[0].yyFileToUpdate, 0, &len);
			if (p) {
				DB_FS::writeFile(vecToBeUpdatedFile[0].dbFile1, p, len);
				free(p);
			}
		}
	}
	else {
		if (bEmptyAry) {
			theDir = DB_STR::utf8_to_gb(theDir);
			DB_FS::deleteDirectory(theDir);
		}
	}

	yyjson_mut_doc_free(mut_doc);
	yyjson_doc_free(doc);

	return 0;
}


void TDB::rpc_db_saveImage(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language) {
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	yyjson_val* yyv_tag = yyjson_obj_get(yyv_params, "tag");

	if (yyv_tag == nullptr) {
		err = JSON_STR_VAL("tag must be specified");
		yyjson_doc_free(doc);

		return;
	}

	yyjson_val* yyv_time = yyjson_obj_get(yyv_params, "time");
	if (yyv_time == nullptr) {
		err = JSON_STR_VAL("time must be specified");
		yyjson_doc_free(doc);

		return;
	}

	yyjson_val* yyv_img = yyjson_obj_get(yyv_params, "data");
	yyjson_val* yyv_info = yyjson_obj_get(yyv_params, "info");
	yyjson_val* yyv_index = yyjson_obj_get(yyv_params, "index");

	string tag = yyjson_get_str(yyv_tag);
	string time = yyjson_get_str(yyv_time);

	string strIndex = yyv_index ? yyjson_get_str(yyv_index) : "";

	DB_TIME t;
	t.fromStr(time);

	if (yyv_img && yyv_info){
		string img = yyjson_get_str(yyv_img);
		string info = yyjson_get_str(yyv_info);

		string& data = img;

		//copatiable with DATA URI Scheme like data:image/jpg;base64,XINGSXXIANGJIJIGSAG== 
		size_t startPos = 0;
		if (data.find("data:") == 0) {
			startPos = data.find(",");
			if (startPos == string::npos) {
				yyjson_doc_free(doc);
				return ;
			}

			startPos += 1;
		}

		size_t buffLen = data.length() * 2;
		unsigned char* out = new unsigned char[buffLen];
		memset(out, 0, buffLen);

		int outLen = tdb_base64_decode(data.c_str() + startPos, data.length() - startPos, out);
		saveImage(tag, t, (char*)out, outLen, info, strIndex);

		delete[] out;
		rlt = "\"image info and data saved\"";
	}
	else if (yyv_img) {
		string img = yyjson_get_str(yyv_img);
		string info = "";

		string& data = img;

		//copatiable with DATA URI Scheme like data:image/jpg;base64,XINGSXXIANGJIJIGSAG== 
		size_t startPos = 0;
		if (data.find("data:") == 0) {
			startPos = data.find(",");
			if (startPos == string::npos) {
				yyjson_doc_free(doc);
				return;
			}

			startPos += 1;
		}

		size_t buffLen = data.length() * 2;
		unsigned char* out = new unsigned char[buffLen];
		memset(out, 0, buffLen);

		int outLen = tdb_base64_decode(data.c_str() + startPos, data.length() - startPos, out);
		saveImage(tag, t, (char*)out, outLen, info, strIndex);

		delete[] out;
		rlt = "\"image data saved\"";
	}
	else if (yyv_info) {
		string info = yyjson_get_str(yyv_info);
		saveImage(tag, t, NULL, 0, info, strIndex);

		rlt = "\"image info saved\"";
	}
	else {
		err = "\"set image info or data in rpc request\"";
	}
	
	yyjson_doc_free(doc);
}

void TDB::rpc_db_getBufferStatus(string& rlt, string& err) {
	m_FsBuff.m_csFsb.lock();
	size_t fileCount = m_FsBuff.m_mapFsBuff.size();
	size_t bufferSize = 0;
	for (auto& iter : m_FsBuff.m_mapFsBuff) {
		bufferSize += iter.second->data.length();
	}
	rlt = DB_STR::format("{\"fileCount\":%d,\"bufferSize\":%d,\"bufferTTL\":%d}",fileCount,bufferSize,m_bufferTTL);
	m_FsBuff.m_csFsb.unlock();
	return;
}


void TDB::rpc_db_setConf(string& sParams, string& rlt, string& err) {
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	
	yyjson_val* yyv_buffer_ttl = yyjson_obj_get(yyv_params, "bufferTTL");
	if (yyv_buffer_ttl) {
		m_bufferTTL = yyjson_get_int(yyv_buffer_ttl);
	}
	yyjson_doc_free(doc);
}

void TDB::rpc_db_delete(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language) {
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	rpc_db_delete(yyv_params, rlt, err, queryInfo, org,language);
	yyjson_doc_free(doc);
}
void TDB::rpc_db_delete(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language) {
	string dbName;
	TDB* tdb = nullptr;
	yyjson_val* yyv_db = yyjson_obj_get(params, "db");
	if (yyjson_is_str(yyv_db)) { 
		dbName = yyjson_get_str(yyv_db);
		tdb = db.getChildDB(dbName);
		if (tdb == nullptr) {
			err = "specified db not found";
			return;
		}
	}

	yyjson_val* yyTag = yyjson_obj_get(params, "tag");
	if (!yyjson_is_str(yyTag)) {
		err = JSON_STR_VAL("specify tag in string format");
		return;
	}

	yyjson_val* yyTime = yyjson_obj_get(params, "time");
	if (!yyjson_is_str(yyTime)) {
		err = JSON_STR_VAL("specify time in string format");
		return;
	}

	string tag = yyjson_get_str(yyTag);
	string time = yyjson_get_str(yyTime);
	DB_TIME dbTime;
	dbTime.fromStr(time);
	bool ret = false;

	if (tdb) {
		ret = tdb->Delete(tag, dbTime);
	}
	else {
		Delete(tag, dbTime);
	}

	if (ret) {
		rlt = JSON_STR_VAL("ok");
	}
	else {
		err = JSON_STR_VAL("delete fail");
	}
}

bool TDB::Delete(string tag, DB_TIME stTime)
{
	string dbFile = getPath_dbFile(tag, stTime);
	string dbData;
	DB_FS::readFile(dbFile, dbData);
	if (dbData == "")
		return false;

	yyjson_doc* doc = yyjson_read(dbData.c_str(), dbData.length(), 0);

	yyjson_mut_doc* mut_doc = yyjson_doc_mut_copy(doc, nullptr);
	yyjson_mut_val* mut_root = yyjson_mut_doc_get_root(mut_doc);

	yyjson_mut_val* deList = nullptr;
	yyjson_type type = yyjson_mut_get_type(mut_root);
	if (type == YYJSON_TYPE_OBJ) { //file with desc
		deList = yyjson_mut_obj_get(mut_root, "data");
	}
	else if (type == YYJSON_TYPE_ARR) {
		deList = mut_root;
	}
	else {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return false;
	}

	bool findDE = false;
	size_t toDeleteIdx = 0;
	string deTime = stTime.toYMD() + " 00:00:00.000";
	string updateTime = stTime.toStr();
	size_t idx, max;
	yyjson_mut_val* de=NULL;
	yyjson_mut_arr_foreach(deList, idx, max, de) {
		yyjson_mut_val* yyTime = yyjson_mut_obj_get(de, "time");
		getDeTime(yyTime, deTime);
		if (updateTime == deTime) {
			findDE = true;
			toDeleteIdx = idx;
		}
	}
	if (!findDE) {
		yyjson_mut_doc_free(mut_doc);
		yyjson_doc_free(doc);

		return false;
	}

	//if match, must only match one 
	//delete the attachments
	if (de) {
		yyjson_mut_val* pFile = yyjson_mut_obj_get(de, "file");
		if (pFile && yyjson_mut_is_arr(pFile)) {
			string strPath = getPath_dataFolder(tag, stTime);
			if (m_timeUnit == BY_DAY) { //db.json path
				strPath =  strPath + "/" + stTime.toStampHMS() + "/";
			}
			else {
				strPath =  strPath + "/" + stTime.toStampFull() + "/";
			}

			strPath = DB_STR::utf8_to_gb(strPath);
			DB_FS::deleteDirectory(strPath);
		}
	}

	if (max == 1) { //only one de in list and is deleted ,remove file.when left [] in db.json, db.insert will execulte as insert mode,a period will inserted after [,this causes error json file
		DB_FS::deleteFile(dbFile);
	}
	else {
		yyjson_mut_arr_remove(deList, toDeleteIdx);

		size_t len = 0;
		char* p = yyjson_mut_write(mut_doc, 0, &len);
		if (p) {
			DB_FS::writeFile(dbFile, p, len);
			free(p);
		}
	}

	yyjson_mut_doc_free(mut_doc);
	yyjson_doc_free(doc);

	return true;
}

bool TDB::Count(string tag, TIME_SELECTOR& timeSelector, string filter, int& iCount)
{
	return false;
}

string TDB::saveDEFile(yyjson_val* yyvFileInfo,string path,DB_TIME dbTime, string& type)
{
	string deFilePath = "";

	yyjson_val* yyv_name = yyjson_obj_get(yyvFileInfo, "name");
	yyjson_val* yyv_type = yyjson_obj_get(yyvFileInfo, "type");
	yyjson_val* yyv_data = yyjson_obj_get(yyvFileInfo, "data");
	if (!yyv_type)return "";
	if (!yyv_data)return "";

	string name;
	if(yyv_name)name = yyjson_get_str(yyv_name);
	
	type = yyjson_get_str(yyv_type);

	if (type == "curve") {
		name = dbTime.toStampHMS() + m_dbFmt.curveDeNameSuffix;
	}

	string data;
	if (yyjson_is_str(yyv_data)) {
		const char* pData = yyjson_get_str(yyv_data);
		int ilen = strlen(pData);
		data = pData;
	}
	else {
		char* p = yyjson_val_write(yyv_data, 0, nullptr);
		if (p) {
			data = p;
			free(p);
		}
	}
	
	deFilePath = path + "/" + name;
	//encoded to base64 by default
	if (type.find("jpg")!=string::npos || type.find("grh") != string::npos ||
		type.find("png")!=string::npos ||
		type.find("svg")!=string::npos){
		//copatiable with DATA URI Scheme like data:image/jpg;base64,XINGSXXIANGJIJIGSAG== 
		size_t startPos = 0;
		if (data.find("data:") == 0) {
			startPos = data.find(",");
			if (startPos == string::npos) {
				return "";
			}

			startPos += 1;
		}

		size_t buffLen = data.length() * 2;
		unsigned char* out = new unsigned char[buffLen];
		memset(out, 0, buffLen);
		int outLen = tdb_base64_decode(data.c_str() + startPos, data.length() - startPos, out);

		DB_FS::writeFile(deFilePath, out, outLen);
		delete [] out;
	}
	else if (type == "text") {  //text file is not encoded 
		DB_FS::writeFile(deFilePath, (char*)data.c_str(), data.length());
	}
	else if (type == "curve") {  //curve file is not encoded 
		DB_FS::writeFile(deFilePath, (char*)data.c_str(), data.length());
	}
	else {
		size_t buffLen = data.length() * 2;
		unsigned char* out = new unsigned char[buffLen];
		memset(out, 0, buffLen);
		int outLen = tdb_base64_decode(data.c_str(), data.length(), out);
		DB_FS::writeFile(deFilePath, out, outLen);
		delete [] out;
	}

	return deFilePath;
}


bool TDB::Open(string strDBUrl,fp_getTagsByTagSelector f,string name)
{
	if (strDBUrl == "")
		return false;

	m_path = replaceStr(strDBUrl,"\\","/");
	m_name = name;
	m_getTagsByTagSelector = f;
	return true;
}

bool TDB::Open_gbk(string strDBUrl, fp_getTagsByTagSelector f, string name)
{
	strDBUrl = DB_STR::gb_to_utf8(strDBUrl);
	m_isGbk = true;
	return Open(strDBUrl, f, name);
}

bool TDB::setBufferTTL(string bufferTTL)
{
	int timeLen = TIME_OPT::timeLen2seconds(bufferTTL);
	if (timeLen != 0) {
		m_bufferTTL = timeLen;
	}
	return false;
}

void TDB::parseDESelector(string& sParams, DE_SELECTOR& deSelector, string& err)
{
	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);
	parseDESelector(yyv_params, deSelector,err);
	yyjson_doc_free(doc);
}


// "aggregate":"max"
// 
// "aggregate":{
//	    "max":"max",
//      "min":"min",
//      "avg":"avg"
// }
map<string, vector<string>> TDB::getAggrOpt(yyjson_val* jAggr) {
	map<string, vector<string>> aggrOpt;
	if (yyjson_is_str(jAggr)) { //aggr val in a single tag
		vector<string> aggrTypes;
		string sAggr = yyjson_get_str(jAggr);
		DB_STR::split(aggrTypes, sAggr, ",");
		aggrOpt[m_dbFmt.deItemKey_value] = aggrTypes;
	}
	else if (yyjson_is_obj(jAggr)) { //aggr by each field
		size_t idx, maxIdx;
		yyjson_val* key, * value;
		yyjson_obj_foreach(jAggr, idx, maxIdx, key, value) {
			string sKey = yyjson_get_str(key);
			string sVal = yyjson_get_str(value);
			vector<string> aggrTypes;
			aggrTypes.push_back(sVal);
			aggrOpt[sKey] = aggrTypes;
		}
	}
	return aggrOpt;
}

int TDB::dhmsSpan2Seconds(string timeSpan) {
	string time1 = timeSpan;
	string strDay = "", strH = "", strM = "", strS = "";
	int n1 = 0, n2 = 0, n3 = 0, n4 = 0;
	size_t pos = time1.find("d");
	if (pos == string::npos)
		pos = time1.find("D");
	if (pos != string::npos) {
		strDay = time1.substr(0, pos);
		time1 = time1.erase(0, pos + 1);
		n1 = (int)(atof(strDay.c_str()) * 24 * 3600);
	}
	pos = time1.find("h");
	if (pos == string::npos)
		pos = time1.find("H");
	if (pos != string::npos) {
		strH = time1.substr(0, pos);
		time1 = time1.erase(0, pos + 1);
		n2 = (int)(atof(strH.c_str()) * 3600);
	}
	pos = time1.find("m");
	if (pos == string::npos)
		pos = time1.find("M");
	if (pos != string::npos) {
		strM = time1.substr(0, pos);
		time1 = time1.erase(0, pos + 1);
		n3 = (int)(atof(strM.c_str()) * 60);
	}
	pos = time1.find("s");
	if (pos == string::npos)
		pos = time1.find("S");
	if (pos != string::npos) {
		strS = time1.substr(0, pos);
		time1 = time1.erase(0, pos + 1);
		n4 = (int)atof(strS.c_str());
	}

	return n1 + n2 + n3 + n4;
}

bool TDB::saveImage(string tag, DB_TIME stTime, char* pData, size_t len, string& imgInfo, string sDeIdx)
{
	if (pData)
	{
		//jedge xxxxxx.imageInfo.json file does exist
		//exist		-->	merge info,after that save image && info
		//not exist	--> save image && info
		string imageInfoPath = getPath_dbFile(tag, stTime, "imageInfo");
		if (fileExist(imageInfoPath))
		{
			if (imgInfo == "")
			{
				string info_yuan;
				DB_FS::readFile(imageInfoPath, info_yuan);
				imgInfo = info_yuan;
			}
			else
			{
				auto doc = yyjson_read(imgInfo.c_str(), imgInfo.size(), 0);
				auto mut_doc = yyjson_doc_mut_copy(doc, NULL);
				auto mut_root = yyjson_mut_doc_get_root(mut_doc);
				yyjson_doc_free(doc);

				string info_yuan;
				DB_FS::readFile(imageInfoPath, info_yuan);
				auto doc_yuan = yyjson_read(info_yuan.c_str(), info_yuan.size(), 0);
				auto mut_doc_yuan = yyjson_doc_mut_copy(doc_yuan, NULL);
				auto mut_root_yuan = yyjson_mut_doc_get_root(mut_doc_yuan);
				yyjson_doc_free(doc_yuan);

				yyjson_mut_val* key, * val;
				size_t indx = 0, max = 0;
				yyjson_mut_obj_foreach(mut_root, indx, max, key, val)
				{
					string strKey = yyjson_mut_get_str(key);
					if (!yyjson_mut_obj_get(mut_root_yuan, strKey.c_str()))
					{
						yyjson_mut_obj_add_val(mut_doc_yuan, mut_root_yuan, strKey.c_str(), yyjson_mut_val_mut_copy(mut_doc_yuan, val));
					}
				}

				char* strTemp = yyjson_mut_write(mut_doc_yuan, 0, 0);
				if (strTemp) {
					imgInfo = strTemp;
					free(strTemp);
				}
				
				yyjson_mut_doc_free(mut_doc);
				yyjson_mut_doc_free(mut_doc_yuan);
			}

			DB_FS::deleteFile(imageInfoPath);
		}

		bool bInfoEmpty = (imgInfo == "");

		//jjpg = jpg + string + string.size + "jjpg"
		string path = getPath_dbFile(tag, stTime, "image");
		size_t imgInfoSize = imgInfo.size();
		size_t buffLen = len + (bInfoEmpty ? 0 : imgInfoSize + 4 + 4);
		char* buff = new char[buffLen];
		memcpy(buff, pData, len);
		if (!bInfoEmpty)
		{
			memcpy(buff + len, imgInfo.c_str(), imgInfoSize);
			memcpy(buff + len + imgInfoSize, &imgInfoSize, 4);
			memcpy(buff + len + imgInfoSize + 4, "jjpg", 4);
		}

		bool ret = DB_FS::writeFile(path, buff, buffLen);
		delete[] buff;

		//save index list
		if (sDeIdx != "")
		{
			yyjson_doc* doc = yyjson_read(sDeIdx.c_str(), sDeIdx.length(), 0);
			yyjson_mut_doc* mdoc = yyjson_doc_mut_copy(doc, NULL);
			yyjson_mut_val* yymDe = yyjson_mut_doc_get_root(mdoc);
			yyjson_mut_val* timeKey = yyjson_mut_strcpy(mdoc, "time");
			yyjson_mut_val* timeVal;
			string sTime = stTime.toStr(true);
			timeVal = yyjson_mut_strcpy(mdoc, sTime.data());
			yyjson_mut_obj_put(yymDe, timeKey, timeVal);

			string dataListPath;
			string deListFolderPath = getPath_dataFolder(tag, stTime);
			dataListPath = deListFolderPath + "/" + m_dbFmt.deListName;
			saveDeToDataListFile(dataListPath, yymDe);

			yyjson_mut_doc_free(mdoc);
			yyjson_doc_free(doc);
		}
		else
		{
			auto mut_doc = yyjson_mut_doc_new(nullptr);
			auto mut_root = yyjson_mut_obj(mut_doc);
			yyjson_mut_doc_set_root(mut_doc, mut_root);

			string sTime = stTime.toStr(true);
			yyjson_mut_obj_add_strcpy(mut_doc, mut_root, "time", sTime.c_str());
			string dataListPath;
			string deListFolderPath = getPath_dataFolder(tag, stTime);
			dataListPath = deListFolderPath + "/" + m_dbFmt.deListName;
			saveDeToDataListFile(dataListPath, mut_root);

			yyjson_mut_doc_free(mut_doc);
		}

		return ret;
	}
	else if (imgInfo != "")
	{
		string imagePath = getPath_dbFile(tag, stTime, "image");
		string imageInfoPath = getPath_dbFile(tag, stTime, "imageInfo");
		//judge xxxxxx.image.jpg file exist
		//exist		--> add to the end of the image file
		//not exist --> save to xxxxxx.imageInfo.json
		if (fileExist(imagePath))
		{
			string image_yuan;
			DB_FS::readFile(imagePath, image_yuan);
			size_t temp = image_yuan.size();
			string strFileEnd = image_yuan.substr(image_yuan.size() - 4, 4);
			//judge xxxxxx.image.jpg file inside,info does it exist
			if (strFileEnd == "jjpg")
			{
				auto doc = yyjson_read(imgInfo.c_str(), imgInfo.size(), 0);
				auto mut_doc = yyjson_doc_mut_copy(doc, NULL);
				auto mut_root = yyjson_mut_doc_get_root(mut_doc);
				yyjson_doc_free(doc);

				string strJsonSize = image_yuan.substr(image_yuan.size() - 8, 4);
				size_t jsonSize = 0;
				memcpy(&jsonSize, strJsonSize.c_str(), 4);
				string strImg = image_yuan.substr(0, image_yuan.size() - jsonSize - 8);
				if (jsonSize>0)
				{
					string info_yuan = image_yuan.substr(image_yuan.size() - jsonSize - 8, jsonSize);
					auto doc_yuan = yyjson_read(info_yuan.c_str(), info_yuan.size(), 0);
					auto mut_doc_yuan = yyjson_doc_mut_copy(doc_yuan, NULL);
					auto mut_root_yuan = yyjson_mut_doc_get_root(mut_doc_yuan);
					yyjson_doc_free(doc_yuan);

					yyjson_mut_val* key, * val;
					size_t indx = 0, max = 0;
					yyjson_mut_obj_foreach(mut_root, indx, max, key, val)
					{
						if (!yyjson_mut_obj_get(mut_root_yuan, yyjson_mut_get_str(key)))
						{
							yyjson_mut_obj_add_val(mut_doc_yuan, mut_root_yuan, yyjson_mut_get_str(key), yyjson_mut_val_mut_copy(mut_doc_yuan, val));
						}
					}

					char* strTemp = yyjson_mut_write(mut_doc_yuan, 0, 0);
					if (strTemp) {
						imgInfo = strTemp;
						free(strTemp);
					}

					yyjson_mut_doc_free(mut_doc);
					yyjson_mut_doc_free(mut_doc_yuan);
				}
				
				bool ret = saveImage(tag, stTime, (char*)strImg.c_str(), strImg.size(), imgInfo);
				return ret;
			}
			else
			{
				size_t imgInfoSize = imgInfo.size();
				size_t buffLen = imgInfo.size() + 4 + 4;
				char* buff = new char[buffLen];
				memcpy(buff, imgInfo.c_str(), imgInfoSize);
				memcpy(buff + imgInfoSize, &imgInfoSize, 4);
				memcpy(buff + imgInfoSize + 4, "jjpg", 4);
				bool ret = DB_FS::appendWrite(imagePath, buff, buffLen);
				delete[] buff;
				return ret;
			}
		}
		else if (fileExist(imageInfoPath))
		{
			auto doc = yyjson_read(imgInfo.c_str(), imgInfo.size(), 0);
			auto mut_doc = yyjson_doc_mut_copy(doc, NULL);
			auto mut_root = yyjson_mut_doc_get_root(mut_doc);
			yyjson_doc_free(doc);

			string info_yuan;
			DB_FS::readFile(imageInfoPath, info_yuan);
			auto doc_yuan = yyjson_read(info_yuan.c_str(), info_yuan.size(), 0);
			auto mut_doc_yuan = yyjson_doc_mut_copy(doc_yuan, NULL);
			auto mut_root_yuan = yyjson_mut_doc_get_root(mut_doc_yuan);
			yyjson_doc_free(doc_yuan);

			yyjson_mut_val* key, * val;
			size_t indx = 0, max = 0;
			yyjson_mut_obj_foreach(mut_root, indx, max, key, val)
			{
				string strKey = yyjson_mut_get_str(key);
				if (!yyjson_mut_obj_get(mut_root_yuan, strKey.c_str()))
				{
					yyjson_mut_obj_add_val(mut_doc_yuan, mut_root_yuan, strKey.c_str(), yyjson_mut_val_mut_copy(mut_doc_yuan, val));
				}
			}

			char* strTemp = yyjson_mut_write(mut_doc_yuan, 0, 0);
			if (strTemp) {
				imgInfo = strTemp;
				free(strTemp);
			}
			
			yyjson_mut_doc_free(mut_doc);
			yyjson_mut_doc_free(mut_doc_yuan);

			bool ret = DB_FS::writeFile(imageInfoPath, (char*)imgInfo.c_str(), imgInfo.size());
			return ret;
		}
		else
		{
			bool ret = DB_FS::writeFile(imageInfoPath, (char*)imgInfo.c_str(), imgInfo.size());
			return ret;
		}
	}
	else
	{
		return false;
	}
}

TDB* TDB::getChildDB(string dbName) {
	map<string, TDB*>::iterator iter = m_childDB.find(dbName);
	if (iter != m_childDB.end()) {
		return iter->second;
	}
	else {
		TDB* p = new TDB();
		p->m_dbFmt = m_dbFmt;
		p->m_timeUnit = m_timeUnit;
		p->m_name = dbName;
		p->m_path = m_path + "/" + dbName;
		p->m_getTagsByTagSelector = m_getTagsByTagSelector;
		m_childDB[dbName] = p;
		return p;
	}
}

string TDB::parseSuffix(string deFileUrl)
{
	string suffix = "";
	size_t posDot = deFileUrl.rfind(".");
	size_t posSlash = deFileUrl.rfind("/");
	if (posDot != string::npos)
	{
		if (posSlash != string::npos)
		{
			if (posSlash < posDot)
			{
				suffix = deFileUrl.substr(posDot + 1, deFileUrl.size() - posDot - 1);
			}
		}
		else
		{
			suffix = deFileUrl.substr(posDot + 1, deFileUrl.size() - posDot - 1);
		}
	}

	return suffix;
}


bool TDB::fileExist(string pszFileName)
{
#ifdef _WIN32
		wstring filePath = DB_STR::utf8_to_utf16(pszFileName);
		DWORD fileAttributes = GetFileAttributesW(filePath.c_str());
		return (fileAttributes != INVALID_FILE_ATTRIBUTES && !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
#else
	//std::filesystem::path filePath = charCodec::tds_to_utf16(pszFileName); //for windows
	std::filesystem::path filePath = pszFileName;

	if (std::filesystem::exists(filePath)) {
		return true;
	}
	else if (std::filesystem::is_directory(filePath)) {
		return true;
	}
	return  false;
#endif
}



TIME_SELECTOR::TIME_SELECTOR()
{
	enable = true;
	snapShot = false;
	m_dataNum = 0;
}

bool TIME_SELECTOR_ATOM::Match(string& deTime)
{
	if (snapShot) {
		if (deTime <= strEnd) {
			return true;
		}
		else
			return false;
	}
	else {
		if (deTime >= strStart && deTime <= strEnd)
			return true;
		return false;
	}
}

bool TIME_SELECTOR::Match(string& deTime)
{
	if (!enable)
		return true;

	for (int i = 0; i < atomSelList.size(); i++) {
		TIME_SELECTOR_ATOM& tsa = atomSelList[i];
		if (tsa.Match(deTime)) {
			return true;
		}
	}
	return false;
}

bool TIME_SELECTOR::AmountMatch(size_t amount)
{
	if (m_dataNum != 0)
	{
		if (amount < m_dataNum)
		{
			return false;
		}
		return true;
	}
	else
	{
		return false;
	}
}

//common year
int monthLastDay[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
//leap year
int monthLastDay_leapYear[12] = { 31,29,31,30,31,30,31,31,30,31,30,31 };

bool isLeapYear(int year) {
	if (year % 4 == 0) {
		if (year % 100 == 0) {
			if (year % 400 == 0) {
				return true;
			}
		}
		else {
			return true;
		}
	}
	return false;
}


int getMonthLastDay(int year, int month) {
	if (isLeapYear(year)) {
		return monthLastDay_leapYear[month-1];
	}
	else {
		return monthLastDay[month-1];
	}
};


string time2DbFileDate(string& time) {
	string dbFileDate;
	//2020-02
	if (time.length() == 7) {
		string sYear = time.substr(0, 4);
		string sMonth = time.substr(5, 2);
		int y = atoi(sYear.c_str());
		int m = atoi(sMonth.c_str());
		int d = getMonthLastDay(y, m);
		string sDay = formatStr("%2d", d);
		dbFileDate = time + "-" + sDay;
		return dbFileDate;
	}
	//2020-02-03
	else if (time.length() == 10) {
		return time;
	}
	else
	{
		return "";
	}
}

bool TIME_SELECTOR::init(vector<string> timeSelList) {
	if (!enable)
		return true;

	for (int i = 0; i < timeSelList.size(); i++) {
		string s = timeSelList[i];
		TIME_SELECTOR_ATOM tsa;
		if (!tsa.init(s)) {
			return false;
		}
		atomSelList.push_back(tsa);
	}
	return true;
}

bool TIME_SELECTOR::init(string time)
{
	if (!enable)
		return true;

	if (time == "")
		return false;

  if ("e" == time.substr(time.length()-1,1))
	{
		time = time.substr(0, time.length() - 1);
		m_dataNum = atoi(time.c_str());
		string timeRange = "2020-01-01 00:00:00~" + DB_TIME::nowStr();
		parseTimeRange(timeRange);
		TIME_SELECTOR_ATOM tsa;
		tsa.timeSetType = TSM_Last;
		tsa.parseTimeRange(timeRange);
		atomSelList.push_back(tsa);
	}
	else if (time.find("now") != string::npos) {
		DB_TIME tNow; tNow.setNow();
		string s = tNow.toStr(false);
		time = DB_STR::replace(time, "now", s);
		TIME_SELECTOR_ATOM tsa;
		tsa.init(time);
		atomSelList.push_back(tsa);
	}
	//maybe multi time range, such as:
	//"00:00:00~01:00:00@2024-09-20~2024-09-21", or "08:40:00~09:40:00,08:40:00~09:40:00@2024-09-20~2024-09-21" 
	else if (time.find("@") != string::npos) {
		int pos = time.find("@");
		string strHmsRanges = time.substr(0, pos);
		string dateRange = time.substr(pos + 1);

		vector<vector<string>> vecHms;//[[hmsStart,hmsEnd],...]
		vector<string> hmsRanges;
		DB_STR::split(hmsRanges, strHmsRanges,",");
		for (auto& oneRange : hmsRanges) {
			string &hmsRange = oneRange;

			int pos1 = hmsRange.find("~");
			if (pos1 == string::npos || hmsRange.length() != 17)
				return false;
			string hmsStart = hmsRange.substr(0, pos1);
			string hmsEnd = hmsRange.substr(pos1 + 1);
			vector<string> one; one.push_back(hmsStart); one.push_back(hmsEnd);
			vecHms.push_back(one);
		}

		int pos2 = dateRange.find("~");
		if (pos2 == string::npos || dateRange.length() != 21)
			return false;
		DB_TIME stDateStart; stDateStart.fromStr(dateRange.substr(0, pos2) + " 00:00:00");
		int unixDateStart = stDateStart.toUnixTime();
		DB_TIME stDateEnd; stDateEnd.fromStr(dateRange.substr(pos2 + 1) + " 00:00:00");
		int unixDateEnd = stDateEnd.toUnixTime();

		vector<string> timeSelList;
		for (int i = unixDateStart; i <= unixDateEnd; i += 86400) {
			DB_TIME tmp; tmp.fromUnixTime(i);
			string ymd = tmp.toYMD();

			for (auto& one : vecHms) {
				string oneRange = ymd + " " + one[0] + "~" + ymd + " " + one[1];
				timeSelList.push_back(oneRange);
			}
		}
		init(timeSelList);
	}
	//2024-09-20 00:00:00~2024-09-20 10:10:10, 2024-09-21 00:00:00~2024-09-21 10:10:10, ...
	else if (time.find(",") != string::npos) { //not have "@" && have "," 
		return false;//later do this
	}
	else {
		TIME_SELECTOR_ATOM tsa;
		tsa.init(time);
		atomSelList.push_back(tsa);
	}
	return true;
}

bool TIME_SELECTOR::isRange()
{
	for (int i = 0; i < atomSelList.size(); i++) {
		TIME_SELECTOR_ATOM& tsa = atomSelList[i];
		if (tsa.timeSetType == TSM_Range) {
			return true;
		}
	}
	return false;
}

bool TIME_SELECTOR_ATOM::init(string time)
{
	selector = time;

	if (time.find("this-month") != string::npos) {
		DB_TIME t;
		t.setNow();
		DB_TIME tStart = t;
		tStart.wDay = 1; tStart.wHour = 0; tStart.wMinute = 0; tStart.wSecond = 0; tStart.wMilliseconds = 0;
		DB_TIME tEnd = tStart;
		tEnd.wMonth += 1;
		if (tEnd.wMonth == 13) {
			tEnd.wMonth = 1;
			tEnd.wYear += 1;
		}
		tEnd = TIME_OPT::addTime(tEnd, 0, 0, -1);
		time = tStart.toStr() + "~" + tEnd.toStr();
	}
	else if (time.find("last-month") != string::npos) {
		DB_TIME t;
		t.setNow();
		DB_TIME tStart = t;
		tStart.wDay = 1; tStart.wHour = 0; tStart.wMinute = 0; tStart.wSecond = 0; tStart.wMilliseconds = 0;
		tStart.wMonth -= 1;
		if (tStart.wMonth == 0) {
			tStart.wMonth = 12;
			tStart.wYear -= 1;
		}
		DB_TIME tEnd = tStart;
		tEnd.wMonth += 1;
		if (tEnd.wMonth == 13) {
			tEnd.wMonth = 1;
			tEnd.wYear += 1;
		}
		tEnd = TIME_OPT::addTime(tEnd, 0, 0, -1);
		time = tStart.toStr() + "~" + tEnd.toStr();
	}
	else if (time.find("this-year") != string::npos) {
		DB_TIME t;
		t.setNow();
		DB_TIME tStart = t;
		tStart.wDay = 1; tStart.wMonth = 1; tStart.wHour = 0; tStart.wMinute = 0; tStart.wSecond = 0; tStart.wMilliseconds = 0;
		DB_TIME tEnd = tStart;
		tEnd.wYear += 1;
		tEnd = TIME_OPT::addTime(tEnd, 0, 0, -1);
		time = tStart.toStr() + "~" + tEnd.toStr();
	}
	else if (time.find("this-day") != string::npos) {
		string t = DB_TIME::nowStr();
		t = t.substr(0, 10);
		time = replaceStr(time, "this-day", t);
	}
	else if (time.find("today") != string::npos) {
		string t = DB_TIME::nowStr();
		t = t.substr(0, 10);
		time = replaceStr(time, "today", t);
	}
	else if (time.find("yesterday") != string::npos) {
		DB_TIME t;
		t.setNow();
		t = TIME_OPT::addTime(t, -24, 0, 0);
		t.wMilliseconds = 0;
		DB_TIME tStart = t;
		DB_TIME tEnd = t;
		tStart.wHour = 0; tStart.wMinute = 0; tStart.wSecond = 0;
		tEnd.wHour = 23; tEnd.wMinute = 59; tEnd.wSecond = 59;
		time = tStart.toStr() + "~" + tEnd.toStr();
	}

	if (
		time.find("y") != string::npos ||
		time.find("M") != string::npos ||
		time.find("d") != string::npos ||
		time.find("h") != string::npos ||
		time.find("m") != string::npos
		) { // 1d2h3m mode
		string timeRange = TIME_OPT::rel2abs(time);
		parseTimeRange(timeRange);
	}
	else {
		string timeRange = shortSel2StardardSel(time);
		parseTimeRange(timeRange);
	}

	if (startTime == endTime) {
		timeSetType = TSM_AnyPoint;
	}
	else {
		timeSetType = TSM_Range;
	}

	return true;
}

string TIME_SELECTOR_ATOM::shortSel2StardardSel(string time)
{
	//2020-02
	if (time.length() == 7 && time[4] == '-') {
		string sYear = time.substr(0, 4);
		string sMonth = time.substr(5, 2);
		int y = atoi(sYear.c_str());
		int m = atoi(sMonth.c_str());
		int d = getMonthLastDay(y, m);
		string sDayEnd = formatStr("%2d", d);
		return time + "-01 00:00:00~" + time + "-" + sDayEnd + " 23:59:59";
	}
	//2020-02-02
	else if (time.length() == 10 && time[4] == '-') {
		return time + " 00:00:00~" + time + " 23:59:59";
	}
	//2020-02-02 11:12:30
	else if (time.length() == 19 && time[4] == '-') {
		return time + ".000~" + time + ".999";
	}
	//2021~2022
	else if (time.length() == 9 && time[4] == '~'){
		string startYear = time.substr(0, 4);
		string endYear = time.substr(5, 4);
		return startYear + "-01-01 00:00:00~" + endYear + "-12-31 23:59:59"; //12月份固定是31天
	}
	//2021
	else if (time.length() == 4) {
		return time + "-01-01 00:00:00~" + time + "-12-31 23:59:59"; //12月份固定是31天
	}
	return time;
}

DB_TIME_RANGE parseTimeRange(string timeExp) {
	DB_TIME_RANGE tr;
	size_t pos = timeExp.find("~");
	string strStart = timeExp.substr(0, pos);
	string strEnd = timeExp.substr(pos + 1, timeExp.length() - pos - 1);
	if (strStart.find(":") == string::npos)
		strStart += " 00:00:00";
	if (strEnd.find(":") == string::npos)
		strEnd += " 23:59:59";
	tr.start.fromStr(strStart);
	tr.end.fromStr(strEnd);
	return tr;
}

bool TIME_SELECTOR_ATOM::parseTimeRange(string condition)
{
	size_t pos = condition.find("~");
	 strStart = condition.substr(0, pos);
	 strEnd = condition.substr(pos + 1, condition.length() - pos - 1);
	if (strStart.find(":") == string::npos)
		strStart += " 00:00:00.000";
	if (strEnd.find(":") == string::npos)
		strEnd += " 23:59:59.999";
	stStart.fromStr(strStart);
	stEnd.fromStr(strEnd);
	startTime = stStart.toUnixTime(); 
	endTime = stEnd.toUnixTime();
	return true;
}

string TIME_SELECTOR_ATOM::getParsedSelector()
{
	return strStart + "~" + strEnd;
}



bool TAG_SELECTOR::init(string tag, string rootTag, string objtype,string objlevel){
	rootTag = DB_TAG::addRoot(rootTag, m_org);
	m_rootTag = rootTag;
	tagSel = tag;
	if (tag.find("*") != string::npos)
	{
		//if tag is * ,rootTag is HangZhou, so selector is  HangZhou.*
		//TAG::addRoot will add .  , so selector won't be  HangZhou*, if HangZhou* ,HangZhou(Test).temprature will be selected uncorrectly
		string tagExp = DB_TAG::addRoot(tag, rootTag);
		string regExp = tagExp;

/*
		The special characters in regular expressions are :

		-. : Matches any character except a newline.
		- *: Matches the preceding element zero or more times.
		- +: Matches the preceding element one or more times.
		- ? : Matches the preceding element zero or one time.
		- ^ : Matches the beginning of the input string.
		- $ : Matches the end of the input string.
		- [] : Defines a character class, matches any one character within the brackets.
		- () : Marks the start and end of a subexpression.
		- | : Specifies a choice between two or more patterns.
		- \ : Escape character, used to escape special characters.
*/

		regExp = replaceStr(regExp, ".", "\\.");
		regExp = replaceStr(regExp, "*", ".*");
		regExp = replaceStr(regExp, "+", "\\+");
		regExp = replaceStr(regExp, "?", "\\?");
		regExp = replaceStr(regExp, "^", "\\^");
		regExp = replaceStr(regExp, "$", "\\$");
		regExp = replaceStr(regExp, "[", "\\[");
		regExp = replaceStr(regExp, "]", "\\]");
		regExp = replaceStr(regExp, "(", "\\(");
		regExp = replaceStr(regExp, ")", "\\)");
		regExp = replaceStr(regExp, "|", "\\|");

		fuzzyMatchExp.push_back(tagExp);
		fuzzyMatchRegExp.push_back(regExp);
	}
	else
	{
		tag = DB_TAG::addRoot(tag, rootTag);
		exactMatchExp.push_back(tag);
	}

	setType(objtype);
	level = objlevel;
	return true;
}

bool TAG_SELECTOR::init(vector<string>& tag, string rootTag, string objtype, string objlevel)
{
	for (auto& i : tag) {
		init(i, rootTag,objtype,objlevel);
	}
	return true;
}

void TAG_SELECTOR::setType(string objType)
{
	if (objType == "all") {
		type = "*";
	}
	else {
		type = objType;
	}
}

bool TAG_SELECTOR::specifyType()
{
	if (type == "")
		return false;
	if (type == "*")
		return false;
	return true;
}

bool TAG_SELECTOR::match(string tag){
	if (tagSel == "*")
		return true;

	for (int i = 0; i < exactMatchExp.size(); i++) {
		string& exp = exactMatchExp[i];
		if (exp == tag) {
			return true;
		}
	}

	for (int i = 0; i < fuzzyMatchRegExp.size(); i++) {
		string& sreg = fuzzyMatchRegExp[i];
		std::regex reg(sreg);
		if (std::regex_match(tag, reg))
		{
			return true;
		}
	}
	return false;
}

bool TAG_SELECTOR::singleSelMode()
{
	if (fuzzyMatchExp.size() == 0 && exactMatchExp.size() == 1) {
		return true;
	}
	return false;
}



CONDITION_SELECTOR::CONDITION_SELECTOR()
{
	bEnable = false;
}


bool CONDITION_SELECTOR::init(string filter)
{
	if (filter.length() > 0)
	{
#ifdef ENABLE_QJS
		// create runtime and context
		JSRuntime* rt = JS_NewRuntime();
		if (!rt) return false;
		global_object = JS_NewContext(rt);
		if (!global_object)
		{
			JS_FreeRuntime(rt);
			return false;
		}
#endif

		filterExp = filter;
		bEnable = true;
	return true;

	}
	return false;
}

CONDITION_SELECTOR::~CONDITION_SELECTOR()
{
	if (filterExp.length() > 0)
	{
#ifdef ENABLE_QJS
		// get runtime
		JSRuntime* rt = JS_GetRuntime(global_object);

		// free
		JS_FreeContext(global_object);
		JS_FreeRuntime(rt);
#endif
	}
}

#ifdef ENABLE_QJS

bool CONDITION_SELECTOR::evaluate_condition(const char* json_str, size_t json_len)
{
	bool ret = false;
	JSValue json_val = JS_UNDEFINED;
	JSValue global = JS_UNDEFINED;
	JSPropertyEnum* props = nullptr;
	uint32_t len = 0;

	try
	{
		// parse JSON
		json_val = JS_ParseJSON(global_object, json_str, json_len, "<input>");
		if (JS_IsException(json_val)) {
			JSValue exception = JS_GetException(global_object);
			const char* err_str = JS_ToCString(global_object, exception);
			string err = DB_STR::utf8_to_gb(err_str);
			std::cerr << "JSON parse json: " << err << std::endl;
			JS_FreeCString(global_object, err_str);
			JS_FreeValue(global_object, exception);
			return false;
		}

		// get global object
		global = JS_GetGlobalObject(global_object);

		// copy properties from JSON to global object
		if (JS_GetOwnPropertyNames(global_object, &props, &len, json_val, JS_GPN_STRING_MASK) < 0) {
			throw std::runtime_error("getOwnPropertyNames failed");
		}

		for (uint32_t i = 0; i < len; i++) {
			JSValue val = JS_GetProperty(global_object, json_val, props[i].atom);
			if (JS_IsException(val)) {
				JS_FreeAtom(global_object, props[i].atom);
				continue;
			}

			JS_SetProperty(global_object, global, props[i].atom, val);
			JS_FreeAtom(global_object, props[i].atom);
		}


		// evaluate script
		std::string script = "!!(" + filterExp + ")";
		JSValue result = JS_Eval(global_object, script.c_str(), script.size(), "<eval>", JS_EVAL_TYPE_GLOBAL);

		if (JS_IsException(result)) {
			JSValue exception = JS_GetException(global_object);
			const char* err_str = JS_ToCString(global_object, exception);
			string err = DB_STR::utf8_to_gb(err_str);
			std::cerr << "evaluate script error: " << err << std::endl;
			JS_FreeCString(global_object, err_str);
			JS_FreeValue(global_object, exception);
			ret = false;
		}
		else {
			ret = JS_ToBool(global_object, result);
		}

		JS_FreeValue(global_object, result);
	}
	catch (const std::exception& e)
	{
		std::cerr << "err: " << e.what() << std::endl;
		ret = false;
	}
	catch (...)
	{
		std::cerr << "err: unknown exception" << std::endl;
		ret = false;
	}

	// free resources
	if (!JS_IsUndefined(global)) {
		for (uint32_t i = 0; i < len; i++) JS_DeleteProperty(global_object, global, props[i].atom, 0);
		JS_FreeValue(global_object, global);
	}
	if (props) {
		// free property array
		js_free(global_object, props);
		props = nullptr;
	}
	if (!JS_IsUndefined(json_val)) {
		JS_FreeValue(global_object, json_val);
	}

	return ret;
}
#endif

bool CONDITION_SELECTOR::match(yyjson_mut_val* de)
{
#ifdef ENABLE_QJS
	if (!bEnable)
		return true;

	bool bMatch = true;

	size_t json_len = 0;
	auto json_str = yyjson_mut_val_write(de, NULL, &json_len);
	if (json_str) {
		bMatch = evaluate_condition(json_str, json_len);
		free(json_str);
		return bMatch;
	}
#endif
	return true;
}

bool CONDITION_SELECTOR::match(yyjson_val* de)
{
	bool bMatch = false;
#ifdef ENABLE_QJS
	if (!bEnable)
		return true;

	size_t json_len = 0;
	char* json_str = yyjson_val_write(de, NULL, &json_len);
	if (json_str) {
		bMatch = evaluate_condition(json_str, json_len);

		free(json_str);
		return bMatch;
	}
#endif
	return bMatch;
}

string DE_SELECTOR::getSelectorDesc()
{
	return "";
}

void DB_TIME::fromUnixTime(time_t iUnix, int milli)
{
	if (iUnix > 100000000000) { //unix timestamp with milli
		iUnix = iUnix / 1000;
		milli = iUnix % 1000;
	}

	tm time_tm;
#ifdef _WIN32
	localtime_s(&time_tm, &iUnix);
#else
	localtime_r(&iUnix,&time_tm);  //for thread safty linux recommends localtime_r,windows recommends localtime_s
#endif

	wYear = time_tm.tm_year + 1900;
	wMonth = time_tm.tm_mon + 1;
	wDay = time_tm.tm_mday;
	wHour = time_tm.tm_hour;
	wMinute = time_tm.tm_min;
	wSecond = time_tm.tm_sec;
	wMilliseconds = milli;
	wDayOfWeek = time_tm.tm_wday;
}

time_t DB_TIME::toUnixTime() const
{
	tm temptm = { wSecond, wMinute, wHour,wDay, wMonth - 1, wYear - 1900, wDayOfWeek, 0, 0 };
	time_t iReturn = mktime(&temptm);
	return iReturn;
}

void DB_TIME::setNow()
{
	auto now = std::chrono::system_clock::now();
	unsigned short milli = (unsigned short)std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()
		- std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() * 1000;
	time_t tt = std::chrono::system_clock::to_time_t(now);
	fromUnixTime(tt, milli);
}

string DB_TIME::toStampHMS() const
{
	string s = formatStr("%02d%02d%02d", wHour, wMinute, wSecond);
	return s;
}

string DB_TIME::toStampFull() const
{
	string s = formatStr("%04d-%02d-%02d %02d%02d%02d", wYear,wMonth,wDay,wHour, wMinute, wSecond);
	return s;
}

string DB_TIME::toYMD() const
{
	string str;
	if (wYear > 2000 && wDay > 0 && wDay < 40 && wHour >= 0 && wHour <= 24 && wMinute >= 0 && wMinute <= 60)
	{
		str = formatStr("%.4d-%.2d-%.2d", wYear,wMonth, wDay);
	}
	return str;
}


string  DB_TIME::toStr(bool enableMS) const
{
	if (enableMS) {
		string str = formatStr("%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",wYear, wMonth, wDay,wHour, wMinute, wSecond,wMilliseconds);
		return str;
	}
	else {
		string str = formatStr("%.4d-%.2d-%.2d %.2d:%.2d:%.2d",wYear, wMonth, wDay,wHour, wMinute, wSecond);
		return str;
	}
}

bool DB_TIME::fromStr(string str)
{
	DB_TIME& t = *this;
	memset(&t, 0, sizeof(t));
	int y, m, d, h, min, s, milli;
	if (str[4] == '-') {
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
			return true;
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
			return true;
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
			return true;
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
			return true;
		}
		else if (str.length() == 10) //2022-02-02
		{
			sscanf(str.c_str(), "%4d-%2d-%2d",
				&y,
				&m,
				&d);
			t.wYear = y; t.wMonth = m; t.wDay = d;
			return true;
		}
	}
	else if (str[2] == ':') {
		if (str.length() == 8) //12:11:11
		{
			sscanf(str.c_str(), "%2d:%2d:%2d",
				&h,
				&min,
				&s);
			t.wHour = h; t.wMinute = min; t.wSecond = s;
			return true;
		}
	}
	else {
		time_t tt = atoi(str.c_str());
		fromUnixTime(tt);
		return true;
	}

	db_exception e;
	e.m_error = "wrong time string format," + str;
	throw e;

	return false;
}

string DB_TIME::nowStr()
{
	DB_TIME t;
	t.setNow();
	return t.toStr(false);
}

string DB_TIME::nowStrWithMilli()
{
	DB_TIME t;
	t.setNow();
	return t.toStr(true);
}

bool DB_FILE::isDataList() {
	if (deType != "curve") {
		return true;
	}
	return false;
}

bool DB_FILE::loadFile() {
	//time.fromUnixTime(ttTime);

	ymd = time.toYMD();
	path = pOwnerDB->getPath_dbFile(tag, time, deType);

	if (pOwnerDB->m_bEnableFsBuff && isDataList()) {
		pOwnerDB->m_FsBuff.readFile(path, data);
	}
	else {
		DB_FS::readFile(path, data);
	}

	if (data == "") {
		return false;
	}

	yyjson_read_err err = { 0 };
	doc = yyjson_read_opts((char*)data.c_str(), data.length(), 0, nullptr, &err);
	if (err.code != YYJSON_READ_SUCCESS) {
		//reload gbk string
		if (err.code == YYJSON_READ_ERROR_INVALID_STRING)
		{
			data = DB_STR::gb_to_utf8(data);
			doc = yyjson_read_opts((char*)data.c_str(), data.length(), 0, nullptr, &err);
		}

		if (err.code != YYJSON_READ_SUCCESS) {
			// error message
			string sErr = err.msg;
			sErr = "load json file fail,file path:" + path + " ,parse fail at byte " + formatStr("%d", err.pos) + ",errInfo:" + sErr;
			db_exception e;
			e.m_error = sErr;
			throw e;
		}
	}

	root = yyjson_doc_get_root(doc);
	return true;
}

bool FS_BUFF::readFile(string path, string& data)
{
	m_csFsb.lock();
	std::map<string, FILE_BUFF*>::iterator iter = m_mapFsBuff.find(path);
	if (iter != m_mapFsBuff.end()) {
		data = iter->second->data;
		FILE_BUFF* fb = iter->second;
		fb->lastActive.setNow();
		m_csFsb.unlock();
		return true;
	}
	m_csFsb.unlock();

	bool bRet = DB_FS::readFile(path, data);
	if (bRet) {
		m_csFsb.lock();
		FILE_BUFF* fb = new FILE_BUFF();
		fb->data = data;
		fb->lastActive.setNow();
		m_mapFsBuff[path] = fb;
		m_csFsb.unlock();
	}

	return bRet;
}

bool FS_BUFF::writeFile(string path, unsigned char* data, size_t len)
{
	return false;
}



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

