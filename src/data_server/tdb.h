/*
  TDB version 1.0.0
  a minimal time series database based on json files for iot
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include <map>
#ifdef ENABLE_JERRY_SCRIPT
#include "jerryscript.h"
#include "jerry.h"
#endif
#include "yyjson.h"
#include <vector>
#include <string>
using namespace std;


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

struct DB_TIME {
	unsigned short wYear;
	unsigned short wMonth;
	unsigned short wDay;
	unsigned short wHour;
	unsigned short wMinute;
	unsigned short wSecond;
	unsigned short wMilliseconds;
	unsigned short wDayOfWeek;

	void fromUnixTime(time_t iUnix, int milli = 0);
	time_t toUnixTime();
	void setNow();
	string toStampHMS();
	string toYMD();
	string toStr(bool enableMS = true);
	void fromStr(string str);
	static string nowStr();
	static string nowStrWithMilli();
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

struct DE_TIME {
	DB_TIME st;
	time_t tt;
	string strT;
};

class TAG_SELECTOR{
public:
	TAG_SELECTOR() {
		getTag = false;
	}

	bool init(string tag, string rootTag="", string objtype="*");

	bool init(vector<string>& tag, string rootTag = "", string objtype = "*");
	bool match(string tag);//use absolute tag 
	bool singleSelMode();

	string m_org;  
	string m_rootTag; 

	vector<string> fuzzyMatchExp;
	vector<string> fuzzyMatchRegExp;
	vector<string> exactMatchExp; 

	bool getTag; //whether contains key "tag" in de returned

	void setType(string objType);
	bool specifyType();
	string type; //object type
	string error;

	vector<string> tagSet;
};


enum Time_Set_Type {
	TSM_All = 0,
	TSM_Range = 1,
	TSM_First = 2,
	TSM_Last = 3
};

enum Period_Type {
	PT_None = 0,
	PT_Month = 1,
	PT_Day = 2,
	PT_Hour = 3,
};


//time selector format is [Time_Set_Type]@[period type]@[time range]
//head@day@8d  select the first de in each day of 8 days
// 
//short mode - how short mode is parsed
//2020-02-02  -> 2020-02-02 00:00:00~2020-02-02 23:59:59
//2020-02  -> 2020-02-01 00:00:00~2020-02-28 23:59:59
//
//head@TIME_SEL   tail@TIME_SEL

class TIME_SELECTOR
{
public:
	TIME_SELECTOR();
	bool Match(string& deTime);
	bool AmountMatch(size_t amount);
	bool init(string time);
	string shortSel2StardardSel(string time);
	bool parseTimeRange(string time);//use standard time selector as 2020-02-01 00:00:00~2020-02-28 23:59:59
	string getParsedSelector();

	string selector;
	Time_Set_Type timeSetType;
	string timeFmt; //specified time format to return,such as YYYY-MM-DD hh:mm:ss

	string strStart;
	string strEnd;
	DB_TIME stStart;
	DB_TIME stEnd;
	time_t startTime;
	time_t endTime;

	Period_Type periodType;
	int startHMS;
	int endHMS;

	int m_dataNum;//how many de to get
	string error;
	DE_TIME deTime;
};

enum class DOWN_SAMPLING_TYPE {
	DST_None,
	DST_Count,
	DST_Time
};

struct INTERVAL_SELECTOR {
	DOWN_SAMPLING_TYPE type;
	int dsi;   //down sampling de count interval
	int dsti;  //down sampling time length interval in seconds

	INTERVAL_SELECTOR() {
		type = DOWN_SAMPLING_TYPE::DST_None;
		dsi = 0;
		dsti = 0;
	}
};


struct DB_FILE {
	bool boundaryFile;
	bool monthBoundaryFile;
	string data;
	string path;
	string ymd;
	DB_TIME time;
	time_t ttTime;
	string tag;
	yyjson_doc* doc;
	yyjson_val* root;
	string deType;

	bool loadFile();

	DB_FILE(time_t tt,string tag_) {
		monthBoundaryFile = false;
		boundaryFile = false;
		ttTime = tt;
		tag = tag_;
		doc = nullptr;
		root = nullptr;
	}
	~DB_FILE() {
		if(doc)
			yyjson_doc_free(doc);
	}
};

//as the data after aggregate, only time and items are valid
//items is empty before aggregate
struct DE_yyjson {
	yyjson_mut_val* time;  //aggregate by time, is valid after aggregate
	yyjson_mut_val* val;
	string fmtTime; 
	string deTime;

	yyjson_mut_val* de;   
	map<string, yyjson_mut_val*> items; //custom de,when val is not used; only one level json structrue is supported. key store json key,val stores val after aggregate

	DE_yyjson() {
		time = 0;
		val = 0;
		de = 0;
	}
};

class TAG_FILE_SET {
public:
	string tag;
	vector<DB_FILE*> fileList; //sort by time asending

	~TAG_FILE_SET(){
		if (fileList.size() > 0)
		{
			for (int i = 0; i < fileList.size(); i++)
			{
				delete fileList[i];
			}
		}
	}
};

//a data set specified by time and tag
//key tag can be exact tag, or fuzzy tag with * to represent multi tags
class DATA_SET {
public:
	string tag;  // system tag
	string relTag;  //rel tag to return in the query
	string mpName;  
	string colKey;


	map<string, string> aggregate; //聚合操作，key是需要聚合的字段，val是聚合方式
	//分组聚合前数据.key是时间戳，数组是de的数组
	map<string, vector<yyjson_val*>> m_groupedBeforeAggr;
	//不分组聚合前数据
	vector<yyjson_val*> m_beforeAggr;

	//执行聚合函数后数据.或者无需聚合直接放入以下结构
	vector<DE_yyjson*> m_afterAggr;

	DATA_SET() {

	}
	~DATA_SET() {
		if (m_afterAggr.size() > 0) {
			for (int i = 0; i < m_afterAggr.size(); i++)
			{
				delete m_afterAggr[i];
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


class CONDITION_SELECTOR {
public:
	CONDITION_SELECTOR();
	~CONDITION_SELECTOR();
	
#ifdef ENABLE_JERRY_SCRIPT
	void yyVal2jerryVal(yyjson_val* yyVal, jerry_value_t& jerryVal);
	void yyVal2jerryVal(yyjson_mut_val* yyVal, jerry_value_t& jerryVal);
	bool setScriptEngineObj(yyjson_val* jObj, jerry_value_t engineObj);
	bool setScriptEngineObj(yyjson_mut_val* jObj, jerry_value_t engineObj);
	jerry_value_t global_object;
#endif

	bool match(yyjson_val* de);
	bool match(yyjson_mut_val* de);
	bool init(string filter);
	string filterExp;
	bool bEnable;

};




//数据元选择器
struct DE_SELECTOR {
	TIME_SELECTOR timeSel;  //时间选择器
	TAG_SELECTOR tagSel;	//位号选择器
	CONDITION_SELECTOR condition;	//条件选择器
	INTERVAL_SELECTOR interval;		//降采样选择器
	bool ascendingSort;
	string sortKey;
	bool timeFill;   //时间截面位号补全。多个位号，可能在某个时间点只有部分位号有数据，如果该选项为true,将自动为每个时间点补全所有位号的数据，数据值选用上一个时间点的该位号值

	string valType;  //不为空表示将数据库值类型强转成指定类型
	string deType;   //数据元类型，默认为原始数据，可以取 curveIdx 曲线索引; deType不一样，对应的数据文件不一样
	bool isValTypeNumber() {
		if (valType == "float" || valType == "number") {
			return true;
		}
		return false;
	}

	//位号重命名
	string tagLabel; //重命名为 tag还是name
	vector<string> vecTagLable; //指定别名

	string groupby;
	string timeGroupBy;
	bool groupByTime; //是否是按照时间进行分组，如果是按时间分组，查询结果的time字段将被改为时间的分组值
	bool groupByTag;  //是否按照位号进行分组

	//聚合运算
	bool bAggr; //是否进行数据聚合
	map<string,string> aggregate; //应用到所有聚合运算.单个位号聚合或者多个位号统一聚合。key为需要聚合的key，val为需要聚合的方式
	vector<map<string, string>> vecAggregate; //多位号独立聚合模式，每个位号指定独立的聚合方式

	//多列模式
	bool tagAsColume; //将位号作为表的列返回.单列模式或多列模式

	//返回结果的运算
	string calc;

	string getSelectorDesc();

	DE_SELECTOR() {
		ascendingSort = true;
		tagAsColume = false;
		tagLabel = "tag";
		groupByTime = false;
		groupByTag = true;
		bAggr = false;
		timeFill = false;
	}
};

class db_exception : public std::exception {
public:
	const char* what() const noexcept /*noexcept*/ override { return m_error.c_str(); }
	string m_error;
};

struct SORT_FLAG {
	double dbFlag;
	string sFlag;

	SORT_FLAG() {
		dbFlag = 0;
		sFlag = "";
	}

	bool operator>(const SORT_FLAG& other) const {
		if (dbFlag > other.dbFlag) {
			return true;
		}
		else if (dbFlag == other.dbFlag) {
			return sFlag > other.sFlag;
		}
		else {
			return false;
		}
	}

	bool operator<(const SORT_FLAG& other) const {
		if (dbFlag < other.dbFlag) {
			return true;
		}
		else if (dbFlag == other.dbFlag) {
			return sFlag < other.sFlag;
		}
		else {
			return false;
		}
	}

	bool operator==(const SORT_FLAG& other) const {
		if (dbFlag == other.dbFlag && sFlag == other.sFlag) {
			return true;
		}
		else {
			return false;
		}
	}
};

struct SELECT_RLT {
	bool getDE;
	string dataList;
	size_t rowCount;
	size_t deCount;
	size_t fileCount;
	map<SORT_FLAG, yyjson_mut_val*> mapRlt;
	string error;
	string info;
	string query;
	string calcResult; //数据集计算结果

	SELECT_RLT() {
		getDE = true;
		rowCount = 0;
		deCount = 0;
		fileCount = 0;
	}
};

namespace CONST_STR {
	const string val = "val";
	const string time = "time";
	const string tag = "tag";
};

struct  DB_FMT
{
	string deListName;
	string curveIdxListName;
	string curveDeNameSuffix;
	string deItemKey_value;
};

typedef void (*fp_getTagsByTagSelector)(vector<string>& tags, TAG_SELECTOR& tagSelector);


//路径中全部使用斜杠  "/" 不要使用反斜杠 "\\"
class TDB{
public:
	TDB();

	bool Open(string strDBUrl, fp_getTagsByTagSelector f = nullptr,string name="");
	void Close();

	map<string, string> getAggrOpt(yyjson_val* jAggr);
	void parseDESelector(yyjson_val* yyParams, DE_SELECTOR& deSelector,string& err);
	void parseDESelector(string& sParams, DE_SELECTOR& deSelector, string& err);
	int dhmsSpan2Seconds(string timeSpan);
//数据库存储规范
	DB_FMT m_dbFmt;

	fp_getTagsByTagSelector m_getTagsByTagSelector;

//rpc接口
public:
	void rpc_db_select(string& sParams, string& rlt, string& err, string& queryInfo, string org);
	void rpc_db_select(yyjson_val* params, string& rlt,string& err,string& queryInfo, string org);

//接口部分
public:
	void Insert(string strTag, DB_TIME stTime, string& sDe);

	bool Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	
	//select
	bool Select_yyjson(DE_SELECTOR& deSel, SELECT_RLT& result);
	bool readFile(string path, string& data);
	bool Select_yyjson_deFile(string& s);
	bool Select_Step_loadFile(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, SELECT_RLT& result);
	bool Select_Step_loadDataElem(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, vector<DATA_SET*>& outputDataSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_doAggregate(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet,yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	//bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool doAggregateOneGroup(DE_SELECTOR& deSel, std::map<string, string> aggrOpt, vector<yyjson_val*>& src, DE_yyjson& des, yyjson_mut_doc* mut_doc);

	//bool Select_simdjson(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result);
	//bool Update(string tag, TIME stTime, string& sData);
	//bool Update(string tag, TIME stTime, json& jData);
	bool Delete(string tag, DB_TIME stTime);
	bool Count(string tag, TIME_SELECTOR& timeSelector, string filter, int& iCount);

	bool isRelative(string time);

	string rel2abs(string time);

	//bool updateJsonObj(json& jOld, json& jNew);
	void saveDEFile(string strTag, DB_TIME stTime, string deFileUrl) ;
	bool saveDEFile(string tag, DB_TIME stTime, unsigned char* pData, int len,string suffix);

	//path management
public:
	string getPath_dbFile(string strTag, DB_TIME date, string deType = "");
	string changeCharForFileName(string s);
	string getPath_dataFolder(string strTag, DB_TIME date);
	string getPath_deFile(string strTag, DB_TIME stTime);
	string getPath_dbRoot();
	string getName_deFile(string tag, DB_TIME time);

	string parseSuffix(string deFileUrl);
	bool fileExist(string pszFileName);
	string m_name; //database name, same as project name
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
};

extern TDB db;
