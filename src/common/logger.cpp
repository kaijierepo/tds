#include "logger.h"
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>

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

void LOG(const char* pszFmt, ...) {
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

void LOG(string info) {
	logger.log(info);
}

void LOG_C(char* info) {
	logger.log(info);
}

Clogger::Clogger()
{
	m_bSaveToFile = false;
	dirCreated = false;
	logOutput = NULL;
	m_bEnable = true;
}

void Clogger::init()
{
	
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

#include <array>
#include <chrono>
#include <cstdio>


std::string getTimeInfo(struct tm& timeinfo,string& timestamp) {

	// 获取当前时间
	auto now = std::chrono::system_clock::now();
	auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
	auto duration = now_ms.time_since_epoch();

	// 转换为time_t
	std::time_t t = std::chrono::system_clock::to_time_t(now_ms);

	// 时间结构
#if defined(_WIN32)
	localtime_s(&timeinfo, &t);
#else
	localtime_r(&t, &timeinfo);
#endif

	// 直接格式化到缓冲区
	constexpr size_t buf_size = 24;
	std::array<char, buf_size> buf;

	// 格式化日期和时间
	size_t len = std::strftime(buf.data(), buf_size,
		"%Y-%m-%d %H:%M:%S",
		&timeinfo);

	// 添加毫秒
	auto ms = duration.count() % 1000;
	std::snprintf(buf.data() + len, 5, ".%03lld", ms);

	return std::string(buf.data());
}

void fast_tm_to_ymd_str(const struct tm* t, char* buf) {
	// 预计算的两位数字转换表
	static const char digits[] =
		"00010203040506070809"
		"10111213141516171819"
		"20212223242526272829"
		"30313233343536373839"
		"40414243444546474849"
		"50515253545556575859"
		"60616263646566676869"
		"70717273747576777879"
		"80818283848586878889"
		"90919293949596979899";

	// 年: tm_year 是从 1900 开始的偏移
	const int year = t->tm_year + 1900;
	const int year_tens = year % 100;

	// 月份调整
	const int month = t->tm_mon + 1;

	// 直接内存拷贝
	buf[0] = '0' + year / 1000;       // 年份千位
	memcpy(buf + 1, digits + (year / 10 % 100) * 2, 2); // 年份中间两位
	memcpy(buf + 3, digits + (year % 100) * 2, 2);       // 年份十位和个位
	memcpy(buf + 4, digits + month * 2, 2);             // 月份
	memcpy(buf + 6, digits + t->tm_mday * 2, 2);        // 日期
	buf[8] = '\0';
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

bool createFolderOfPath(string strFile)
{
	strFile.replace(strFile.begin(),strFile.end(), "\\", "/");
	strFile.replace(strFile.begin(), strFile.end(), "////", "/");
	strFile.replace(strFile.begin(), strFile.end(), "///", "/");
	strFile.replace(strFile.begin(), strFile.end(), "//", "/");

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
	return filesystem::create_directories(utf8_to_utf16(strFile));
#endif
#else
	filesystem::path p = strFile;
	return filesystem::create_directories(p);
#endif
}

bool appendFile(string path, char* data, size_t len)
{
	FILE* fp = nullptr;
#ifdef _WIN32
	_wfopen_s(&fp, utf8_to_utf16(path).c_str(), L"ab");
#else
	fp = fopen(path.c_str(), "ab");
#endif
	if (fp)
	{
		fwrite(data, 1, len, fp);
		fclose(fp);
		return true;
	}
	return false;
}

string Clogger::logInternal(string info, bool writeToFile)
{
	LOG_LEVEL ll = getLogLevel(info);
	if (ll == LOG_LEVEL::LL_KEYINFO)
	{
		info.replace(info.begin(), info.end(), "[keyinfo]", "");
	}
	
	struct tm stNow;
	string timeStamp;
	getTimeInfo(stNow,timeStamp);

	//命令行和文件中的日志用gb2312编码
	string logline = timeStamp + " " + info;

	//logLevel用户控制本地命令行界面和日志文件当中是否记录。weblog监视统一全部推送
	if (ll < logLevel) {
		return logline;
	}

#ifdef _WIN32
	info = utf8_to_gb(logline);
#else
	info = logline;
#endif

	setConsoleTextColor(ll);

	printf(info.c_str());
	printf("\r\n");

	if (writeToFile && m_strLogDir!="") {
		//create log path
		std::lock_guard<mutex> lockGuard(m_lock);
	
		//程序调试过程中，可能经常有删除整个日志文件夹，然后运行一会看下日志这样的操作。因此每次都尝试创建文件夹
		createFolderOfPath(m_strLogDir);

		//save to log file
		char datePath[20] = {0};
		fast_tm_to_ymd_str(&stNow, datePath);
		string strFile = datePath;
		strFile = m_strLogDir + "/" + strFile + ".log";
		info += "\r\n";
		appendFile(strFile,(char*)info.c_str(),info.size());
	}

	return logline;
}

void Clogger::log(string info, bool writeToFile) {
	if (LL_NONE == logLevel) {
		return;
	}

	//logInternal only log to file and cmdline
	//log will log to some user specified place, the code must not trigger log again
	//log to websocket code routine must not use log, but use logInternal
	string log = logInternal(info);
	if (log != "" && logOutput) {
		logOutput(log);
	}
}
