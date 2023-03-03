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

日志信息抽象:

         什么时候  在哪       谁      对谁              干了什么

原语      when     where     who     to whom           doWhat

表头字段  time      org      src      object     type  level  info


                                                              
对什么可以省略，事件无非是一个动词。动词可以是及物动词也可以是不及物动词
我吃了面包是一个事件，包含object
我饿了也是一个事件，不包含object
在tds中,object统一用tag表示
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