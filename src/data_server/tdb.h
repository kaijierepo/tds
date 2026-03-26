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
	DB_TIME(const std::string& sTime) {
		fromStr(sTime);
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
	std::string toStampHMS() const;
	std::string toStampFull() const;
	std::string toYMD() const;
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
	std::string toStr(bool enableMS = true) const;
	bool fromStr(std::string str);
	int getTimePassSecond();
	static std::string nowStr(bool enalbeMs = true);
	static std::string nowStrWithMilli();

	bool operator==(const DB_TIME& right) const {
		return 0 == memcmp(this, &right,sizeof(DB_TIME));
	}
	bool isLeapYear() const {
		return (wYear % 4 == 0 && wYear % 100 != 0) || (wYear % 400 == 0);
	}
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
		wMilliseconds += right.wMilliseconds;
		if (wMilliseconds >= 1000) {
			wMilliseconds -= 1000;
			wSecond += 1;
		}
		wSecond += right.wSecond;
		if (wSecond >= 60) {
			wSecond -= 60;
			wMinute += 1;
		}
		wMinute += right.wMinute;
		if (wMinute >= 60) {
			wMinute -= 60;
			wHour += 1;
		}
		wHour += right.wHour;
		if (wHour >= 24) {
			wHour -= 24;
			wDay += 1;
		}
		wDay += right.wDay;
		while (wDay > getMaxDayOfMonth()) {
			wDay -= getMaxDayOfMonth();
			wMonth += 1;
			if (wMonth > 12) {
				wMonth = 1;
				wYear += 1;
			}
		}
		wMonth += right.wMonth;
		if (wMonth > 12) {
			wMonth -= 12;
			wYear += 1;
		}
		wYear += right.wYear;
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

	DB_TIME& operator=(const std::string& sTime) {
		this->fromStr(sTime);
		return *this;
	}
};

struct DE_TIME {
	DB_TIME st;
	time_t tt;
	std::string strT;
};

struct DB_TIME_RANGE {
	DB_TIME start;
	DB_TIME end;
	void* p;
};

namespace DB_STR {
	wstring utf8_to_utf16(std::string instr);
	std::string utf16_to_utf8(wstring instr);
	std::string gb_to_utf8(std::string instr);
	std::string utf8_to_gb(std::string instr);
	wstring gb_to_utf16(std::string instr);
}

namespace DB_FS {
	bool readFile(std::string path, std::string& data);
	bool createFolderOfPath(std::string strFile);
	bool writeFile(std::string path, char* data, size_t len);
	bool writeFile(std::string path, unsigned char* data, size_t len);
	bool writeFile(std::string path, std::string& data);
	bool deleteFile(std::string path);
	void DeleteDirectoryContents(const std::string& dirPath);
	void deleteDirectory(std::string& dirPath);
	bool copyFile(const std::string& src, const std::string& dest);
	bool rename(const std::string& oldPath, const std::string& newPath);

	std::string normalizationPath(std::string& s);

	struct FILE_INFO {
		std::string modifyTime;
		std::string createTime;
		size_t len;
		std::string accessTime;
		std::string name;
		std::string path;
		std::string folderPath;
	};

	void getFolderList(std::vector<FILE_INFO>& list, std::string strFolder, bool recursive = false);
	void getFileList(std::vector<FILE_INFO>& list, std::string strFolder, bool recursive = false, std::string suffix = "*", std::vector<std::string>* exclude = nullptr);
	void getFileList(std::vector<std::string>& list, std::string strFolder, bool includeFolder = false, bool recursive = false);
}

class TAG_SELECTOR{
public:
	TAG_SELECTOR() {
		getTag = false;
	}

	//objType==* select all obj who's type is set.
	//objType=="" select all obj
	bool init(std::string tag, std::string rootTag="", std::string objtype="",std::string level="*");
	bool init(std::vector<std::string>& tag, std::string rootTag = "", std::string objtype = "", std::string level = "*");
	bool match(std::string tag);//use absolute tag 
	bool singleSelMode();

	std::string m_org;  
	std::string m_rootTag; 


	std::string tagSel;
	std::vector<std::string> fuzzyMatchExp;
	std::vector<std::string> fuzzyMatchRegExp;
	std::vector<std::string> exactMatchExp; 

	bool getTag; //whether contains key "tag" in de returned

	void setType(std::string objType);
	bool specifyType();
	std::string type; //object type
	std::string level;
	std::string error;
	std::string ioType;
	std::string selLanguage;
	std::string rltLanguage;
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
	bool Match(std::string& deTime);
	bool init(std::string time);
	std::string selector;
	Time_Atom_Sel_Type timeSetType;
	std::string strStart;
	std::string strEnd;
	DB_TIME stStart;
	DB_TIME stEnd;
	time_t startTime;
	time_t endTime;
	bool snapShot;
	std::string shortSel2StardardSel(std::string time);
	bool parseTimeRange(std::string time);//use standard time selector as 2020-02-01 00:00:00~2020-02-28 23:59:59
	std::string getParsedSelector();

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
	bool Match(std::string& deTime);
	bool AmountMatch(size_t amount);
	bool init(std::vector<std::string> time);
	bool init(std::string time);

	bool isRange();

	bool isVarTimePoint();

	std::vector<TIME_SELECTOR_ATOM> atomSelList;

	std::string timeFmt; //specified time format to return,such as YYYY-MM-DD hh:mm:ss
	int m_dataNum;//how many de to get
	std::string timeRangeByDataNum;
	std::string error;
	DE_TIME deTime;
	bool enable;
	bool snapShot;
};
DB_TIME_RANGE parseTimeRange(std::string timeExp);

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
	std::string data;
	std::string path;
	std::string ymd;
	DB_TIME time;
	std::string tag;
	yyjson_doc* doc;
	yyjson_val* root;
	std::string deType;
	TDB* pOwnerDB;

	bool loadFile();

	bool isDataList();  //datalist file, curve index file ,not curve file. only data list is buffered in tdb

	DB_FILE(DB_TIME t, std::string tag_, TDB* pOwner) {
		monthBoundaryFile = false;
		boundaryFile = false;
		time = t;
		tag = tag_;
		doc = nullptr;
		root = nullptr;
		pOwnerDB = pOwner;
	}

	DB_FILE(time_t tt,std::string tag_,TDB* pOwner) {
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
	std::string deTime; 
	yyjson_mut_val* val;
	std::string fmtTime; 
	
	yyjson_mut_val* de;   
	map<std::string, yyjson_mut_val*> items; //custom de,when val is not used; only one level json structrue is supported. key store json key,val stores val after aggregate

	DE_yyjson() {
		val = 0;
		de = 0;
	}
};

class TAG_FILE_SET {
public:
	std::string tag;
	std::string dbFileTag;
	std::vector<DB_FILE*> fileList; //sort by time asending

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
	std::string tag;  // system tag
	std::string relTag;  //rel tag to return in the query
	std::string mpName;  
	std::string colKey;


	map<std::string, std::vector<std::string>> aggregate; //key is the json key to aggregate，val is aggregate mode (max,min,diff ...)
	
	//grouped data before aggregate key is time stamp ,val is de std::vector
	map<std::string, std::vector<yyjson_val*>> m_origDeGrouped;
	//ungrouped data before aggregate or without aggr option or no de mutation
	std::vector<yyjson_val*> m_orgDe;

	//data after aggregate 
	std::vector<DE_yyjson*> m_afterAggr;

	//   saved custom groupby columns
	map<std::string, map<std::string, std::string>> customGroupValues;
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
	bool init(std::string filter);
	std::string filterExp;
	bool bEnable;

};


struct TIME_RELATION {
	std::string type;
	int offset;
	int count;
};

struct WHEN_SELECTOR {
	std::string tag;
	std::string match;
	bool whenStatus;  //false: when match   true: when status
	DB_VAL status;
	CONDITION_SELECTOR condition;
	std::vector<DB_TIME_SPAN> eventTimeSlot;
	std::vector<TIME_RELATION> relation;
};

struct DE_SELECTOR {
	TIME_SELECTOR timeSel;  
	TAG_SELECTOR tagSel;	
	CONDITION_SELECTOR condition;	
	DOWN_SAMPLING_SELECTOR downSamplingSel;		
	WHEN_SELECTOR whenSel;
	bool ascendingSort;
	std::string sortKey;
	bool timeFill;   //in a time section ,data is not exist in some tag. use value before this time section to fill in this time section
	std::string splitBy;    //split into multiple result data set

	std::string valType;  //transform de val to specified value
	std::string deType;   //get de by default;  curveIdx to get curveIdx in curveList file
	bool isValTypeNumber() {
		if (valType == "float" || valType == "number") {
			return true;
		}
		return false;
	}

	std::string tagLabel; //rename tag
	std::vector<std::string> vecTagLable; //muti rename

	std::string groupby;
	std::string timeGroupBy;
	bool groupByTime; //time in selected de is set a time group key such as "2023-09-01 11" when groupby "hour"
	bool groupByTag;  
	std::vector<std::string> customGroupBy;  // 自定义分组字段，如 ["acqType"]
	map<std::string, std::string> customGroupAlias; // 自定义分组字段别名

	bool bAggr; 
	map<std::string,std::vector<std::string>> aggregate; //global aggr option. key is the json key to aggr, val is aggr type
	std::vector<map<std::string, std::vector<std::string>>> vecAggregate; //specified each tag in its own aggregate type
	map<std::string, std::vector<DB_TIME_RANGE>> mapTimeSlots;  //named time slots,used in "increase" aggr mode
	bool tagAsColume; //return data set as a table.each tag as a columne

	//use function to calc the selected dataset
	std::string calc;
	std::string curvePtAggr;
	std::string baseCurve;
	std::string getSelectorDesc();

	int offset = 0;
	int limit = 0;
	int pageNo = 1;
	int pageSize = 0;

	//map<std::string, std::string> mapSelfParms;
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

	bool init(const std::string& params,std::string& err);
};

class db_exception : public std::exception {
public:
	const char* what() const noexcept /*noexcept*/ override { return m_error.c_str(); }
	std::string m_error;
};

struct SORT_FLAG {
	double dbFlag;
	std::string sFlag;

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
	std::string dataList;
	size_t rowCount;
	size_t deCount;
	size_t fileCount;
	map<SORT_FLAG, yyjson_mut_val*> rltDataSet; //single result data set
	map<std::string, map<SORT_FLAG, yyjson_mut_val*>> rltDataSetList; //db.select uses splitBy
	std::vector<yyjson_mut_val*> rltDataSetVec;  //single result data set ,do not need order
	map<std::string, std::vector<yyjson_mut_val*>> rltDataSetVecList; //db.select uses splitBy
	yyjson_mut_doc* rlt_mut_doc;
	std::string error;
	std::string info;
	std::string query;
	std::string calcResult; 

	std::vector<std::string> tagSet;  //in query language
	std::vector<std::string> dbFileTagSet;  //in disk storage language
	std::vector<std::vector<DATA_SET*>*>  dataSetBuff; 
	std::vector<TAG_FILE_SET*> tagFileSet;

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
			std::vector<DATA_SET*>* p = dataSetBuff[i];
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
	const std::string val = "val";
	const std::string time = "time";
	const std::string tag = "tag";
	const std::string url = "url";
};

struct  DB_FMT
{
	std::string deListName;
	std::string deListStatisticsName;
	std::string curveIdxListName;
	std::string curveDeNameSuffix;  //The suffix contains "."
	std::string deItemKey_value;
	std::string language;    // zh for chinese en for english
	std::string dbRootTag;   // root tag of tag in query. when multi db engines use one db folder

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
typedef void (*fp_dbLog)(std::string& str);

struct FILE_BUFF {
	DB_TIME lastActive;
	std::string data;
	FILE_BUFF() {

	}
	~FILE_BUFF() {
	}
};

class FS_BUFF {
public:
	std::mutex m_csFsb;
	std::map<std::string,FILE_BUFF*> m_mapFsBuff;

	bool readFile(std::string path, std::string& data);
	bool writeFile(std::string path, unsigned char* data, size_t len);
};

inline std::string JSON_STR_VAL(const std::string& s) {
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
	bool Open(std::string strDBUrl, fp_getTagsByTagSelector f = nullptr,std::string name="");
	bool Open_gbk(std::string strDBUrl, fp_getTagsByTagSelector f = nullptr, std::string name = "");
	bool setBufferTTL(std::string bufferTTL);
	bool m_enableDB = true;
	DB_FMT m_dbFmt;
	bool m_bEnableFsBuff;
	FS_BUFF m_FsBuff;
	int m_bufferTTL;
	DB_TIME_UNIT m_timeUnit;
	bool m_bAutoUpgrade;

	bool handleRpc(const std::string& method,yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);

	// time series db function
	void rpc_db_insert(std::string& sParams, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);
	void rpc_db_insert(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);

	void rpc_db_select(std::string& sParams, std::string& rlt, std::string& err, std::string& queryInfo, std::string org,std::string language);
	void rpc_db_select(yyjson_val* params, std::string& rlt,std::string& err,std::string& queryInfo, std::string org,std::string language);

	void rpc_db_update(std::string& sParams, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);
	void rpc_db_update(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);

	void rpc_db_merge(std::string& sParams, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);
	void rpc_db_merge(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);
	
	bool Update(std::string tag, DB_TIME stTime, yyjson_val& jData);

	void rpc_db_delete(std::string& sParams, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);
	void rpc_db_delete(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);

	void rpc_db_saveImage(std::string& sParams, std::string& rlt, std::string& err, std::string& queryInfo, std::string org, std::string language);
	void rpc_db_getBufferStatus(std::string& rlt, std::string& err);
	void rpc_db_setConf(std::string& sParams, std::string& rlt, std::string& err);

	//table db function
	void rpc_db_table_insert(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, const std::string& org, const std::string& language);
	void rpc_db_table_delete(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, const std::string& org, const std::string& language);
	void rpc_db_table_update(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, const std::string& org, const std::string& language);
	void rpc_db_table_select(yyjson_val* params, std::string& rlt, std::string& err, std::string& queryInfo, const std::string& org, const std::string& language);

	bool Select(DE_SELECTOR& deSel, SELECT_RLT& result);

	bool tableUpdate(std::string tableName, std::vector<std::string>& match, std::vector<yyjson_val*>& updateData, std::string& err);
	bool tableUpdate(std::string tableName, std::vector<std::string>& match, std::vector<std::string>& updateData,std::string& err);
	bool tableUpdate(std::string tableName, const std::string& match, const std::string& updateData,std::string& err);
	bool tableInsert(std::string tableName, const std::string& row, std::string& err);
	bool tableInsert(std::string tableName, yyjson_val* row, std::string& err);
	bool tableSelect(std::string tableName, std::vector<std::string>& match, std::string& rlt, std::string& err);

	// db.insert functions
	// insert basic val type,use reference ,avoid force conversion
	bool Insert(std::string strTag, DB_TIME stTime, double& dbVal);
	bool Insert(std::string strTag, DB_TIME stTime, int& iVal);
	bool Insert(std::string strTag, DB_TIME stTime, long long& iVal);
	bool Insert(std::string strTag, DB_TIME stTime, float& fVal);
	bool Insert(std::string strTag, bool bVal, DB_TIME* stTime=nullptr);
	bool Insert(std::string strTag, double dbVal, DB_TIME* stTime = nullptr);
	bool Insert(std::string strTag, int iVal, DB_TIME* stTime = nullptr);
	bool Insert(std::string strTag, long long iVal, DB_TIME* stTime = nullptr);

	// insert complex data type
	// custom data element in json format
	bool Insert(std::string strTag, std::string& sDe,DB_TIME* stTime = nullptr );
	// curve type internal data type of tds, save to file  123000.curve.json in the same path with db.json(datalist file)
	bool Insert(std::string strTag, std::string& sDeIdx,std::string& sDeCurve, DB_TIME* stTime = nullptr);


	//db.update functions
	int Update(std::string tag, DB_TIME stTime, yyjson_val* yyVal, yyjson_val* updateFile);

	//db.merge functions
	int Merge(std::string tag, const DB_TIME& stTime, const DB_TIME &stTimeRange1, const DB_TIME& stTimeRange2, const std::multimap<std::string, yyjson_val*>& mMergeParams);


	//save main image data such as 123000.image.jpg,data list will not be modified 
	bool saveImage(std::string tag, DB_TIME stTime, char* pData, size_t len, std::string& imgInfo, std::string sDeIdx = "");

	bool Delete(std::string tag, DB_TIME stTime);

	TDB* getChildDB(std::string dbName);
	map<std::string, TDB*> m_childDB;
//private func
public:
	// convert old datalist file to new format
	yyjson_mut_doc* convertJsonFormat(yyjson_doc* original_doc);

	//param parse
	map<std::string, std::vector<std::string>> getAggrOpt(yyjson_val* jAggr);
	bool parseDESelector(yyjson_val* yyParams, DE_SELECTOR& deSelector, std::string& err);
	bool parseDESelector(const std::string& sParams, DE_SELECTOR& deSelector, std::string& err);
	int dhmsSpan2Seconds(std::string timeSpan);
	//insert
	bool InsertValJsonStr(std::string strTag, DB_TIME stTime, std::string& sVal);
	//select
	bool Select_Step_selectTags(DE_SELECTOR& deSel, SELECT_RLT& rlt);
	bool Select_Step_loadFile(DE_SELECTOR& deSel, std::vector<TAG_FILE_SET*>& tagDBFileSet, SELECT_RLT& result);
	bool Select_Step_loadDataElem(DE_SELECTOR& deSel, std::vector<TAG_FILE_SET*>& tagDBFileSet, std::vector<DATA_SET*>& outputDataSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_FilterByRelation(DE_SELECTOR& deSel, std::vector<DATA_SET*>& inputDataSet, std::vector<DATA_SET*>& outputDataSet);
	bool Select_Step_doAggregate(DE_SELECTOR& deSel, std::vector<DATA_SET*>& tagDBFileSet,yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, std::vector<DATA_SET*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool saveDeToDataListFile(std::string dataListPath, yyjson_mut_val* yymDe);
	bool Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, std::vector<DATA_SET*>& set_list, map<SORT_FLAG, yyjson_mut_val*>& mapRlt,SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, std::vector<DATA_SET*>& set_list, map<SORT_FLAG, yyjson_mut_val*>& mapRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, std::vector<DATA_SET*>& set_list, std::vector<yyjson_mut_val*>& vecRlt, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	//bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, std::vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool doAggregateOneGroup(DE_SELECTOR& deSel, std::map<std::string, std::vector<std::string>> aggrKeyType, std::string groupKey,std::vector<yyjson_val*>& src, DE_yyjson& des, yyjson_mut_doc* mut_doc);
	//double doAggrOneGroup_increase(DE_SELECTOR& deSel, std::string& aggrKey, std::vector<yyjson_val*>& deGroup);
	map<std::string, double> doAggrOneGroup_increase_withTimeSlots(DE_SELECTOR& deSel, std::string& aggrKey, std::string groupKey, std::vector<yyjson_val*>& deGroup);
	double doAggrOneGroup_sum(DE_SELECTOR& deSel, std::string& aggrKey, std::vector<yyjson_val*>& deGroup);
	void doAggrOneGroup_duration(DE_SELECTOR& deSel, std::string& aggrKey, std::vector<yyjson_val*>& deGroup, yyjson_mut_val*& pAggrRlt, yyjson_mut_doc* yydoc);
	double doAggrOneGroup_diff(DE_SELECTOR& deSel, std::string& aggrKey, std::vector<yyjson_val*>& deGroup);
	double doAggrOneGroup_avg(DE_SELECTOR& deSel, std::string& aggrKey, std::vector<yyjson_val*>& deGroup);
	//update
	//bool Update(std::string tag, TIME stTime, std::string& sData);
	//bool Update(std::string tag, TIME stTime, json& jData);
	bool Count(std::string tag, TIME_SELECTOR& timeSelector, std::string filter, int& iCount);

	void getDeTime(yyjson_mut_val* yyTime, std::string& deTime);

	//file save operation
	//bool saveToDeListFile(const std::string& dataListPath, yyjson_mut_val* yymDe);
	//bool saveToDeListFile(const std::string& dataListPath, std::string sDe);
	std::string saveDEFile(yyjson_val* yyvFileInfo, std::string path,DB_TIME dbTime,std::string& type);

	//path management
	std::string getPath_dbFile(std::string strTag, std::string time, std::string deType = "");
	std::string getPath_dbFile(std::string strTag, const DB_TIME& date, std::string deType = "") const;
	std::string changeCharForFileName(std::string s)  const;
	std::string getPath_dataFolder(std::string strTag, const DB_TIME& date, const std::string& deType = "") const;
	std::string getPath_dataFolder_NO_DB(std::string strTag, const DB_TIME& date) const;
	std::string getPath_deFile(std::string strTag, DB_TIME stTime);
	std::string getPath_dbRoot();
	std::string getName_deFile(std::string tag, DB_TIME time);
	std::string getDeFilesFolder(std::string& deListFolder, DB_TIME& time);

	std::string parseSuffix(std::string deFileUrl);
	static bool fileExist(std::string pszFileName);
	static bool folderExist(std::string pszFileName);
	std::string m_name; //database name, same as project name
	std::string m_path; // without a slash in the end.  add a slash if you want to compose a path
	fp_getTagsByTagSelector m_getTagsByTagSelector;
	bool m_isGbk;
	std::string m_confPath;
	std::string m_currentPath;
	fp_dbLog m_fpDBLog;
};
unsigned int
tdb_base64_encode(const unsigned char* in, unsigned int inlen, char* out);

bool IsLeapYear(int wYear);
int DaysInAMonth(int wYear, int wMonth);

extern TDB db;
