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
	string toStr(bool enableMS);
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
	bool match(string tag);//使用不带根的绝对位号
	bool singleSelMode();

	string m_org;  //组织结构
	string m_rootTag; //查询根

	//模糊匹配表达式
	vector<string> fuzzyMatchExp;
	//模糊匹配正则表达式
	vector<string> fuzzyMatchRegExp;
	//精确匹配表达式
	vector<string> exactMatchExp; 

	//返回的数据元中是否需要包含tag字段
	bool getTag;

	void setType(string objType);
	bool specifyType();
	string type; //object type
	string error;

	//选出的位号列表
	vector<string> tagSet;

};


//时间集合类型
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


//完整的时间选择器格式[Time_Set_Type]@[period type]@[time range]
//head@day@8d  选择8天当中每天的第一个数据
// 
// 
//时间选择器先确定需要加载的数据库原始文件的时间范围
//加载原始文件后，对边界上的两个文件的数据进行 时间范围的进一步选择
//时间点 理解成 开始时间和结束时间相同的一个timeRange
// 
//简写模式 - 解析规则
//2020-02-02  -> 2020-02-02 00:00:00~2020-02-02 23:59:59
//2020-02  -> 2020-02-01 00:00:00~2020-02-28 23:59:59
//
//取头尾模式
//head@TIME_SEL   tail@TIME_SEL

class TIME_SELECTOR
{
public:
	TIME_SELECTOR();
	bool Match(string& deTime);
	bool AmountMatch(size_t amount);
	bool init(string time);
	string shortSel2StardardSel(string time);
	bool parseTimeRange(string time);//标准格式时间范围2020-02-01 00:00:00~2020-02-28 23:59:59
	string getParsedSelector();

	//选择器字符串
	string selector;
	Time_Set_Type timeSetType;
	string timeFmt; //指定返回的时间格式,采用如下语法 YYYY-MM-DD hh:mm:ss


	//时间范围
	string strStart;
	string strEnd;
	DB_TIME stStart;
	DB_TIME stEnd;
	time_t startTime;
	time_t endTime;

	//是否进行周期性选择
	Period_Type periodType;
	int startHMS;
	int endHMS;

	int m_dataNum;//存储传入参数的，ne,n代表获取几个数据。
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
	int dsi;   //降采样元素个数间隔
	int dsti;  //降采样的时间间隔 单位秒

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

//作为聚合后数据时，只有 time 和 items是有值的
//聚合前数据items是空的
struct DE_yyjson {
	yyjson_mut_val* time;  //暂时只只是按时间聚合，聚合后该字段一定存在。
	yyjson_mut_val* val;
	string fmtTime; //根据请求格式格式化后的时间
	string deTime;

	yyjson_mut_val* de;    //为了提升性能，非聚合模式下，输出前输出存在这儿
	map<string, yyjson_mut_val*> items; //非val格式下的通用de   只支持1级json结构。key存储字段名称，val存储聚合后的值

	DE_yyjson() {
		time = 0;
		val = 0;
		de = 0;
	}
};

class TAG_FILE_SET {
public:
	string tag;
	vector<DB_FILE*> fileList; //按照时间顺序从前往后排序

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

//使用位号和时间标注的一个数据集
//位号可以是单个位号，或者是模糊匹配位号，表示多个位号
class DATA_SET {
public:
	string tag;  //系统位号
	string relTag;  //本次查询需要返回的相对位号
	string mpName;  //监控点名称
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
//路径管理
public:
	//获得数据库文件db.json的路径
	bool getDBFile(DB_TIME t, string tag, string fileName);
	string getPath_dbFile(string strTag, DB_TIME date, string deType = "");
	bool writeFile(string path, unsigned char* data, size_t len);
	bool writeFile(string path, char* data, size_t len);
	string changeCharForFileName(string s);
	//获得数据元文件或者数据库文件的存储文件夹目录
	string getPath_dataFolder(string strTag, DB_TIME date);
	//获得数据元文件或者数据元文件夹的路径
	string getPath_deFile(string strTag, DB_TIME stTime);
	string getPath_dbRoot();
	string getName_deFile(string tag, DB_TIME time);

	string parseSuffix(string deFileUrl);
	bool fileExist(string pszFileName);
	void createFolderOfPath(string strFile);
	string m_name; //database name, same as project name
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
};

extern TDB db;
