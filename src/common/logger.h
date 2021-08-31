#pragma once
#include <string>
#include <mutex>
using namespace std;

//log level:  trace,debug,warn,error
enum LOG_LEVEL {
	LL_DETAIL = -2,
	LL_TRACE = -1,
	LL_DEBUG = 0,
	LL_WARN = 1,
	LL_ERROR = 2,
};

typedef void (*fp_logOutputCallback)(string text);

class  Clogger
{
public:
	Clogger();
	std::string formatStr(const char* pszFmt, ...);
	LOG_LEVEL str2logLevel(string level);
	void setLogLevel(string level);
	bool isNeedLog(string info);
	string logInternal(string info);
	string appPath();
	void log(string info);
	bool dirCreated;
	LOG_LEVEL logLevel;
	mutex m_lock;
	bool m_bSaveToFile; //工具模式下仅输出到命令行
	fp_logOutputCallback logOutput;
};

extern Clogger logger;

void LOG(const char* pszFmt, ...);
void LOG(string info);