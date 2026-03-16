#include "logger.h"
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <iostream>
#include <ctime>
#include <limits.h>

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
		nLength += 1; 

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

static std::string getAppDir() {
#ifdef _WIN32
	char buffer[MAX_PATH] = { 0 };
	if (GetModuleFileNameA(NULL, buffer, MAX_PATH) == 0) {
		return "";
	}

	std::string path(buffer);
	size_t last_slash = path.find_last_of("\\/");
	if (last_slash != std::string::npos) {
		return path.substr(0, last_slash);
	}
#else
	char buffer[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (len == -1) {
		return "";
	}
	buffer[len] = '\0';

	std::string path(buffer);
	size_t last_slash = path.find_last_of('/');
	if (last_slash != std::string::npos) {
		return path.substr(0, last_slash);
	}
#endif
	return ""; 
}

static std::string getAppName() {
#ifdef _WIN32
	char buffer[MAX_PATH] = { 0 };
	if (GetModuleFileNameA(NULL, buffer, MAX_PATH) == 0) {
		return "";
	}

	std::string path(buffer);
	size_t last_slash = path.find_last_of("\\/");
	if (last_slash != std::string::npos) {
		path = path.substr(last_slash + 1);
	}

	size_t pos = path.find('.');
	if (pos != std::string::npos) {
		return path.substr(0, pos);
	}
	else {
		return path; 
	}
#else
	char buffer[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (len == -1) {
		return "";
	}
	buffer[len] = '\0';

	std::string path(buffer);
	size_t last_slash = path.find_last_of('/');
	if (last_slash != std::string::npos) {
		path = path.substr(last_slash + 1);
	}

	size_t pos = path.find('.');
	if (pos != std::string::npos) {
		return path.substr(0, pos);
	}
	else {
		return path; 
	}
#endif
}

static string utf8_to_gb(string instr) //utf-8-->ansi
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
	str = instr;
#endif
	return str;
}

static string gb_to_utf8(string instr) {
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
	str = instr;
#endif
	return str;
}

static wstring utf8_to_utf16(string instr) //utf-8-->ansi
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

Clogger::Clogger()
{
	m_bSaveToFile = false;
	dirCreated = false;
	logOutput = NULL;
	m_bEnable = true;

	string appDir = getAppDir();
	string appName = getAppName();

	// getAppDir  getAppName returns gb
	m_strLogDir = gb_to_utf8(appDir + "/log_" + appName);
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
	//use cout not printf,printf will parse some format,such as string below
	//R"([IO]client->dev q 11 i0;c0;x63;n0;r0;q1;p0;w0;s0;m0;a5;tbodazl/624378949537178/lastdp;u{""""data"""":[{""""name"""":""""%N%"""",""""value"""":%V%}]};dupdata)";
	//printf caused crash, print doing something special with %N% 
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


static void getTimeInfo(struct tm& timeinfo,string& timestamp) {
	auto now = std::chrono::system_clock::now();
	auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
	auto duration = now_ms.time_since_epoch();

	std::time_t t = std::chrono::system_clock::to_time_t(now_ms);

#if defined(_WIN32)
	localtime_s(&timeinfo, &t);
#else
	localtime_r(&t, &timeinfo);
#endif

	constexpr size_t buf_size = 24;
	std::array<char, buf_size> buf;

	size_t len = std::strftime(buf.data(), buf_size,
		"%Y%m%d %H:%M:%S",
		&timeinfo);

	auto ms = duration.count() % 1000;
	std::snprintf(buf.data() + len, 5, ".%03lld", ms);

	timestamp = buf.data();
}

static bool appendFile(string path, char* data, size_t len)
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

static string replaceStr(string str, const string to_replaced, const string newchars) {
	for (string::size_type pos(0); pos != string::npos; pos += newchars.length()) {
		pos = str.find(to_replaced, pos);

		if (pos != string::npos) {
			str.replace(pos, to_replaced.length(), newchars);
		}
		else {
			break;
		}
	}

	return str;
}

static void createFolderOfPath(string strFile) {
	strFile = replaceStr(strFile, "\\", "/");
	strFile = replaceStr(strFile, "////", "/");
	strFile = replaceStr(strFile, "///", "/");
	strFile = replaceStr(strFile, "//", "/");

	size_t iDotPos = strFile.rfind('.');
	size_t iSlashPos = strFile.rfind('/');
	if (iDotPos != string::npos && iDotPos > iSlashPos) {//is a file
		strFile = strFile.substr(0, iSlashPos);
	}

#ifdef _WIN32
	int iStartPos = 0;
	while (1) {
		int iSlash = strFile.find('/', iStartPos);
		if (iSlash == string::npos) {
			break; 
		}

		string strFolder = strFile.substr(0, iSlash);
		CreateDirectoryW(utf8_to_utf16(strFolder).c_str(), NULL);

		if (iSlash + 1 == strFile.length()) {//last char is /
			break;
		}

		iStartPos = iSlash + 1;
	}

	CreateDirectoryW(utf8_to_utf16(strFile).c_str(), NULL);
#else
	
#endif
}

string Clogger::logInternal(string info, bool writeToFile)
{
	LOG_LEVEL ll = getLogLevel(info);

	struct tm stNow;
	string timeStamp;
	getTimeInfo(stNow,timeStamp);

	string logline = timeStamp + " " + info;

	if (ll < logLevel) {
		return logline;
	}

#ifdef _WIN32
	//use gb2312 log in windows
	if (m_InterfaceCodec == LOG_CODEC::utf8) {
		logline = utf8_to_gb(logline);
	}
#else
	//use utf8 log in linux
#endif

	setConsoleTextColor(ll);

	cout << logline;
	printf("\r\n");

	if (writeToFile && m_strLogDir!="" && m_bEnable) {
		//create log path
		std::lock_guard<mutex> lockGuard(m_lock);
	
		//when debugging ,maybe all log dir is deleted, then wait new log file generate, try create folder each time
		createFolderOfPath(m_strLogDir);

		//save to log file
		string strFile = timeStamp.substr(0,8);
		strFile = m_strLogDir + "/" + strFile + ".log";
		logline += "\r\n";
		appendFile(strFile,(char*)logline.c_str(), logline.size());
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
