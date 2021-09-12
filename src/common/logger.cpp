#include "pch.h"
#include "logger.h"
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include "common.h"

Clogger logger;
void LOG(const char* pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt);
	{
		int nLength = _vscprintf(pszFmt, args);
		nLength += 1;
		std::vector<char> vectorChars(nLength);
		_vsnprintf(vectorChars.data(), nLength, pszFmt, args);
		str.assign(vectorChars.data());
	}
	va_end(args);
	LOG(str);
}
void LOG(string info)
{
	logger.log(info);
}
void loggingCB(char* info)
{
	logger.log(info);
}

Clogger::Clogger()
{
	m_bSaveToFile = false;
	dirCreated = false;
	logOutput = NULL;
}

std::string Clogger::formatStr(const char* pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt);
	{
		int nLength = _vscprintf(pszFmt, args);
		nLength += 1;  
		std::vector<char> vectorChars(nLength);
		_vsnprintf(vectorChars.data(), nLength, pszFmt, args);
		str.assign(vectorChars.data());
	}
	va_end(args);
	return str;
}

LOG_LEVEL Clogger::str2logLevel(string level)
{
	LOG_LEVEL ll;
	if (level == "trace")
		ll = LL_TRACE;
	else if (level == "detail")
		ll = LL_DETAIL;
	else if (level == "debug")
		ll = LL_DEBUG;
	else if (level == "warn")
		ll = LL_WARN;
	else if (level == "error")
		ll = LL_ERROR;
	else
		ll = LL_DEBUG;
	return ll;
}

void Clogger::setLogLevel(string level)
{
	logLevel = str2logLevel(level);
}

bool Clogger::isNeedLog(string info)
{
	LOG_LEVEL ll = LL_DEBUG;
	if (info.find("[trace]") != string::npos)
		ll = str2logLevel("trace");
	else if (info.find("[detail]") != string::npos)
		ll = str2logLevel("detail");
	else if (info.find("[debug]") != string::npos)
		ll = str2logLevel("debug");
	else if (info.find("[warn]") != string::npos)
		ll = str2logLevel("warn");
	else if (info.find("[error]") != string::npos)
		ll = str2logLevel("error");

	if (ll >= logLevel)
		return true;
	return false;
}

string Clogger::logInternal(string info)
{
	if (!isNeedLog(info))
		return "";

	SYSTEMTIME stNow;
	GetLocalTime(&stNow);
	string time = formatStr("%02d:%02d:%02d.%03d", stNow.wHour, stNow.wMinute, stNow.wSecond, stNow.wMilliseconds);
	//命令行和文件中的日志用gb2312编码
	string logline = time + " " + info;
	info = charCodec::utf8toAnsi(logline);

	printf(info.c_str());
	printf("\r\n");
	//std::cout << info << std::endl; 这句话在 AllocConsole 生成的命令行中不输出了

	//create log path
	std::lock_guard<mutex> lockGuard(m_lock);
	if (!dirCreated)
	{
		wstring strLogDir = charCodec::utf8toUtf16(fs::appPath() + "\\log");
		DWORD dwAttr = ::GetFileAttributesW(strLogDir.c_str());
		if ((dwAttr == -1) || ((dwAttr & FILE_ATTRIBUTE_DIRECTORY) == 0))
		{
			::CreateDirectoryW(strLogDir.c_str(), NULL);
		}
		dirCreated = true;
	}

	//save to log file
	string strFile = formatStr("%04d%02d%02d", stNow.wYear, stNow.wMonth, stNow.wDay);
	strFile = fs::appPath() + "\\log\\" + strFile + ".txt";
	fs::appendFile(strFile, info + "\r\n");

	return logline;
}

void Clogger::log(string info)
{
	//logInternal only log to file and cmdline
	//log will log to some user specified place, the code must not trigger log again
	//log to websocket code routine must not use log, but use logInternal
	string log = logInternal(info);
	if (log != "" && logOutput)
	{
		logOutput(log);
	}
}
