#pragma once
#include "common.h"
#include <map>
#include "json.hpp"
#include "tds.h"
using json = nlohmann::json;

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


//路径中全部使用斜杠  "/" 不要使用反斜杠 "\\"
class database : public i_database{
public:
	database();
	bool create(string strDBUrl,string name);
	bool Open(string strDBUrl,string name="");
	void Close();

//接口部分
public:
	void INSERT(string strTag, SYSTEMTIME stTime, json& jData,json dataFile = nullptr) ;
	bool SELECT(string tag, TIME_SELECTOR& timeSelector, string filter,DB_DATA_SET& result) ;
	bool updateJsonObj(json& jOld, json& jNew);
	bool UPDATE(string tag, SYSTEMTIME stTime, string& sData);
	bool UPDATE(string tag, SYSTEMTIME stTime, json& jData);
	void saveDEFile(string strTag, SYSTEMTIME stTime, string deFileUrl) ;


//路径管理
public:
	//获得数据库文件db.json的路径
	string getPath_dbFile(string strTag, SYSTEMTIME date);
	//获得数据元文件或者数据库文件的存储文件夹目录
	string getPath_dataFolder(string strTag, SYSTEMTIME date);
	//获得数据元文件或者数据元文件夹的路径
	string getPath_deFile(string strTag, SYSTEMTIME stTime);
	string getPath_dbRoot();
	string getName_deFile(string tag, SYSTEMTIME time);

	string parseSuffix(string deFileUrl);
	string dataSet2String(DB_DATA_SET& dataSet);
	void LoadAllFile_FromPath(string strPath, string strExtType, vector<string>& vecFiles, bool bOnlyName = false, bool bIncludeChild = true);
	void GetFileTreeOfPath(FILE_ITEM* pfi, string strPath);
	string m_name; //database name, same as project name
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
};

extern database db;
