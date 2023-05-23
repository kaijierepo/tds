#include "logger.h"
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include "common.h"
#include "dumpCatch.h"

//linux console color control
#define COLOR_(msg, color, ctl) \
  "\033[0;" #ctl ";" #color ";m" msg "\033[0m"

#define COLOR(msg, color) \
  "\033[0;" #color ";m" msg "\033[0m"

#define BLACK(msg)  COLOR(msg, 30)
#define RED(msg)    COLOR(msg, 31)
#define GREEN(msg)  COLOR(msg, 32)
#define YELLOW(msg) COLOR(msg, 33)
#define BLUE(msg)   COLOR(msg, 34)
#define PURPLE(msg) COLOR(msg, 35)
#define CYAN(msg)   COLOR(msg, 36)
#define WHITE(msg)  COLOR(msg, 37)

#define BBLACK(msg)  COLOR_(msg, 30, 1)
#define BRED(msg)    COLOR_(msg, 31, 1)
#define BGREEN(msg)  COLOR_(msg, 32, 1)
#define BYELLOW(msg) COLOR_(msg, 33, 1)
#define BBLUE(msg)   COLOR_(msg, 34, 1)
#define BPURPLE(msg) COLOR_(msg, 35, 1)
#define BCYAN(msg)   COLOR_(msg, 36, 1)
#define BWHITE(msg)  COLOR_(msg, 37, 1)

#define UBLACK(msg)  COLOR_(msg, 30, 4)
#define URED(msg)    COLOR_(msg, 31, 4)
#define UGREEN(msg)  COLOR_(msg, 32, 4)
#define UYELLOW(msg) COLOR_(msg, 33, 4)
#define UBLUE(msg)   COLOR_(msg, 34, 4)
#define UPURPLE(msg) COLOR_(msg, 35, 4)
#define UCYAN(msg)   COLOR_(msg, 36, 4)
#define UWHITE(msg)  COLOR_(msg, 37, 4)


int _vscprintf_cross_log(const char* format, va_list pargs) {
	int retval;
	va_list argcopy;
	va_copy(argcopy, pargs);
	retval = vsnprintf(NULL, 0, format, argcopy);
	va_end(argcopy);
	return retval;
}

Clogger logger;
void LOG(const char* pszFmt, ...)
{
	std::string str;
	va_list args;
	va_start(args, pszFmt);
	{
		int nLength = _vscprintf_cross_log(pszFmt, args);
		nLength += 1;  //上面返回的长度是包含\0，这里加上
		std::vector<char> vectorChars(nLength);
		vsnprintf(vectorChars.data(), nLength, pszFmt, args);
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
	m_strLogDir = fs::appPath() + "\\log";
	m_bSaveToFile = false;
	dirCreated = false;
	logOutput = NULL;
	m_bEnable = true;
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
	else if (level == "keyinfo")
		ll = LL_KEYINFO;
	else if (level == "none")
		ll = LL_NONE;
	else
		ll = LL_DEBUG;
	return ll;
}

void Clogger::setLogLevel(string level)
{
	logLevel = str2logLevel(level);
}

LOG_LEVEL Clogger::getLogLevel(string& info)
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
	else if (info.find("[keyinfo]") != string::npos)
		ll = str2logLevel("keyinfo");


	return ll;
}

void Clogger::setConsoleTextColor(LOG_LEVEL ll) {
#ifdef WINDOWS
	//使用cout输出，不要使用printf输出，printf会将某些格式进行解析，例如下面字符串
	//R"([IO设备透传]client->dev q 11 i0;c0;x63;n0;r0;q1;p0;w0;s0;m0;a5;tbodazl/624378949537178/lastdp;u{""""data"""":[{""""name"""":""""%N%"""",""""value"""":%V%}]};dupdata)";
	//使用printf输出会导致奔溃 ，应该print将 %N% 作为某种特殊字符处理了
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (ll == LOG_LEVEL::LL_ERROR)
	{
		SetConsoleTextAttribute(handle, FOREGROUND_INTENSITY | FOREGROUND_RED);
	}
	else if (ll == LOG_LEVEL::LL_WARN)
	{
		SetConsoleTextAttribute(handle, FOREGROUND_INTENSITY | 0x06);
	}
	else if (ll == LOG_LEVEL::LL_KEYINFO)
	{
		SetConsoleTextAttribute(handle, FOREGROUND_INTENSITY | FOREGROUND_GREEN);
	}
	else
	{
		SetConsoleTextAttribute(handle, 0x07);
	}
#endif
}

string Clogger::logInternal(string info)
{
	LOG_LEVEL ll = getLogLevel(info);
	if (ll == LOG_LEVEL::LL_KEYINFO)
	{
		info = str::trim(info, "[keyinfo]");
	}
	

	TIME stNow;
	timeopt::now(&stNow);
	string time = str::format("%02d:%02d:%02d.%03d", stNow.wHour, stNow.wMinute, stNow.wSecond, stNow.wMilliseconds);
	//命令行和文件中的日志用gb2312编码
	string logline = time + " " + info;

	//logLevel用户控制本地命令行界面和日志文件当中是否记录。weblog监视统一全部推送
	if (ll < logLevel)
		return logline;

	info = charCodec::utf8_to_gb(logline);
	setConsoleTextColor(ll);
	cout << info;
	printf("\r\n");
	//std::cout << info << std::endl; 这句话在 AllocConsole 生成的命令行中不输出了

	//create log path
	std::lock_guard<mutex> lockGuard(m_lock);
	//if (!dirCreated)
	//{
	//	wstring strLogDir = charCodec::utf8toUtf16(fs::appPath() + "\\log");
	//	DWORD dwAttr = ::GetFileAttributesW(strLogDir.c_str());
	//	if ((dwAttr == -1) || ((dwAttr & FILE_ATTRIBUTE_DIRECTORY) == 0))
	//	{
	//		::CreateDirectoryW(strLogDir.c_str(), NULL);
	//	}
	//	dirCreated = true;
	//}
	//程序调试过程中，可能经常有删除整个日志文件夹，然后运行一会看下日志这样的操作。因此每次都尝试创建文件夹
	fs::createFolderOfPath(m_strLogDir);
	

	//save to log file
	string strFile = str::format("%04d%02d%02d", stNow.wYear, stNow.wMonth, stNow.wDay);
	strFile = fs::appPath() + "\\log\\" + strFile + ".txt";
	fs::appendFile(strFile, info + "\r\n");

#ifdef _TDS
	//if (ll == LOG_LEVEL::LL_ERROR)
	//{
	//	if (tds->conf->bCreateDumpWhenLogError)
	//	{
	//		int iRet = ::MessageBox(NULL, charCodec::utf8_to_gb("CreateDumpWhenLogError功能开启,错误日志,是否截取dump").c_str(), "CreateDumpWhenLogError", MB_OKCANCEL);
	//		if (iRet = IDOK)
	//		{
	//			CDumpCatch::createDump(NULL);
	//		}
	//	}
	//}
#endif

	return logline;
}

void Clogger::log(string info)
{
	if (LL_NONE == logLevel)
		return;
	//logInternal only log to file and cmdline
	//log will log to some user specified place, the code must not trigger log again
	//log to websocket code routine must not use log, but use logInternal
	string log = logInternal(info);
	if (log != "" && logOutput)
	{
		logOutput(log);
	}
}
