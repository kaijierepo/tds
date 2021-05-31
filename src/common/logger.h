#pragma once
#include <string>
#include <mutex>
using namespace std;

//log level:  trace,debug,warn,error
enum LOG_LEVEL {
	LL_TRACE = -1,
	LL_DEBUG = 0,
	LL_WARN = 1,
	LL_ERROR = 2,
};


class  Clogger
{
public:
	Clogger();
	std::string formatStr(const char* pszFmt, ...);
	LOG_LEVEL str2logLevel(string level);
	void setLogLevel(string level);
	bool isNeedLog(string info);
	string appPath();
	void log(string info);
	bool dirCreated;
	LOG_LEVEL logLevel;
	mutex m_lock;
};

extern Clogger logger;

void LOG(const char* pszFmt, ...);
void LOG(string info);