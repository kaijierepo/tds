/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu 卢涛

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
#include "json.hpp"

#include "tdb.h"
#include "common.h"
using namespace std;
using json = nlohmann::json;
class OBJ;


/* 对象数据类型族 
						             obj 对象  
				________________________|_________________________
               |                                                  |
	      mo 监控对象（实质监控内容）                         org 组织结构（容器）                大类，用大类可以一起查询其子类             
		_______|______ _______                        ____________|_______________
	   |       |      |       |                      |            |               |
	customMo   mo    mp    mpgroup                  org       customOrg         project       type取值可以为该5种
*/



struct OBJ_QUERIER {
	//指定子对象的返回结构
	bool getChild;
	bool getMp;
	bool dataSaveMp;
	string rootTag;  //查询该根位号下的位号，并且返回的位号除去该根位号
	string leafType;
	string leafLevel;
	bool flatten;  //是否将多层级的树形子节点压缩为只有一个层级的列表。

	//指定对象中返回的数据
	bool getVal; 
	bool getValDesc;
	bool getStatus;  //status = val + alarm + 其他运行时数据
	bool getStatusDesc; //以文本可阅读的方式返回状态信息，方便UI显示或者可视化组态
	bool getStatusDetail;
	bool getConf;
	bool getConfDetail;
	bool getUnit; //值描述信息是否需要带单位
	bool getTag;

	string match; //对象信息的自定义条件匹配
	string language;

	OBJ* pRoot;

	OBJ_QUERIER() {
		 dataSaveMp = false;
		 getConf = true;
		 getMp = false;
		 getStatus = false;
		 getStatusDetail = false;
		 getVal = false;
		 getChild = false;
		 getStatusDesc = false;
		 getValDesc = false;
		 leafType = "";
		 getConfDetail = true; //配置文件中不保存。内部使用，不开放给接口api
		 getUnit = true;
		 flatten = false;
		 pRoot = nullptr;
		 getTag = false;
	}
};


struct SCHEDULE_TASK {
	string name;
	string type;   // runScript or output
	Date dateStart;
	Date dateEnd;
	string outputTag;
	string outputVal;
	HMS time;
	bool week[7];
	TIME lastExecuteTime;
	string script;
	string mode; // weeklyRepeat, dailyRepeat, cutsomTimeRepeat, onlyOnce

	//run time 
	bool lastHmsReach;

	SCHEDULE_TASK() {
		memset(week, 0, sizeof(week));
		lastExecuteTime.setNow();
		type = "runScript";
		mode = "weeklyRepeat";
		lastHmsReach = true;
	}

	string getTypeDesc() {
		if (type == "output") {
			return "控制输出,位号=" + outputTag + ",值=" + outputVal;
		}
		else {
			return "执行脚本," + script;
		}
	}

	string toDescStr() {
		string s = "";
		if (mode == "weeklyRepeat") {
			s = str::format("任务名称:%s,类型:%s,调度模式:%s,%d%d%d%d%d%d%d,%s",
				name.c_str(),
				getTypeDesc().c_str(),
				mode.c_str(),
				week[0], week[1], week[2], week[3], week[4], week[5], week[6],
				time.toStr().c_str()
		  );
		}
		else if (mode == "customTimeRepeat") {
		  s = str::format("%s,%s-%s,%d:%d:%d,%s,%s",
			name.c_str(),
			dateStart.toStr().c_str(),
			dateEnd.toStr().c_str(),
			time.wHour, time.wMinute, time.wSecond,
			script.c_str(),
			mode.c_str()
		  );
		}
		return s;
	};

	TIME getExeTime();

	void fromJson(json& j) {
		if (j["name"].is_string()) {
			name = j["name"];
		}
		if (j["dateStart"].is_string()) {
			string s = j["dateStart"];
			dateStart.fromStr(s); 
		}
		if (j["dateEnd"].is_string()) {
			string s = j["dateEnd"];
			dateEnd.fromStr(s);
		}
		if (j["type"].is_string()) {
			type = j["type"];
		}
		if (j["script"].is_string()) {
			script = j["script"];
		}
		if (j["outputTag"].is_string()) {
			outputTag = j["outputTag"].get<string>();
		}
		if (j["outputVal"]!= nullptr) {
			outputVal = j["outputVal"];
		}
		if (j["mode"].is_string()) {
			mode = j["mode"];
			if (mode == "weeklyRepeat") {
        if (j["time"].is_string()) {
          string s = j["time"];
          time.fromStr(s);
        }
        if (j["week"].is_array()) {
				json& jWeek = j["week"];
				  for (int i = 0; i < jWeek.size(); ++i) {
					week[i] = jWeek[i].get<bool>();
				  }
				}
			}
			else if (mode == "customTimeRepeat") {
				json& jRepeatInterval = j["repeatInterval"];
				time.wHour = jRepeatInterval["hour"].get<int>();
				time.wMinute = jRepeatInterval["minute"].get<int>();
				time.wSecond = jRepeatInterval["second"].get<int>();
			}
		}
	}

	void fromJson(yyjson_val* conf) {
		yyjson_val* v = yyjson_obj_get(conf, "name");
		if (v) {
			name = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "dateStart");
		if (v) {
			dateStart.fromStr(yyjson_get_str(v));
		}

		v = yyjson_obj_get(conf, "dateEnd");
		if (v) {
			dateEnd.fromStr(yyjson_get_str(v));
		}

		v = yyjson_obj_get(conf, "type");
		if (v) {
			type = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "script");
		if (v) {
			script = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "outputTag");
		if (v) {
			outputTag = yyjson_get_str(v);
		}

		v = yyjson_obj_get(conf, "outputVal");
		if ( v) {
			if(yyjson_is_str(v))
				outputVal = yyjson_get_str(v);
			else if (yyjson_is_num(v)) {
				outputVal = to_string(yyjson_get_num(v));
			}
		}

		v = yyjson_obj_get(conf, "mode");
		if (v) {
			mode = yyjson_get_str(v);
			if (mode == "weeklyRepeat") {
				v = yyjson_obj_get(conf, "time");
				if (v) {
					string s = yyjson_get_str(v);
					time.fromStr(s);
				}
				yyjson_val* jWeek = yyjson_obj_get(conf, "week");
				if (jWeek) {
					for (int i = 0; i < yyjson_get_len(jWeek); ++i) {
						week[i] = yyjson_get_bool(yyjson_arr_get(jWeek, i));
					}
				}
			}
			else if (mode == "customTimeRepeat") {
				yyjson_val* jRepeatInterval = yyjson_obj_get(conf, "repeatInterval");
				time.wHour = yyjson_get_int(yyjson_obj_get(jRepeatInterval, "hour"));
				time.wMinute = yyjson_get_int(yyjson_obj_get(jRepeatInterval, "minute"));
				time.wSecond = yyjson_get_int(yyjson_obj_get(jRepeatInterval, "second"));
			}
		}
	}

	void toJson(json& j) {
		j["name"] = name;
		j["dateStart"] = dateStart.toStr();
		j["dateEnd"] = dateEnd.toStr();
		j["script"] = script;
		j["type"] = type;
		j["outputTag"] = outputTag;
		j["outputVal"] = outputVal;
		j["mode"] = mode;
		if (mode == "weeklyRepeat") {
			j["time"] = time.toStr();
			json jWeek = json::array();
			for (int i = 0; i < 7; ++i) {
			jWeek.push_back(week[i]);
			}
			j["week"] = jWeek;
		}
		else if (mode == "customTimeRepeat") {
		  j["repeatInterval"] = {
			{"hour", time.wHour},
			{"minute", time.wMinute},
			{"second", time.wSecond}
		  };
		}
	}

	void toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc) {
		yyjson_mut_obj_add_strcpy(doc, conf, "name", name.c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "dateStart", dateStart.toStr().c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "dateEnd", dateEnd.toStr().c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "script", script.c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "type", type.c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "outputTag", outputTag.c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "outputVal", outputVal.c_str());
		yyjson_mut_obj_add_strcpy(doc, conf, "mode", mode.c_str());
		if (mode == "weeklyRepeat") {
			yyjson_mut_obj_add_strcpy(doc, conf, "time", time.toStr().c_str());
			yyjson_mut_val* jWeek = yyjson_mut_arr(doc);
			for (int i = 0; i < 7; ++i) {
				yyjson_mut_arr_add_bool(doc, jWeek, week[i]);
			}
			yyjson_mut_obj_add_val(doc, conf, "week", jWeek);
		}
		else if (mode == "customTimeRepeat") {
			yyjson_mut_val* jRepeatInterval = yyjson_mut_obj(doc);

			yyjson_mut_obj_add_int(doc, jRepeatInterval, "hour", time.wHour);
			yyjson_mut_obj_add_int(doc, jRepeatInterval, "minute", time.wMinute);
			yyjson_mut_obj_add_int(doc, jRepeatInterval, "second", time.wSecond);
			yyjson_mut_obj_add_val(doc, conf, "repeatInterval", jRepeatInterval);
		}
	}

	bool isInterval();

	void updateExecuteTime();
};

struct OBJ_STATIS {
	string customType;
	int count;
	int online;
	int offline;
	int alarm;
	int fault;
	int normal;

	OBJ_STATIS() {
		count = 0;
		online = 0;
		offline = 0;
		alarm = 0;
		fault = 0;
		normal = 0;
	}
};

struct IO_TYPE_STATIS {
	int I;
	int O;
	int IO;
	int C;
	int V;
	int Unknown;

	IO_TYPE_STATIS() {
		memset(this, 0, sizeof(IO_TYPE_STATIS));
	}
};

struct VAL_TYPE_STATIS {
	int Float;
	int Bool;
	int String;
	int Int;
	int Video;
	int Unknown;
	VAL_TYPE_STATIS() {
		memset(this, 0, sizeof(VAL_TYPE_STATIS));
	}
};

struct MAP_CONF {
	bool enable;
	double center[2];
	double pitch;
	double rotation;
	string viewMode; //3D 2D
	double zoom;

	MAP_CONF() {
		enable = false;
	}
};

#define INVALID_COORD 1000

struct YY_OBJ_VAL {
	yyjson_mut_doc* doc;
	yyjson_mut_val* val;
	char* s;

	char* dump() {
		if (s)
			free(s);
		s = yyjson_mut_val_write(val, 0, nullptr);
		return s;
	}

	YY_OBJ_VAL() {
		doc = yyjson_mut_doc_new(nullptr);
		val = yyjson_mut_obj(doc);
		s = nullptr;
	}

	~YY_OBJ_VAL() {
		if (s) {
			free(s);
		}
		yyjson_mut_doc_free(doc);
	}
};

class MP;
class TDB;
class OBJ
{
public:
	OBJ();
	virtual ~OBJ();

	//static
	static bool m_bDefaultOnline;
	static void treeStatus2ListStatus(json& tree, json& list, json& onlineStatus, string tag);

	//function
	void loadTask(json& jTask);
	void loadTask(yyjson_val* conf);
	virtual bool loadConf(json& conf, bool bCreate = true);
	virtual bool loadConf(yyjson_val* conf, bool bCreate = true);
	virtual bool loadStatus(yyjson_val* status);
	virtual bool saveStatus(json& statusNode);
	virtual bool saveStatus(yyjson_mut_val* statusNode, yyjson_mut_doc* doc);
	virtual bool toJson(YY_OBJ_VAL& yyObj, OBJ_QUERIER querier, bool* isSelectedByLeafType = nullptr, const string& user = "admin");
	virtual bool toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, OBJ_QUERIER querier, bool* isSelectedByLeafType = nullptr, const string& user = "admin");
	//virtual bool toJson(json& conf, json serializeOption);
	//从srcTree找到与自身对应的对象，并拷贝以该对象为根节点的子树的状态
	virtual bool loadTreeStatus(OBJ* pSrcTree);
	//从srcObj拷贝对象状态，不含子对象的状态
	virtual bool loadObjStatus(OBJ* pSrcObj);
	void toAttrInfo(nlohmann::ordered_json& attrInfo);
	void getVal(yyjson_mut_val*& val, yyjson_mut_doc* doc);
	//配置数据
	bool isCustomMo();
	bool isCustomMp();
	bool isCustomOrg();
	bool isChildObjOfIntelliDev();
	OBJ* createObjBranchByTag(string tag);
	//查询接口
	virtual json getRT();
	void getMpList(map<string, MP*>& MPlist);
	OBJ* queryObj(string strTag, string language = "");//在以自己为根节点的整颗书检索Tag,找到对应的CMO返回
	MP* GetMPByTag(string strTag, string language);
	MP* GetMPByTagPinyin(string strTag);
	void queryObj(std::vector<OBJ*>* tagVec, string strTag, string language, string type = "", string level = "*");
	void GetMPByTag(std::vector<MP*>* tagVec, string strTag, string language);
	void getMpList(vector<MP*>& MPlist);
	OBJ* GetChildObjByName(string strName);
	MP* GetDescendantMPByName(string strName);
	OBJ* GetDescendantObjByName(string strName);
	bool isSelectedByIOType(const string& ioType);
	//确认自己是否被某类型选中
	bool isSelectedByLevel(const string& level);
	bool isSelectedByType(const string& type);
	//指定叶子节点类型，将自己作为树枝节点进行判断，确定是否返回。只要在结构上可以包含叶子节点类型的枝干节点都将被返回
	bool isSelectedByLeafType(string leafType);
	bool isSelectedByLeafLevel(string leafType);
	//修改接口
	void recursiveSetOffline();
	void setChildMpOnline();
	void setChildMpOffline();
	//树管理
	OBJ* createChildMO(string subTag, string moType);
	void GetAllChildObj(std::vector<OBJ*>& aryMO, string type);
	void GetAllChildMp(std::vector<MP*>& aryMP);
	void GetAttriMp(std::vector<MP*>& aryMP);
	map<string, json> m_childCustomMoTypeList;  //子mo中所有的自定义的moType类型
	map<string, json> getChildCustomTypeList(string level = "*");
	void statisChildCustomMoType(map<string, json>& list);
	string getChildObjStatis();
	void statisChildObj(map<string, OBJ_STATIS>& rlt);
	void statisChildMo(json& jStatis);
	void removeMp(json& mo);
	void clearChildren();
	OBJ* getOwnerChildTds();
	static OBJ_QUERIER parseQuerier(json& opt);
	OBJ* GetRootMO();
	OBJ* GetFatherMO(string type);//获得指定类型的父节点，或者是自身
	OBJ* GetChildMO(string type);
	OBJ* CopyMO();//复制一份与该mo相同的配置
	virtual OBJ& operator=(OBJ& right);
	OBJ* GetProjectMO();
	bool hasOnlineStatus();
	string& getName(const string& language);
	string getTag();
	virtual string getTag(string root, string language); //返回不包含根节点的位号 如果指定了root，返回以root为根节点的位号
	json getTypeTag();
	vector<string> GetAlias();
	vector<string> GetAllTagNamePlus();
	virtual string getTagWithRoot();
	string GetStatusSummary();//获得当前状态概要，用于在拓扑图上显示
	void GetAllChildAlarmInfo(string& strSummary);
	bool getTagsByTagSelector(TAG_SELECTOR& tagSelector,SELECT_RLT& rlt);
	void getObjByTagSelector(vector<OBJ*>& objList, TAG_SELECTOR& tagSelector);
	OBJ* getObjByID(string id);
	void getMpByTagSelector(vector<MP*>& mpList, TAG_SELECTOR& tagSelector);
	vector<string> getTagPartials(string strTag);
	string getTypeLabel(string type);
	string getUpdateTimeDesc();

	//conf
	string m_level;
	string m_type;  
	map<string, string> m_mapTypeTranslate;
	string m_groupName; //设备编组。1个自定义的字符串
	string m_name;
	string m_namePinyin;
	map<string,string> m_mapNameTranslate;
	string m_alias;
	string m_objID;  //name可能在系统中有重名，ID不会
	bool m_bShow;
	double m_longitude;
	double m_latitude;
	bool m_bDynLocation; //动态定位模式，从监控点获取
	bool m_bLocationCalib;
	double m_dbLongitudeCalib;
	double m_dbLatitudeCalib;
	bool m_bChildTds; //是否是下级服务
	string m_streamAccess;
	string m_strLastModify;  //上一次配置修改时间
	string m_comment;
	bool m_bEnableIO;
	bool m_bEnableTask;
	vector<SCHEDULE_TASK> m_scheduleTasks;
	string m_strIoAddrBind; //如果绑定了io地址，该mo是一台智能设备
	json m_customConf;
	std::vector<OBJ*> m_childObj;
	OBJ* m_pParentMO;
	MAP_CONF m_mapConf;
	string m_rootTag;

	//status
	bool m_bOnline;
	TIME m_stDataLastUpdate;
	double m_longitudeDyn;
	double m_latitudeDyn;
	string m_status;
	mutex m_mxAlarmStatus;
	json m_jAlarmStatus;
};

OBJ* createMO(string type);

