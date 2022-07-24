#pragma once
#include "common.hpp"
#include <map>
#include "json.hpp"
#include "tds.h"
#include "jerryscript.h"
#include "yyjson.h"
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


struct DE_TIME {
	SYSTEMTIME st;
	time_t tt;
	string strT;
};


class TIME_CONDITON {
public:
	bool init(string condition);
	bool Match(string& deTime);
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
	string strStart;
	string strEnd;
};

class TAG_SELECTOR{
public:
	bool init(string tag);
	bool match(string tag);//使用不带根的绝对位号

	string tagExp;
	string regExp;
	string error;
	bool singleMode; //单位号选中模式
};

class TIME_SELECTOR
{
public:
	TIME_SELECTOR();
	bool Match(string& deTime);
	bool AmountMatch(int amount);
	bool init(string time);
	vector<TIME_CONDITON> vecCondition;
	//多个条件组合出来的最宽的数据范围，用于数据库文件遍历
	SYSTEMTIME stStart;
	SYSTEMTIME stEnd;
	time_t startTime;
	time_t endTime;
	int m_dataNum;//存储传入参数的，ne,n代表获取几个数据。
	string error;
	DE_TIME deTime;
};

enum DOWN_SAMPLING_TYPE {
	DST_None,
	DST_Count,
	DST_Time
};

struct DOWN_SAMPLING_PARAM {
	DOWN_SAMPLING_TYPE type;
	int dsi;   //降采样元素个数间隔
	int dsti;  //降采样的时间间隔 单位秒

	DOWN_SAMPLING_PARAM() {
		type = DST_None;
		dsi = 0;
		dsti = 0;
	}
};


struct DB_FILE {
	bool boundaryFile;
	string data;
	string path;
	string ymd;
	SYSTEMTIME time;
};

class DB_FILE_SET {
public:
	vector<DB_FILE*> fileList;
	~DB_FILE_SET() {
		if (fileList.size() > 0)
		{
			for (int i = 0; i < fileList.size(); i++)
			{
				delete fileList[i];
			}
		}
	}
};

struct HMS_STR {
	unsigned char hourH;
	unsigned char hourL;
	unsigned char semicolon1;
	unsigned char minH;
	unsigned char minL;
	unsigned char semicolon2;
	unsigned char secH;
	unsigned char secL;

	int getTotalSec() {
		return (hourH * 10 + hourL) * 3600 + (minH * 10 + minL) * 60 + (secH * 10 + secL);
	}
};


class ATTRI_SELECTOR {
public:
	ATTRI_SELECTOR();
	~ATTRI_SELECTOR();
#ifdef ENABLE_JERRY_SCRIPT
	bool setScriptEngineObj(yyjson_mut_val* jObj, jerry_value_t engineObj);
#endif
	bool match(string& de); //检查一个de是否满足条件
	bool match(yyjson_mut_val* de);
	bool init(string filter);
	string filterExp;
	bool bEnable;
	jerry_value_t global_object;
};

class db_exception : public std::exception {
public:
	const char* what() const noexcept /*noexcept*/ override { return m_error.c_str(); }
	string m_error;
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
	void Insert(string strTag, SYSTEMTIME stTime, json& jData,json dataFile = nullptr) ;
	bool Select_yyjson(vector<string> tagSet, TIME_SELECTOR& timeSelector, string filter, string& result, DOWN_SAMPLING_PARAM dsp); //dsi = downsamplingInterval
	bool Select_simdjson(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result);
	bool Select(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result);
	bool Update(string tag, SYSTEMTIME stTime, string& sData);
	bool Update(string tag, SYSTEMTIME stTime, json& jData);
	bool Delete(string tag, SYSTEMTIME stTime);

	bool updateJsonObj(json& jOld, json& jNew);
	void saveDEFile(string strTag, SYSTEMTIME stTime, string deFileUrl) ;
	bool saveDEFile(string tag, SYSTEMTIME stTime, unsigned char* pData, int len,string suffix);
//路径管理
public:
	//获得数据库文件db.json的路径
	string getPath_dbFile(string strTag, SYSTEMTIME date);
	string changeCharForFileName(string s);
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
