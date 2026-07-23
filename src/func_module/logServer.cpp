#include "pch.h"
#include "tAlmSrv.h"
#include "common.h"
#include "prj.h"
#include "database/tDatabase.h"
#include "tds.h"
#include "logServer.h"
#include "userMng.h"

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
	string header = "time,org,src,object,type,level,info";
	vector<string> vecHeader;
	str::split(vecHeader, header, ",");
	tableLog.init("/log/log",vecHeader);
	tableLog.bOneFilePerMonth = true;
}


void logServer::addLog(json& log) {
	if (!log.contains("time"))
	{
		log["time"] = timeopt::nowStr();
	}
	tableLog.add(log);
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
	if (params.contains("rootTag")) {
		rootTag = params["rootTag"];
	}
	rootTag = TAG::addRoot(rootTag, session.org);

	string dataSet = "[";
	std::lock_guard<mutex> g(m_csLogData);
	int startYear = timeSelector.atomSelList[0].stStart.wYear;
	int startMonth = timeSelector.atomSelList[0].stStart.wMonth;
	int endYear = timeSelector.atomSelList[0].stEnd.wYear;
	int endMonth = timeSelector.atomSelList[0].stEnd.wMonth;

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


				if (j["time"].is_string() == false)
					continue;

				string time,org,object;
				time = j["time"];
				if (j["org"].is_string())
					org = j["org"];
				if (j["object"].is_string())
					object = j["object"];

				if (!timeSelector.Match(time))
				{
					continue;
				}

				
				if (rootTag != "")
				{
					if (object.find(rootTag) == string::npos)
						continue;

					//历史记录中的org是绝对位号，删去用户rootTag，转换成用户会话的相对位号。
					j["object"] = TAG::trimRoot(object, rootTag);
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
