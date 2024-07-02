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

class TDB;
/*
functions：
1.manage the file system of database，use tag and time as data reference
2.data read&write
3.data stats

URL represents the relative path to the database root path
URL begins with \
use dbRoot + URL to compose an absolute path of a db file
*/

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
	string toStampFull();
	string toYMD();
	string toStr(bool enableMS = true);
	bool fromStr(string str);
	static string nowStr();
	static string nowStrWithMilli();

	bool operator==(const DB_TIME& right) const {
		return 0 == memcmp(this, &right,sizeof(DB_TIME));
	}

	bool operator>(const DB_TIME& right) const {
		if (wYear > right.wYear)return true;
		if (wYear < right.wYear)return false;
		if (wMonth > right.wMonth)return true;
		if (wMonth < right.wMonth)return false;
		if (wDay > right.wDay)return true;
		if (wDay < right.wDay)return false;
		if (wHour > right.wHour)return true;
		if (wHour < right.wHour)return false;
		if (wMinute > right.wMinute) return true;
		if (wMinute < right.wMinute) return false;
		if (wSecond > right.wSecond)return true;
		if (wSecond < right.wSecond)return false;
		if (wMilliseconds > right.wMilliseconds)return true;
		if (wMilliseconds < right.wMilliseconds)return false;
		return false;
	}
	bool operator>=(const DB_TIME& right) const {
		if (*this > right || *this == right) {
			return true;
		}
		return false;
	}
	bool operator<(const DB_TIME& right) const {
		if (wYear < right.wYear)return true;
		if (wYear > right.wYear)return false;
		if (wMonth < right.wMonth)return true;
		if (wMonth > right.wMonth)return false;
		if (wDay < right.wDay)return true;
		if (wDay > right.wDay)return false;
		if (wHour < right.wHour)return true;
		if (wHour > right.wHour)return false;
		if (wMinute < right.wMinute) return true;
		if (wMinute > right.wMinute) return false;
		if (wSecond < right.wSecond)return true;
		if (wSecond > right.wSecond)return false;
		if (wMilliseconds < right.wMilliseconds)return true;
		if (wMilliseconds > right.wMilliseconds)return false;
		return false;
	}
	bool operator<=(const DB_TIME& right)const {
		if (*this < right || *this == right) {
			return true;
		}
		return false;
	}
};

struct DE_TIME {
	DB_TIME st;
	time_t tt;
	string strT;
};

struct DB_TIME_RANGE {
	DB_TIME start;
	DB_TIME end;
	void* p;
};

class TAG_SELECTOR{
public:
	TAG_SELECTOR() {
		getTag = false;
	}

	//objType==* select all obj who's type is set.
	//objType=="" select all obj
	bool init(string tag, string rootTag="", string objtype="",string level="*");
	bool init(vector<string>& tag, string rootTag = "", string objtype = "", string level = "*");
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
	string level;
	string error;
	string language;

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
DB_TIME_RANGE parseTimeRange(string timeExp);

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
	TDB* pOwnerDB;

	bool loadFile();

	DB_FILE(time_t tt,string tag_,TDB* pOwner) {
		monthBoundaryFile = false;
		boundaryFile = false;
		ttTime = tt;
		tag = tag_;
		doc = nullptr;
		root = nullptr;
		pOwnerDB = pOwner;
	}
	~DB_FILE() {
		if(doc)
			yyjson_doc_free(doc);
	}
};

//as the data after aggregate, only time and items are valid
//items is empty before aggregate
struct DE_yyjson {
	//when as an orignal de, deTime is standard time format with millisecond like 2023-10-01 12:00:00.001
	//when as an aggr result de, deTime is time group key; groupby day -> 2023-10-01  groupby hour ->2023-10-01 12 
	string deTime; 
	yyjson_mut_val* val;
	string fmtTime; 
	
	yyjson_mut_val* de;   
	map<string, yyjson_mut_val*> items; //custom de,when val is not used; only one level json structrue is supported. key store json key,val stores val after aggregate

	DE_yyjson() {
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


	map<string, vector<string>> aggregate; //key is the json key to aggregate，val is aggregate mode (max,min,diff ...)
	//grouped data before aggregate key is time stamp ,val is de vector
	map<string, vector<yyjson_val*>> m_groupedBeforeAggr;
	//ungrouped data before aggregate
	vector<yyjson_val*> m_beforeAggr;

	//data after aggregate or without aggr option
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
	TIME_SELECTOR timeSel;  
	TAG_SELECTOR tagSel;	
	CONDITION_SELECTOR condition;	
	INTERVAL_SELECTOR interval;		
	bool ascendingSort;
	string sortKey;
	bool timeFill;   //in a time section ,data is not exist in some tag. use value before this time section to fill in this time section

	string valType;  //transform de val to specified value
	string deType;   //get de by default;  curveIdx to get curveIdx in curveList file
	bool isValTypeNumber() {
		if (valType == "float" || valType == "number") {
			return true;
		}
		return false;
	}

	string tagLabel; //rename tag
	vector<string> vecTagLable; //muti rename

	string groupby;
	string timeGroupBy;
	bool groupByTime; //time in selected de is set a time group key such as "2023-09-01 11" when groupby "hour"
	bool groupByTag;  

	bool bAggr; 
	map<string,vector<string>> aggregate; //global aggr option. key is the json key to aggr, val is aggr type
	vector<map<string, vector<string>>> vecAggregate; //specified each tag in its own aggregate type
	map<string, vector<DB_TIME_RANGE>> mapTimeSlots;  //named time slots,used in "increase" aggr mode
	bool tagAsColume; //return data set as a table.each tag as a columne

	//use function to calc the selected dataset
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
	string calcResult; 

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
	const string url = "url";
};

struct  DB_FMT
{
	string deListName;
	string curveIdxListName;
	string curveDeNameSuffix;  //The suffix contains "."
	string jsonIdxListName;
	string jsonDeNameSuffix;  //The suffix contains "."
	string deItemKey_value;
};

enum DB_TIME_UNIT {
	NONE = 0,
	BY_DAY,
	BY_MONTH,
	BY_YEAR
};

typedef void (*fp_getTagsByTagSelector)(vector<string>& tags, TAG_SELECTOR& tagSelector);

namespace DB_STR {
	wstring utf8_to_utf16(string instr);
	string gb_to_utf8(string instr);
}

inline string JSON_STR_VAL(string s) {
	return "\"" + s + "\"";
}

//use  "/"  but not "\\" in a path
class TDB{
public:
	TDB();

//interface
	bool Open(string strDBUrl, fp_getTagsByTagSelector f = nullptr,string name="");
	bool Open_gbk(string strDBUrl, fp_getTagsByTagSelector f = nullptr, string name = "");
	DB_FMT m_dbFmt;
	DB_TIME_UNIT m_timeUnit;

	void rpc_db_insert(string& sParams, string& rlt, string& err, string& queryInfo, string org = "");
	void rpc_db_insert(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org = "");

	void rpc_db_select(string& sParams, string& rlt, string& err, string& queryInfo, string org = "");
	void rpc_db_select(yyjson_val* params, string& rlt,string& err,string& queryInfo, string org = "");

	void rpc_db_update(string& sParams, string& rlt, string& err, string& queryInfo, string org = "");
	void rpc_db_update(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org = "");

	bool Update(string tag, DB_TIME stTime, yyjson_val& jData);

	void rpc_db_delete(string& sParams, string& rlt, string& err, string& queryInfo, string org = "");
	void rpc_db_delete(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org = "");

	bool Select(DE_SELECTOR& deSel, SELECT_RLT& result);
	void Insert(string strTag, DB_TIME stTime, double& dbVal);
	void Insert(string strTag, DB_TIME stTime, int& iVal);
	void Insert(string strTag, DB_TIME stTime, long long iVal);
	void Insert(string strTag, string& sDe,DB_TIME* stTime = nullptr );
	bool Delete(string tag, DB_TIME stTime);
	int Update(string tag, DB_TIME stTime, yyjson_val* yyVal, yyjson_val* updateFile);

	TDB* getChildDB(string dbName);
	map<string, TDB*> m_childDB;
//private func
public:
	//param parse
	map<string, vector<string>> getAggrOpt(yyjson_val* jAggr);
	void parseDESelector(yyjson_val* yyParams, DE_SELECTOR& deSelector, string& err);
	void parseDESelector(string& sParams, DE_SELECTOR& deSelector, string& err);
	int dhmsSpan2Seconds(string timeSpan);
	//insert
	void InsertValJsonStr(string strTag, DB_TIME stTime, string& sVal);
	//select
	bool Select_Step_loadFile(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, SELECT_RLT& result);
	bool Select_Step_loadDataElem(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, vector<DATA_SET*>& outputDataSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_doAggregate(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet,yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	string getDeFilesFolder(string& deListFolder, DB_TIME& time);
	//bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool doAggregateOneGroup(DE_SELECTOR& deSel, std::map<string, vector<string>> aggrKeyType, string groupKey,vector<yyjson_val*>& src, DE_yyjson& des, yyjson_mut_doc* mut_doc);
	//double doAggrOneGroup_increase(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup);
	map<string, double> doAggrOneGroup_increase_withTimeSlots(DE_SELECTOR& deSel, string& aggrKey, string groupKey, vector<yyjson_val*>& deGroup);
	double doAggrOneGroup_sum(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup);
	void doAggrOneGroup_duration(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup, yyjson_mut_val*& pAggrRlt, yyjson_mut_doc* yydoc);
	double doAggrOneGroup_diff(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup);
	double doAggrOneGroup_avg(DE_SELECTOR& deSel, string& aggrKey, vector<yyjson_val*>& deGroup);
	//update
	//bool Update(string tag, TIME stTime, string& sData);
	//bool Update(string tag, TIME stTime, json& jData);
	bool Count(string tag, TIME_SELECTOR& timeSelector, string filter, int& iCount);

	void getDeTime(yyjson_mut_val* yyTime, string& deTime);

	//bool updateJsonObj(json& jOld, json& jNew);
	string saveDEFile(yyjson_val* yyvFileInfo, string path,DB_TIME dbTime,string& type);

	//path management
	string getPath_dbFile(string strTag, DB_TIME date, string deType = "");
	string changeCharForFileName(string s);
	string getPath_dataFolder(string strTag, DB_TIME date);
	string getPath_dataFolder_NO_DB(string strTag, DB_TIME date);
	string getPath_deFile(string strTag, DB_TIME stTime);
	string getPath_dbRoot();
	string getName_deFile(string tag, DB_TIME time);

	string parseSuffix(string deFileUrl);
	bool fileExist(string pszFileName);
	string m_name; //database name, same as project name
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
	fp_getTagsByTagSelector m_getTagsByTagSelector;
	bool m_isGbk;
};

extern TDB db;
