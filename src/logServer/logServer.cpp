#include "pch.h"
#include "as.h"
#include "common.h"
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
	string header = "time,object,event,level,detail,org,host";
	vector<string> vecHeader;
	str::split(vecHeader, header, ",");
	tableLog.init("/log/log",vecHeader);
	tableLog.bOneFilePerMonth = true;
}


void logServer::rpc_addLog(json& log, RPC_SESSION session)
{
	if (!log.contains("time"))
	{
		log["time"] = timeopt::nowStr();
	}
	if (session.org != "") //转换为绝对位号存储
	{
		string org = log["org"].get<string>();
		org = TAG::addRoot(org, session.org);
		log["org"] = org;
	}
	tableLog.add(log);
}


string logServer::rpc_queryLog(json params, RPC_SESSION session)
{
	//事件过滤器
	TIME_SELECTOR timeSelector;
	string time = params["time"].get<string>();
	if (!timeSelector.init(time))
		return "[]";

	//组织结构过滤器
	string rootTag = "";
	if (session.user != "")
	{
		json jUser = userMng.getUser(session.user);
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

	//默认逆序查询
	for (int iYear = endYear; iYear >= startYear; iYear--)
	{
		//获得月份遍历范围
		int iEndMonth = 12;   //遍历结束月份   中间的年份其实结束都是0-12. 两端的年份按照指定的开始月份和结束月份
		int iStartMonth = 1; //遍历起始月份
		if (iYear == startYear) iStartMonth = startMonth;
		if (iYear == endYear) iEndMonth = endMonth;


		for (int iMonth = iEndMonth; iMonth >= iStartMonth; iMonth--)
		{
			tableLog.loadFile(tableLog.getFilePath(iYear, iMonth));
			for (int i=tableLog.buffData.size()-1 ;i >=0; i--)
			{
				json j = *tableLog.buffData[i];
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

					//历史记录中的org是绝对位号，删去用户rootTag，转换成用户会话的相对位号。
					j["org"] = TAG::trimRoot(org, rootTag);
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
