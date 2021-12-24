#pragma once
#include "tdscore.h"
#include <mutex>
#include "db.h"
#include "json.hpp"
#include "tds.h"
#include "csvTable.h"


class logServer
{
public:
	void addLog(json& log);
	string queryLog(json params, string user);

public:
	logServer(void);
	~logServer(void);
	static logServer& Inst() {
		static logServer inst;
		return inst;
	}
	void run();

	csvTable tableLog;
	std::mutex m_csLogData;
};

extern logServer logSrv;