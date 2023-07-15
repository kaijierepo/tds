#pragma once
#include "common.h"
#include <map>
#include "json.hpp"
#include "tds.h"
#ifdef ENABLE_JERRY_SCRIPT
#include "jerryscript.h"
#endif
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
class RPC_SESSION;

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
	TIME st;
	time_t tt;
	string strT;
};


class TAG_SELECTOR{
public:
	TAG_SELECTOR() {
		getTag = false;
	}
	bool init(string tag,string rootTag = "");
	bool init(json tag, string rootTag = "");
	bool match(string tag);//使用不带根的绝对位号
	bool singleSelMode();

	string m_rootTag; //查询根

	//模糊匹配表达式
	vector<string> fuzzyMatchExp;
	//模糊匹配正则表达式
	vector<string> fuzzyMatchRegExp;
	//精确匹配表达式
	vector<string> exactMatchExp; 

	//返回的数据元中是否需要包含tag字段
	bool getTag;

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

	//选择器字符串
	string selector;
	Time_Set_Type timeSetType;
	string timeFmt; //指定返回的时间格式,采用如下语法 YYYY-MM-DD hh:mm:ss


	//时间范围
	string strStart;
	string strEnd;
	TIME stStart;
	TIME stEnd;
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
	string data;
	string path;
	string ymd;
	TIME time;
	time_t ttTime;
	string tag;
	yyjson_doc* doc;
	yyjson_val* root;
	string deType;

	bool loadFile();

	DB_FILE(time_t tt,string tag_) {
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
	map<string, yyjson_mut_val*> items; //非val格式下的通用de

	DE_yyjson() {
		time = 0;
		val = 0;
		de = 0;
	}
};

//单个位号的数据和查询参数
class TAG_DB_DATA {
public:
	string tag;  //系统位号
	string relTag;  //本次查询需要返回的相对位号
	string mpName;  //监控点名称
	string colKey;


	map<string, string> aggregate; //聚合操作，key是需要聚合的字段，val是聚合方式
	bool bAggr;

	vector<DB_FILE*> fileList; //按照时间顺序从前往后排序

	//分组聚合前数据.key是时间戳，数组是de的数组
	map<string, vector<yyjson_val*>> m_groupedBeforeAggr;
	//不分组聚合前数据
	vector<yyjson_val*> m_beforeAggr;

	//执行聚合函数后数据.或者无需聚合直接放入以下结构
	vector<DE_yyjson*> m_afterAggr;

	TAG_DB_DATA() {
		bAggr = false;
	}
	~TAG_DB_DATA() {
		if (fileList.size() > 0)
		{
			for (int i = 0; i < fileList.size(); i++)
			{
				delete fileList[i];
			}
		}

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
	bool setScriptEngineObj(yyjson_val* jObj, jerry_value_t engineObj);
	bool setScriptEngineObj(yyjson_mut_val* jObj, jerry_value_t engineObj);
	jerry_value_t global_object;
#endif
	bool match(string& de); //检查一个de是否满足条件
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

	//位号重命名
	string tagLabel; //重命名为 tag还是name
	vector<string> vecTagLable; //指定别名

	string groupby;
	bool groupByTime; //是否是按照时间进行分组，如果是按时间分组，查询结果的time字段将被改为时间的分组值
	bool grouped;

	//聚合运算
	bool bAggr; //是否进行数据聚合
	map<string,string> aggregate; //应用到所有聚合运算.key为需要聚合的key，val为需要聚合的方式
	vector<map<string, string>> vecAggregate;

	//多列模式
	bool tagAsColume; //将位号作为表的列返回.单列模式或多列模式

	//返回结果的运算
	string calc;

	DE_SELECTOR() {
		ascendingSort = true;
		tagAsColume = false;
		tagLabel = "tag";
		grouped = false;
		groupByTime = false;
		bAggr = false;
		timeFill = false;
	}
};

class db_exception : public std::exception {
public:
	const char* what() const noexcept /*noexcept*/ override { return m_error.c_str(); }
	string m_error;
};

struct SELECT_RLT {
	bool getDE;
	string dataList;
	size_t rowCount;
	size_t deCount;
	size_t fileCount;
	map<string, yyjson_mut_val*> mapRlt;

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



//路径中全部使用斜杠  "/" 不要使用反斜杠 "\\"
class database : public i_database{
public:
	database();
	bool create(string strDBUrl,string name);
	bool Open(string strDBUrl,string name="");
	void Close();

	map<string, string> getAggrOpt(json& jAggr);

	string parseDESelector(json params, DE_SELECTOR& deSelector);

//数据库存储规范
	DB_FMT m_dbFmt;


//rpc接口
public:
	void rpc_db_select(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_db_count(json params, RPC_RESP& resp, RPC_SESSION session);
	void rpc_db_getFile(json params, RPC_RESP& resp, RPC_SESSION session);

//接口部分
public:
	void Insert(string strTag, TIME stTime, json& jData,json dataFile = nullptr) ;

	bool Select_Step_outputRows_SingleCol_timeFill(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	
	//select
	bool Select_yyjson(DE_SELECTOR& deSel, SELECT_RLT& result);
	bool Select_yyjson_deFile(string& s);
	bool Select_Step_loadFile(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result);
	bool Select_Step_loadDataElem(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_doAggregate(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* rlt_mut_doc);
	bool Select_Step_outputRows_MultiCol(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool Select_Step_outputRows_SingleCol(DE_SELECTOR& deSel, vector<TAG_DB_DATA*>& tagDBFileSet, SELECT_RLT& result, yyjson_mut_doc* mut_doc);
	bool doAggregateSingleTag(DE_SELECTOR& deSel, std::map<string, string> aggrOpt, vector<yyjson_val*>& src, DE_yyjson& des, yyjson_mut_doc* mut_doc);

	//bool Select_simdjson(string tag, TIME_SELECTOR& timeSelector, string filter, DB_DATA_SET& result);
	bool Update(string tag, TIME stTime, string& sData);
	bool Update(string tag, TIME stTime, json& jData);
	bool Delete(string tag, TIME stTime);
	bool Count(string tag, TIME_SELECTOR& timeSelector, string filter, int& iCount);

	bool updateJsonObj(json& jOld, json& jNew);
	void saveDEFile(string strTag, TIME stTime, string deFileUrl) ;
	bool saveDEFile(string tag, TIME stTime, unsigned char* pData, int len,string suffix);
//路径管理
public:
	//获得数据库文件db.json的路径
	bool getDBFile(TIME t, string tag, string fileName);
	string getPath_dbFile(string strTag, TIME date, string deType = "");
	string changeCharForFileName(string s);
	//获得数据元文件或者数据库文件的存储文件夹目录
	string getPath_dataFolder(string strTag, TIME date);
	//获得数据元文件或者数据元文件夹的路径
	string getPath_deFile(string strTag, TIME stTime);
	string getPath_dbRoot();
	string getName_deFile(string tag, TIME time);

	string parseSuffix(string deFileUrl);
	string dataSet2String(DB_DATA_SET& dataSet);
	void GetFileTreeOfPath(FILE_ITEM* pfi, string strPath);
	string m_name; //database name, same as project name
	string m_path; // without a slash in the end.  add a slash if you want to compose a path
};

extern database db;
