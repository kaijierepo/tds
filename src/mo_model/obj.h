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
#include "tdscore.h"
using namespace std;
using json = nlohmann::json;

/* 位号相关的核心概念
| 位号名称                 | 父位号     | 例子                           | 类型     |
| ------------------------ | ---------- | ------------------------------ | -------- |
| 系统位号   sysTag        | -          | 杭州.科技大楼.一楼.开水间.温度 | 绝对位号 |
| 用户根位号   userRootTag | -          | 杭州.科技大楼                  | 绝对位号 |
| 用户位号 userTag         | 用户根位号 | 一楼.开水间.温度               | 相对位号 |
| 查询根位号 queryRootTag  | 用户根位号 | 一楼                           | 相对位号 |
| 查询位号 queryTag        | 查询根位号 | 开水间.温度                    | 相对位号 |

sysTag = userRootTag + queryRootTag + queryTag

杭州.科技大楼.一楼.开水间.温度 = 杭州.科技大楼 + 一楼 + 开水间.温度

应用场景：

例如张三是科技大楼的管理员，那么张三的 userRootTag = 杭州.科技大楼

对于他来说，一楼.开水间.温度 是张三的视角下查询该数据点的位号

某一次数据观察，张三希望统一观察一楼所有的数据点，因此，张三指定了queryRootTag = 一楼

因此，张三得到了 开水间.温度、开水间.湿度等 queryTag

*/

/* 对象数据类型族 
									obj 对象  
							_________|__________ 
						   mo 监控对象          org 组织结构                大类，用大类可以一起查询其子类             
					_______|______         _____|______________________
				   |       |      |       |             |              |
			   customMO    mo     mp     org         customOrg       project    type取值可以为该5种
*/

namespace TAG {
	string trimRoot(string& tag);
	string trimRoot(string& tag,string root);
	string userTag2sysTag(string userTag, string userOrg);
	string sysTag2userTag(string sysTag, string userOrg);
	string addRoot(string& tag);
	string addRoot(string tag, string root);

	bool hasTag(json& tree, string tag); //moTree中是否包含某个tag.该tag不包含前面树的根节点
	int getMoLevel(string tag); //根节点level 为0，依次增加
	json mapTree2List(json mapTree);
}

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

	OBJ_QUERIER() {
		 getConf = true;
		 getMp = false;
		 getStatus = false;
		 getChild = false;
		 getStatusDesc = false;
		 leafType = "mo";
		 getConfDetail = true; //配置文件中不保存。内部使用，不开放给接口api
	}
};


class amo;
class MP;
class database;
class OBJ
{
public:
	OBJ();
	virtual ~OBJ();

	virtual bool loadConf(json& conf);
	virtual bool toJson(json& conf, OBJ_QUERIER querier);
	virtual bool toJson(json& conf, json serializeOption);
	virtual bool loadStatus(OBJ* pMo,bool saveToDB = false);
	bool loadStatus(json& jMpList);

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
	string m_parentTag;

	//动态创建
	OBJ* createObjBranchByTag(string tag);

	//状态数据
	bool m_bOnline;
	SYSTEMTIME m_stDataLastUpdate;
	json m_longitudeDyn;
	json m_latitudeDyn;
	string m_status;
	json m_jAlarmStatus;
	string m_strIoAddrBind; //如果绑定了io地址，该mo是一台智能设备

	//查询接口
	virtual json getRT();
	OBJ* queryObj(string strTag);//在以自己为根节点的整颗书检索Tag,找到对应的CMO返回
	MP* GetMPByTag(string strTag);
	void queryObj(std::vector<OBJ*>* tagVec, string strTag, string type = "obj");
	void GetMPByTag(std::vector<MP*>* tagVec, string strTag);
	MP* GetMPByName(string strName);
	OBJ* GetMOByName(string strName);
	//确认自己是否被某类型选中
	bool isSelectedByType(string type);
	//指定叶子节点类型，将自己作为树枝节点进行判断，确定是否返回。只要在结构上可以包含叶子节点类型的枝干节点都将被返回
	bool isSelectedByLeafType(string leafType);

	//树管理
	std::vector<OBJ*> m_childMO;
	OBJ* m_pParentMO;
	OBJ* createChildMO(string subTag, string moType);
	void GetAllChildMO(std::vector<OBJ*>& aryMO, string type);
	map<string, json> m_childCustomMoTypeList;  //子mo中所有的自定义的moType类型
	map<string, json> getChildCustomMoTypeList();
	void statisChildCustomMoType(map<string, json>& list);
	void statisChildMo(json& jStatis);
	void removeMp(json& mo);
	void clearChildren();

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
	string TranslateRelateTag(string rtag);
	json m_mapConf;


	string GetStatusSummary();//获得当前状态概要，用于在拓扑图上显示
	void GetAllChildAlarmInfo(string& strSummary);
	vector<string> getTagPartials(string strTag);
	string getTypeLabel(string type);

	static string AppendTagRoot(string& str);
	static string ResolveTag(string strTagExp, string strTagThis); //strTagContext指位号表达式所在mp的父mo的位号
	static string trimProperty(string& strTagExp);
};

OBJ* createMO(string type);

