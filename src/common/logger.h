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
	LL_KEYINFO = 3,
	LL_NONE = 4
};

namespace LOG_CODEC {
	const string gb2312 = "gb2312";
	const string utf8 = "utf8";
}

typedef void (*fp_logOutputCallback)(string text);

class Clogger
{
public:
	Clogger();
	void init();

	LOG_LEVEL str2logLevel(string level);

	void setLogLevel(string level);
	LOG_LEVEL getLogLevel(string& info);

	void setConsoleTextColor(LOG_LEVEL ll);

	string logInternal(string info, bool writeToFile = true); //is bForceWrite == true,ignore level

	void log(string info,bool writeToFile = true);
	string m_InterfaceCodec = LOG_CODEC::utf8;  // codec for function interface
	bool dirCreated;
	bool m_bSaveToFile; //save to console mode is supported
	bool m_bEnable;

	mutex m_lock;
	string m_strLogDir;
	LOG_LEVEL logLevel;

	fp_logOutputCallback logOutput;

};

extern Clogger logger;

void LOG(const char* pszFmt, ...);
void LOG(string info);
void LOG_C(char* p);

int _vscprintf_cross_log(const char* format, va_list pargs);
