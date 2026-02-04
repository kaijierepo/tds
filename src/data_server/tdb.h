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
#include <set>
#include <mutex>
#ifdef ENABLE_QJS
#include <script/cutils.h>
#include <script/quickjs-libc.h>
#include <script/quickjs.h>
#endif
#include "yyjson.h"
#include <vector>
#include <string>
#include <functional>
#include <thread>
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

	DB_TIME() {
		memset(this, 0, sizeof(DB_TIME));
	}
	DB_TIME(unsigned short year, unsigned short month, unsigned short day, unsigned short hour, unsigned short minute, unsigned short second, unsigned short millisecond)
	{
		wYear = year;
		wMonth = month;
		wDay = day;
		wHour = hour;
		wMinute = minute;
		wSecond = second;
		wMilliseconds = millisecond;
	}

	void fromUnixTime(time_t iUnix, int milli = 0);
	time_t toUnixTime() const;
	void setNow();
	string toStampHMS() const;
	string toStampFull() const;
	string toYMD() const;
	void clearHMS() {
		wHour = 0;
		wMinute = 0;
		wSecond = 0;
		wMilliseconds = 0;
	}
	void setMaxHMS() {
		wHour = 23;
		wMinute = 59;
		wSecond = 59;
		wMilliseconds = 999;
	}
	string toStr(bool enableMS = true) const;
	bool fromStr(string str);
	int getTimePassSecond();
	static string nowStr(bool enalbeMs = true);
	static string nowStrWithMilli();

	bool operator==(const DB_TIME& right) const {
		return 0 == memcmp(this, &right,sizeof(DB_TIME));
	}
	// 判断是否为闰年
	bool isLeapYear() const {
		return (wYear % 4 == 0 && wYear % 100 != 0) || (wYear % 400 == 0);
	}

	// 获取某个月的最大天数
	unsigned short getMaxDayOfMonth() const {
		static const unsigned short daysInMonth[] = {
			31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
		};
		if (wMonth == 2 && isLeapYear()) {
			return 29;
		}
		return daysInMonth[wMonth - 1];
	}
	DB_TIME& operator+=(const DB_TIME& right)
	{
		// 毫秒
		wMilliseconds += right.wMilliseconds;
		if (wMilliseconds >= 1000) {
			wMilliseconds -= 1000;
			wSecond += 1;
		}

		// 秒
		wSecond += right.wSecond;
		if (wSecond >= 60) {
			wSecond -= 60;
			wMinute += 1;
		}

		// 分钟
		wMinute += right.wMinute;
		if (wMinute >= 60) {
			wMinute -= 60;
			wHour += 1;
		}

		// 小时
		wHour += right.wHour;
		if (wHour >= 24) {
			wHour -= 24;
			wDay += 1;
		}

		// 天
		wDay += right.wDay;
		while (wDay > getMaxDayOfMonth()) {
			wDay -= getMaxDayOfMonth();
			wMonth += 1;
			if (wMonth > 12) {
				wMonth = 1;
				wYear += 1;
			}
		}

		// 月
		wMonth += right.wMonth;
		if (wMonth > 12) {
			wMonth -= 12;
			wYear += 1;
		}

		// 年
		wYear += right.wYear;

		// 星期（可选，根据需求计算）
		// 这里假设 wDayOfWeek 不需要更新

		return *this;
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


	string tagSel;
	vector<string> fuzzyMatchExp;
	vector<string> fuzzyMatchRegExp;
	vector<string> exactMatchExp; 

	bool getTag; //whether contains key "tag" in de returned

	void setType(string objType);
	bool specifyType();
	string type; //object type
	string level;
	string error;
	string ioType;
	string selLanguage;
	string rltLanguage;
};


enum Time_Atom_Sel_Type {
	TSM_All = 0,
	TSM_Range = 1,
	TSM_AnyPoint = 2,  
	TSM_First = 3,
	TSM_Last = 4
};

enum DB_VAL_TYPE {
	DBV_DOUBLE,
	DBV_INT,
	DBV_BOOL
};

struct DB_VAL {
	DB_VAL_TYPE  type;
	double dbVal;
	bool bVal;
	int iVal;
};


//time selector format is [Time_Set_Type]@[period type]@[time range]
//head@day@8d  select the first de in each day of 8 days
// 
//short mode - how short mode is parsed
//2020-02-02  -> 2020-02-02 00:00:00~2020-02-02 23:59:59
//2020-02  -> 2020-02-01 00:00:00~2020-02-28 23:59:59
//
//head@TIME_SEL   tail@TIME_SEL

struct TIME_SELECTOR_ATOM {
	bool Match(string& deTime);
	bool init(string time);
	string selector;
	Time_Atom_Sel_Type timeSetType;
	string strStart;
	string strEnd;
	DB_TIME stStart;
	DB_TIME stEnd;
	time_t startTime;
	time_t endTime;
	bool snapShot;
	string shortSel2StardardSel(string time);
	bool parseTimeRange(string time);//use standard time selector as 2020-02-01 00:00:00~2020-02-28 23:59:59
	string getParsedSelector();

	TIME_SELECTOR_ATOM() {
		startTime = 0;
		endTime = 0;
		snapShot = false;
	}
};

class TIME_SELECTOR
{
public:
	TIME_SELECTOR();
	bool Match(string& deTime);
	bool AmountMatch(size_t amount);
	bool init(vector<string> time);
	bool init(string time);

	bool isRange();

	bool isVarTimePoint();

	vector<TIME_SELECTOR_ATOM> atomSelList;

	string timeFmt; //specified time format to return,such as YYYY-MM-DD hh:mm:ss
	int m_dataNum;//how many de to get
	string timeRangeByDataNum;
	string error;
	DE_TIME deTime;
	bool enable;
	bool snapShot;
};
DB_TIME_RANGE parseTimeRange(string timeExp);

enum class INTERVAL_DOWN_SAMPLING_TYPE {
	DST_None,
	DST_Count,
	DST_Time
};

struct DOWN_SAMPLING_SELECTOR {
	INTERVAL_DOWN_SAMPLING_TYPE intervalType;
	int dsi;   //down sampling de count interval
	int dsti;  //down sampling time length interval in seconds
	bool minDiffDownSampling;
	double minDiff;

	DOWN_SAMPLING_SELECTOR() {
		intervalType = INTERVAL_DOWN_SAMPLING_TYPE::DST_None;
		dsi = 0;
		dsti = 0;
		minDiffDownSampling = false;
	}
};

struct DOWN_SAMPLING_CHECK {
	INTERVAL_DOWN_SAMPLING_TYPE intervalType;
	bool minDiffDownSampling;
	bool selBy_timeInterval;
	bool selBy_countInterval;
	bool selBy_minDiff;

	DOWN_SAMPLING_CHECK() {
		init();
	}

	void init() {
		selBy_timeInterval = false;
		selBy_countInterval = false;
		selBy_minDiff = false;
	}

	bool isSel() {
		if (minDiffDownSampling) {
			if (selBy_minDiff)
				return true;
			else if (selBy_timeInterval)
				return true;
			else if (selBy_countInterval)
				return true;
		}
		else {
			
		}
	}
};

struct DB_TIME_SPAN {
	DB_TIME start;
	DB_TIME end;
};


struct DB_FILE {
	bool boundaryFile;
	bool monthBoundaryFile;
	string data;
	string path;
	string ymd;
	DB_TIME time;
	string tag;
	yyjson_doc* doc;
	yyjson_val* root;
	string deType;
	TDB* pOwnerDB;

	bool loadFile();

	bool isDataList();  //datalist file, curve index file ,not curve file. only data list is buffered in tdb

	DB_FILE(DB_TIME t, string tag_, TDB* pOwner) {
		monthBoundaryFile = false;
		boundaryFile = false;
		time = t;
		tag = tag_;
		doc = nullptr;
		root = nullptr;
		pOwnerDB = pOwner;
	}

	DB_FILE(time_t tt,string tag_,TDB* pOwner) {
		monthBoundaryFile = false;
		boundaryFile = false;
		time.fromUnixTime(tt);
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

#include <unordered_map>
#include <atomic>

class DB_LOCK {
public:
	std::mutex mutex_;
	DB_TIME last_used_;
	std::atomic<int> ref_count_{ 0 };

	DB_LOCK() {
		last_used_.setNow();
	}
};

class DB_LOCK_POOL {
public:
	static DB_LOCK_POOL& instance() {
		static DB_LOCK_POOL pool;
		return pool;
	}

	static int lockTTL;

	DB_LOCK& get_lock(const std::string& path) {
		std::lock_guard<std::mutex> lock(pool_mutex_); 
		auto& entry = locks_[path];
		entry.last_used_.setNow();
		entry.ref_count_++; // cleaner thread can not check ref_count because pool_mutex_, so in using lock will not be deleted
		return entry;
	}

	void release_lock(DB_LOCK& lock) {
		//do not need to lock pool_mutex_,not thread safe ref_count option.
		//release_lock is called ,then clean thread try to check ref_count,do not clean,then ref_count--
		//not using lock will not be cleaned, do not cause problem;clean in using lock causes problem
		lock.ref_count_--;
	}

	DB_LOCK_POOL() {
		cleaner_.store(true);
		std::thread([this]() {
			while (cleaner_.load()) {
				std::this_thread::sleep_for(std::chrono::seconds(DB_LOCK_POOL::lockTTL));
				std::lock_guard<std::mutex> lock(pool_mutex_);
				auto now = std::chrono::steady_clock::now();
				for (auto it = locks_.begin(); it != locks_.end();) {
					// in pool_mutex_ ,keep ref_count_ check thread safe
					if (it->second.ref_count_ == 0 && it->second.last_used_.getTimePassSecond() > DB_LOCK_POOL::lockTTL) {
						it = locks_.erase(it);
					}
					else {
						++it;
					}
				}
			}
			}).detach();
	}

	~DB_LOCK_POOL() {
		cleaner_.store(false);
	}

	std::mutex pool_mutex_; //keep locks_ thread safe, keep clean and getLock thread safe
	std::unordered_map<std::string, DB_LOCK> locks_;
	std::atomic<bool> cleaner_{ false };
};

struct DB_LOCK_GUARD {
	DB_LOCK* lock_;

	static bool enable;

	DB_LOCK_GUARD(const std::string& path) {
		if (DB_LOCK_GUARD::enable) {
			lock_ = &DB_LOCK_POOL::instance().get_lock(path);
			lock_->mutex_.lock();
		}
	}

	~DB_LOCK_GUARD() {
		if (DB_LOCK_GUARD::enable) {
			lock_->mutex_.unlock();
			DB_LOCK_POOL::instance().release_lock(*lock_);
		}
	}
};

//as the data after aggregate, only time and items are valid
//items is empty before aggregate
struct DE_yyjson {
	//when as an orignal de, deTime is standard time format with millisecond like 2023-10-01 12:00:00.001
	//when as an aggr result de, deTime is time group key; groupby day -> 2023-10-01  groupby hour ->2023-10-01 12 
	//if not grouped ,deTime is time range
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
	string dbFileTag;
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
	map<string, vector<yyjson_val*>> m_origDeGrouped;
	//ungrouped data before aggregate or without aggr option or no de mutation
	vector<yyjson_val*> m_orgDe;

	//data after aggregate 
	vector<DE_yyjson*> m_afterAggr;

	//   saved custom groupby columns
	map<string, map<string, string>> customGroupValues;
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
	
#if ENABLE_QJS
	bool evaluate_condition(const char* json_str, size_t json_len);
	JSContext* global_object;
#endif

	bool match(yyjson_val* de);
	bool match(yyjson_mut_val* de);
	bool init(string filter);
	string filterExp;
	bool bEnable;

};


struct TIME_RELATION {
	string type;
	int offset;
	int count;
};

struct WHEN_SELECTOR {
	string tag;
	string match;
	bool whenStatus;  //false: when match   true: when status
	DB_VAL status;
	CONDITION_SELECTOR condition;
	vector<DB_TIME_SPAN> eventTimeSlot;
	vector<TIME_RELATION> relation;
};

struct DE_SELECTOR {
	TIME_SELECTOR timeSel;  
	TAG_SELECTOR tagSel;	
	CONDITION_SELECTOR condition;	
	DOWN_SAMPLING_SELECTOR downSamplingSel;		
	WHEN_SELECTOR whenSel;
	bool ascendingSort;
	string sortKey;
	bool timeFill;   //in a time section ,data is not exist in some tag. use value before this time section to fill in this time section
	string splitBy;    //split into multiple result data set

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
	vector<string> customGroupBy;  // 自定义分组字段，如 ["acqType"]
	map<string, string> customGroupAlias; // 自定义分组字段别名

	bool bAggr; 
	map<string,vector<string>> aggregate; //global aggr option. key is the json key to aggr, val is aggr type
	vector<map<string, vector<string>>> vecAggregate; //specified each tag in its own aggregate type
	map<string, vector<DB_TIME_RANGE>> mapTimeSlots;  //named time slots,used in "increase" aggr mode
	bool tagAsColume; //return data set as a table.each tag as a columne

	//use function to calc the selected dataset
	string calc;
	string curvePtAggr;
	string baseCurve;
	string getSelectorDesc();

	int offset = 0;
	int limit = 0;
	int pageNo = 1;
	int pageSize = 0;

	//map<string, string> mapSelfParms;
	int theLimit=-9999;
	int self_interval = -9999;

	DE_SELECTOR() {
		ascendingSort = true;
		tagAsColume = false;
		tagLabel = "tag";
		groupByTime = false;
		groupByTag = true;
		bAggr = false;
		timeFill = false;
	}

	bool init(const string& params,string& err);
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
	map<SORT_FLAG, yyjson_mut_val*> rltDataSet; //single result data set
	map<string, map<SORT_FLAG, yyjson_mut_val*>> rltDataSetList; //db.select uses splitBy
	vector<yyjson_mut_val*> rltDataSetVec;  //single result data set ,do not need order
	map<string, vector<yyjson_mut_val*>> rltDataSetVecList; //db.select uses splitBy
	yyjson_mut_doc* rlt_mut_doc;
	string error;
	string info;
	string query;
	string calcResult; 

	vector<string> tagSet;  //in query language
	vector<string> dbFileTagSet;  //in disk storage language
	vector<vector<DATA_SET*>*>  dataSetBuff; 
	vector<TAG_FILE_SET*> tagFileSet;

	SELECT_RLT() {
		rlt_mut_doc = yyjson_mut_doc_new(nullptr);
		getDE = true;
		rowCount = 0;
		deCount = 0;
		fileCount = 0;
	}

	~SELECT_RLT() {
		if (rlt_mut_doc) {
			yyjson_mut_doc_free(rlt_mut_doc);
		}
		//release src
		for (int i = 0; i < dataSetBuff.size(); i++)
		{
			vector<DATA_SET*>* p = dataSetBuff[i];
			for (int j = 0; j < p->size(); j++)
			{
				DATA_SET* fSet = p->at(j);
				delete fSet;
			}
			delete p;
		}
		//release file data
		for (int i = 0; i < tagFileSet.size(); i++)
		{
			delete tagFileSet[i];
		}

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
	string deListStatisticsName;
	string curveIdxListName;
	string curveDeNameSuffix;  //The suffix contains "."
	string deItemKey_value;
	string language;    // zh for chinese en for english
	string dbRootTag;   // root tag of tag in query. when multi db engines use one db folder

	DB_FMT() {
		language = "zh";
		deListName = "db.json";
		curveIdxListName = "db.curve.json";
		curveDeNameSuffix = ".curve.json";
		deItemKey_value = "val";
	}
};

enum DB_TIME_UNIT {
	NONE = 0,
	BY_DAY,
	BY_MONTH,
	BY_YEAR
};

typedef void (*fp_getTagsByTagSelector)(TAG_SELECTOR& tagSelector,SELECT_RLT& rlt);

namespace DB_STR {
	wstring utf8_to_utf16(string instr);
	string utf16_to_utf8(wstring instr);
	string gb_to_utf8(string instr);
	string utf8_to_gb(string instr);
	wstring gb_to_utf16(string instr);
}

namespace DB_FS {
	bool readFile(string path, string& data);
	bool readFile(string path, char*& pData, int& len);
	void createFolderOfPath(string strFile);
	bool writeFile(string path, char* data, size_t len);
	bool writeFile(string path, unsigned char* data, size_t len);
	bool writeFile(string path, string& data);
	bool deleteFile(string path);
	void DeleteDirectoryContents(const std::string& dirPath);
	void deleteDirectory(string& dirPath);
	bool copyFile(const std::string& src, const std::string& dest);
	bool rename(const std::string& oldPath, const std::string& newPath);

	string normalizationPath(string& s);

	struct FILE_INFO {
		string modifyTime;
		string createTime;
		size_t len;
		string accessTime;
		string name;
		string path;
		string folderPath;
	};

	void getFolderList(vector<FILE_INFO>& list, string strFolder, bool recursive = false);
	void getFileList(vector<FILE_INFO>& list, string strFolder, bool recursive = false, string suffix = "*", vector<string>* exclude = nullptr);
	void getFileList(vector<string>& list, string strFolder, bool includeFolder = false, bool recursive = false);
}

struct FILE_BUFF {
	DB_TIME lastActive;
	string data;
	FILE_BUFF() {

	}
	~FILE_BUFF() {
	}
};

class FS_BUFF {
public:
	std::mutex m_csFsb;
	std::map<string,FILE_BUFF*> m_mapFsBuff;

	bool readFile(string path, string& data);
	bool writeFile(string path, unsigned char* data, size_t len);
};

inline string JSON_STR_VAL(const string& s) {
	return "\"" + s + "\"";
}

#define DB_OK "\"ok\""

enum DE_JSON_TYPE {
	DE_J_ARR,
	DE_J_OBJ
};

//use  "/"  but not "\\" in a path
class TDB{
public:
	TDB();

//interface
	bool Open(string strDBUrl, fp_getTagsByTagSelector f = nullptr,string name="");
	bool Open_gbk(string strDBUrl, fp_getTagsByTagSelector f = nullptr, string name = "");
	bool setBufferTTL(string bufferTTL);
	bool m_enableDB = true;
	DB_FMT m_dbFmt;
	bool m_bEnableFsBuff;
	FS_BUFF m_FsBuff;
	int m_bufferTTL;
	DB_TIME_UNIT m_timeUnit;
	bool m_bAutoUpgrade;

	bool handleRpc(const string& method,yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language);

	// time series db function
	void rpc_db_insert(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language);
	void rpc_db_insert(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language);

	void rpc_db_select(string& sParams, string& rlt, string& err, string& queryInfo, string org,string language);
	void rpc_db_select(yyjson_val* params, string& rlt,string& err,string& queryInfo, string org,string language);

	void rpc_db_update(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language);
	void rpc_db_update(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language);

	void rpc_db_merge(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language);
	void rpc_db_merge(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language);
	
	bool Update(string tag, DB_TIME stTime, yyjson_val& jData);

	void rpc_db_delete(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language);
	void rpc_db_delete(yyjson_val* params, string& rlt, string& err, string& queryInfo, string org, string language);

	void rpc_db_saveImage(string& sParams, string& rlt, string& err, string& queryInfo, string org, string language);
	void rpc_db_getBufferStatus(string& rlt, string& err);
	void rpc_db_setConf(string& sParams, string& rlt, string& err);

	//table db function
	void rpc_db_table_insert(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language);
	void rpc_db_table_delete(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language);
	void rpc_db_table_update(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language);
	void rpc_db_table_select(yyjson_val* params, string& rlt, string& err, string& queryInfo, const string& org, const string& language);

	bool Select(DE_SELECTOR& deSel, SELECT_RLT& result);

	bool tableUpdate(string tableName, vector<string>& match, vector<yyjson_val*>& updateData, string& err);
	bool tableUpdate(string tableName, vector<string>& match, vector<string>& updateData,string& err);
	bool tableUpdate(string tableName, const string& match, const string& updateData,string& err);
	bool tableInsert(string tableName, const string& row, string& err);
	bool tableInsert(string tableName, yyjson_val* row, string& err);
	bool tableSelect(string tableName, vector<string>& match, string& rlt, string& err);

	// db.insert functions
	// insert basic val type
	void Insert(string strTag, DB_TIME stTime, double& dbVal);
	void Insert(string strTag, DB_TIME stTime, int& iVal);
	void Insert(string strTag, DB_TIME stTime, long long iVal);
	void Insert(string strTag, bool bVal, DB_TIME* stTime=nullptr);
	void Insert(string strTag, double dbVal, DB_TIME* stTime = nullptr);
	void Insert(string strTag, int iVal, DB_TIME* stTime = nullptr);
	void Insert(string strTag, long long iVal, DB_TIME* stTime = nullptr);

	// insert complex data type
	// custom data element in json format
	void Insert(string strTag, string& sDe,DB_TIME* stTime = nullptr ); 
	// curve type internal data type of tds, save to file  123000.curve.json in the same path with db.json(datalist file)
	void Insert(string strTag, string& sDeIdx,string& sDeCurve, DB_TIME* stTime = nullptr);


	//db.update functions
	int Update(string tag, DB_TIME stTime, yyjson_val* yyVal, yyjson_val* updateFile);

	//db.merge functions
	int Merge(string tag, const DB_TIME& stTime, const DB_TIME &stTimeRange1, const DB_TIME& stTimeRange2, const std::multimap<string, yyjson_val*>& mMergeParams);


	//save main image data such as 123000.image.jpg,data list will not be modified 
	bool saveImage(string tag, DB_TIME stTime, char* pData, size_t len, string& imgInfo, string sDeIdx = "");

	bool Delete(string tag, DB_TIME stTime);

	TDB* getChildDB(string dbName);
	map<string, TDB*> m_childDB;
//private func
public:
	// convert old datalist file to new format
	yyjson_mut_doc* convertJsonFormat(yyjson_doc* original_doc);

	//param parse
	map<string, vector<string>> getAggrOpt(yyjson_val* jAggr);
	bool parseDESelector(yyjson_val* yyParams, DE_SELECTOR& deSelector, string& err);
	bool parseDESelector(const string& sParams, DE_SELECTOR& deSelector, string& err);
	int dhmsSpan2Seconds(string timeSpan);
	//insert
	void InsertValJsonStr(string strTag, DB_TIME stTime, string& sVal);
	//select
	bool Select_Step_selectTags(DE_SELECTOR& deSel, SELECT_RLT& rlt);
	bool Select_Step_loadFile(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, SELECT_RLT& result);
	bool Select_Step_loadDataElem(DE_SELECTOR& deSel, vector<TAG_FILE_SET*>& tagDBFileSet, vector<DATA_SET*>& outputDataSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_FilterByRelation(DE_SELECTOR& deSel, vector<DATA_SET*>& inputDataSet, vector<DATA_SET*>& outputDataSet);
	bool Select_Step_doAggregate(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet,yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool saveDeToDataListFile(string dataListPath, yyjson_mut_val* yymDe);
	bool Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, vector<DATA_SET*>& set_list, map<SORT_FLAG, yyjson_mut_val*>& mapRlt,SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<DATA_SET*>& set_list, map<SORT_FLAG, yyjson_mut_val*>& mapRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<DATA_SET*>& set_list, vector<yyjson_mut_val*>& vecRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
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
	string getPath_dbFile(string strTag, string time, string deType = "");
	string getPath_dbFile(string strTag, const DB_TIME& date, string deType = "") const;
	string changeCharForFileName(string s)  const;
	string getPath_dataFolder(string strTag, const DB_TIME& date, const string& deType = "") const;
	string getPath_dataFolder_NO_DB(string strTag, const DB_TIME& date) const;
	string getPath_deFile(string strTag, DB_TIME stTime);
	string getPath_dbRoot();
	string getName_deFile(string tag, DB_TIME time);

	string parseSuffix(string deFileUrl);
	static bool fileExist(string pszFileName);
	static bool folderExist(string pszFileName);
	string m_name; //database name, same as project name
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
	fp_getTagsByTagSelector m_getTagsByTagSelector;
	bool m_isGbk;
	string m_confPath;
	string m_currentPath;
};
unsigned int
tdb_base64_encode(const unsigned char* in, unsigned int inlen, char* out);

bool IsLeapYear(int wYear);
int DaysInAMonth(int wYear, int wMonth);

extern TDB db;
