#pragma once
#include "common.h"
#include <map>
#include "json.hpp"

/*
functions：
1.manage the file system of database，use tag and time as data reference
2.data read&write
3.data stats

URL represents the relative path to the database root path
URL begins with \
use dbRoot + URL to compose an absolute path of a db file
*/

using namespace std;


enum EXP_CURVE_TYPE
{
	EXP_MAX,
	EXP_MIN,
	EXP_AVG,
};

struct FILE_ITEM {
	string strName;
	vector<FILE_ITEM*> childItem;
	FILE_ITEM* parentItem;

	FILE_ITEM()
	{
		parentItem = NULL;
	}
};

struct CCurveStatisItem {
	float sum;
	float cnt;
	float avg;
	float max;
	float min;

	CCurveStatisItem()
	{
		sum = 0.0;
		cnt = 0;
		avg = 0.0;
		max = 0.0;
		min = 0.0;
	}
};

struct CPoint2 {
	float x;
	float y;
	CPoint2() :x(0.0), y(0.0)
	{
	}
};


class TIME_CONDITON {
public:
	bool Init(string condition);
	bool Match(string& timeTag);
	bool IsHMS; // only hms is specified, the timespan of each day is selected
	TIME_CONDITON() {
		IsHMS = false;
		startHMS = 0;
		endHMS = 0;
	}

	int startHMS;
	int endHMS;
	SYSTEMTIME stStart;
	SYSTEMTIME stEnd;
	time_t startTime;
	time_t endTime;
};

class TAG_SELECTOR{
public:
	bool init(string tag);
	bool match(string tag);

	string tagExp;
	string regExp;
	string error;
};

class TIME_SELECTOR
{
public:
	TIME_SELECTOR();
	bool Match(string& timeTag);
	bool AmountMatch(int amount);
	bool Init(string time);
	vector<TIME_CONDITON> vecCondition;
	//多个条件组合出来的最宽的数据范围，用于数据库文件遍历
	SYSTEMTIME stStart;
	SYSTEMTIME stEnd;
	time_t startTime;
	time_t endTime;
	int m_dataNum;//存储传入参数的，ne,n代表获取几个数据。
	string error;
};


class CAttriFilter {
public:
	CAttriFilter();
	bool Match(json& jAttri);
	bool Init(string filter);
	vector<string> cdtList;
};

//key is timestamp as 2020-01-01 11:11:11,value is a json string of one data element
#define DB_DATA_SET std::map<string,string>

class database {
public:
	database();
	bool create(string strDBUrl,string name);
	bool Open(string strDBUrl,string name="");
	void Close();

	//crud options
	void INSERT(string strTag, SYSTEMTIME stTime, json& jData,json dataFile = nullptr);
	void INSERT_FILE(string strTag, SYSTEMTIME DataTime, string strDataFile, string suffix = "");
	void INSERT_FILE(string strTag, SYSTEMTIME stTime, char* pData, int iLen, string fmt);//保存
	//time: "2020-02-14~2020-02-15" or "1d1h1m30s"
	//filter: "humidiy==55 && temperature>30"
	//dataSet是一个json数组，数组成员为1个数据元。 meta是元数据，描述数据的一些信息
	bool SELECT(string tag, TIME_SELECTOR& timeSelector, string filter,DB_DATA_SET& result);


	string dataSet2String(DB_DATA_SET& dataSet);
	string getFileUrl(string strTag,SYSTEMTIME date);
	void LoadAllFile_FromPath(string strPath, string strExtType, vector<string>& vecFiles, bool bOnlyName = false, bool bIncludeChild = true);
	void GetFileTreeOfPath(FILE_ITEM* pfi, string strPath);
	string getDBFolder(string strTag, SYSTEMTIME date);
	string getDBFile(string strTag,SYSTEMTIME date);
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
	string m_name; //database name, same as project name
};

extern database db;
