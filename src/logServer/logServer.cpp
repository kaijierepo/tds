#include "pch.h"
#include "as.h"
#include "common.hpp"
#include "prj.h"
#include <regex>
#include "rpcHandler.h"
#include "db.h"
#include "tds.h"
#include "logServer/logServer.h"
#include "users/userMng.h"

logServer logSrv;

logServer::logServer(void)
{
	
}


logServer::~logServer(void)
{
}

void logServer::run()
{
	//org表示事件发生的所属组织结构
	string header = "time,object,event,level,detail,org";
	vector<string> vecHeader;
	str::split(vecHeader, header, ",");
	tableLog.init("/log/log",vecHeader);
	tableLog.bOneFilePerMonth = true;
}


void logServer::addLog(json& log)
{
	if (!log.contains("time"))
	{
		log["time"] = timeopt::nowStr();
	}
	tableLog.add(log);
}


string logServer::queryLog(json params, string user)
{
	//事件过滤器
	TIME_SELECTOR timeSelector;
	string time = params["time"].get<string>();
	if (!timeSelector.init(time))
		return "[]";

	//组织结构过滤器
	string rootTag = "";
	if (user != "")
	{
		json jUser = userMng.getUser(user);
		if (jUser != nullptr)//指定用户模式下，tag是相对位号，必须有根的位号。 报警的数据当中，存储的都是完整位号
		{
			rootTag = jUser["org"];
		}
	}

	string dataSet = "[";
	std::lock_guard<mutex> g(m_csLogData);
	int startYear = timeSelector.stStart.wYear;
	int startMonth = timeSelector.stStart.wMonth;
	int endYear = timeSelector.stEnd.wYear;
	int endMonth = timeSelector.stEnd.wMonth;
	int iMonth = 0;
	int iEndMonth = 0;
	for (int iYear = startYear; iYear <= endYear; iYear++)
	{
		if (iYear == startYear) iMonth = startMonth;
		else iMonth = 1;
		if (iYear == endYear) iEndMonth = endMonth;
		else iEndMonth = 12;
		for (; iMonth <= iEndMonth; iMonth++)
		{
			tableLog.loadFile(tableLog.getFilePath(iYear, iMonth));
			for (json*& i : tableLog.buffData)
			{
				json& j = *i;
				string time = j["time"].get<string>();
				string org;
				if(j["org"]!=nullptr)
					org = j["org"].get<string>();
				if (!timeSelector.Match(time))
				{
					continue;
				}

				if (rootTag != "")
				{
					if (org.find(rootTag) == string::npos)
						continue;
				}

				if (dataSet != "[")
					dataSet += "," + j.dump();
				else
					dataSet += j.dump();
			}
		}
	}
	dataSet += "]";
	return dataSet;
}
