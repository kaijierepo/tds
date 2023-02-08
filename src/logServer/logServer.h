#pragma once

#include <mutex>
#include "db.h"
#include "json.hpp"
#include "tds.h"
#include "csvTable.h"


/*
日志权限：
1. 下级组织结构管理员  可以查看到  上级组织结构管理员  对本组织结构的操作
2. 不能看到  上级组织结构管理员 对 其他平级组织结构 的操作

因为如果看不到 系统管理员对本组织结构的操作，他会发现配置变了，但不知道什么原因。
*/

class logServer
{
public:
	void rpc_addLog(json& log,RPC_SESSION session);
	string rpc_queryLog(json params, RPC_SESSION session);

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