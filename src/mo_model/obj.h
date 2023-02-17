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

#include "db.h"
using namespace std;
using json = nlohmann::json;



/* 对象数据类型族 
									obj 对象  
							_________|__________ 
						   mo 监控对象          org 组织结构                大类，用大类可以一起查询其子类             
					_______|______         _____|______________________
				   |       |      |       |             |              |
			   customMO    mo     mp     org         customOrg       project    type取值可以为该5种
*/



struct OBJ_QUERIER {
	//指定子对象的返回结构
	bool getChild;
	bool getMp;
	string rootTag;  //查询该根位号下的位号，并且返回的位号除去该根位号
	string leafType;

	//指定对象中返回的数据
	bool getStatus;
	bool getConf;
	bool getConfDetail;
	bool getStatusDesc; //以文本可阅读的方式返回状态信息，方便UI显示或者可视化组态
	bool getUnit; //值描述信息是否需要带单位

	OBJ_QUERIER() {
		 getConf = true;
		 getMp = false;
		 getStatus = false;
		 getChild = false;
		 getStatusDesc = false;
		 leafType = "mo";
		 getConfDetail = true; //配置文件中不保存。内部使用，不开放给接口api
		 getUnit = true;
	}
};


class MP;
class database;
class OBJ
{
public:
	OBJ();
	virtual ~OBJ();

	virtual bool loadConf(json& conf);
	virtual bool loadStatus(json& status);
	virtual bool toJson(json& conf, OBJ_QUERIER querier);
	virtual bool toJson(json& conf, json serializeOption);
	virtual bool loadStatus(OBJ* pMo,TIME* dataTime = nullptr,bool saveToDB = false);

	//配置数据
	string m_type;
	string m_customType;  //如果用中文命名，此处转为中文首字母
	string m_customTypeLabel;
	string m_groupName; //设备编组。1个自定义的字符串
	string m_name;
	string m_alias;
	bool m_bShow;
	json m_longitude;
	json m_latitude;
	bool m_bDynLocation; //动态定位模式，从监控点获取
	bool m_bLocationCalib;
	double m_dbLongitudeCalib;
	double m_dbLatitudeCalib;
	bool m_bChildTds; //是否是下级服务
	string m_strLastModify;  //上一次配置修改时间

	//动态创建
	OBJ* createObjBranchByTag(string tag);

	//状态数据
	bool m_bOnline;
	TIME m_stDataLastUpdate;
	json m_longitudeDyn;
	json m_latitudeDyn;
	string m_status;
	json m_jAlarmStatus;
	string m_strIoAddrBind; //如果绑定了io地址，该mo是一台智能设备

	//查询接口
	virtual json getRT();
	void getMpList(map<string, MP*>& MPlist);
	OBJ* queryObj(string strTag,bool usePinyin = false);//在以自己为根节点的整颗书检索Tag,找到对应的CMO返回
	MP* GetMPByTag(string strTag,bool usePinyin = false);
	MP* GetMPByTagPinyin(string strTag);
	void queryObj(std::vector<OBJ*>* tagVec, string strTag,bool usePinyin = false, string type = "obj");
	void GetMPByTag(std::vector<MP*>* tagVec, string strTag);
	void getMpList(vector<MP*>& MPlist);
	OBJ* GetChildObjByName(string strName);
	MP* GetDescendantMPByName(string strName);
	OBJ* GetDescendantObjByName(string strName);
	//确认自己是否被某类型选中
	bool isSelectedByType(string type);
	//指定叶子节点类型，将自己作为树枝节点进行判断，确定是否返回。只要在结构上可以包含叶子节点类型的枝干节点都将被返回
	bool isSelectedByLeafType(string leafType);

	//树管理
	std::vector<OBJ*> m_childObj;
	OBJ* m_pParentMO;
	OBJ* createChildMO(string subTag, string moType);
	void GetAllChildObj(std::vector<OBJ*>& aryMO, string type);
	void GetAllChildMp(std::vector<MP*>& aryMP);
	void GetAttriMp(std::vector<MP*>& aryMP);
	map<string, json> m_childCustomMoTypeList;  //子mo中所有的自定义的moType类型
	map<string, json> getChildCustomMoTypeList();
	void statisChildCustomMoType(map<string, json>& list);
	void statisChildMo(json& jStatis);
	void removeMp(json& mo);
	void clearChildren();
	//OBJ* getOwnerChildTds();

	OBJ_QUERIER parseQuerier(json& opt);
	OBJ* GetRootMO();
	OBJ* GetFatherMO(string type);//获得指定类型的父节点，或者是自身
	OBJ* GetChildMO(string type);
	OBJ* CopyMO();//复制一份与该mo相同的配置
	virtual OBJ& operator=(OBJ& right);
	OBJ* GetProjectMO();//获得当前设备所属的Project节点，MO树根节点
	string m_rootTag; //仅当当前对象为根节点时有效，将影响getTag的返回，getTag前面都会加上rootTag

	virtual string getTag(string root = ""); //返回不包含根节点的位号 如果指定了root，返回以root为根节点的位号
	vector<string> GetAlias();
	vector<string> GetAllTagNamePlus();
	virtual string getTagWithRoot();
	json m_mapConf;


	string GetStatusSummary();//获得当前状态概要，用于在拓扑图上显示
	void GetAllChildAlarmInfo(string& strSummary);
	bool getTagsByTagSelector(vector<string>& tags, TAG_SELECTOR& tagSelector);
	void getObjByTagSelector(vector<OBJ*>& objList, TAG_SELECTOR& tagSelector);
	void getMpByTagSelector(vector<MP*>& mpList, TAG_SELECTOR& tagSelector);
	vector<string> getTagPartials(string strTag);
	string getTypeLabel(string type);
	string getUpdateTimeDesc();
};

OBJ* createMO(string type);

