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

namespace TAG {
	string trimRoot(string& tag);
	string trimRoot(string& tag,string root);
	string addRoot(string& tag);
	string addRoot(string tag, string root);

	bool hasTag(json& tree, string tag); //moTree中是否包含某个tag.该tag不包含前面树的根节点
	int getMoLevel(string tag); //根节点level 为0，依次增加
	json mapTree2List(json mapTree);
}


class amo;
class MP;
class database;
class MO
{
public:
	MO();
	virtual ~MO();

	virtual bool loadConf(json& conf);
	virtual bool toJson(json& conf, json serializeOption);

	void removeMp(json& mo);
	void clearChildren();

	string m_moType;
	string m_moCustomType;
	string m_strName;
	string m_alias;
	bool m_bShow;

	json m_longitude;
	json m_latitude;

	json m_jAlarmStatus;

	MO* createChildMO(string subTag, string moType);
	void DeleteChildAMO(string& strName);
	void GetAllChildMO(std::vector<MO*>& aryMO, string type);
	void getAllSmartDev(std::vector<MO*>& aryMO); //与ioDev进行位号绑定的mo对象称为智能设备
	map<string,string> m_childCustomMoTypeList;  //子mo中所有的自定义的moType类型
	map<string,string> getChildCustomMoTypeList();
	void statisChildCustomMoType(map<string,string>& list);

	MO* GetRootMO();
	MO* GetFatherMO(string type);//获得指定类型的父节点，或者是自身
	MO* GetChildMO(string type);
	MO* CopyMO();//复制一份与该mo相同的配置
	virtual MO& operator=(MO& right);

	std::vector<MO*> m_childMO;

	MO* m_pParentMO;
	MO* GetProjectMO();//获得当前设备所属的Project节点，MO树根节点

	virtual json getRT();

	virtual string getTag(string root = ""); //返回不包含根节点的位号 如果指定了root，返回以root为根节点的位号
	vector<string> GetAlias();
	vector<string> GetAllTagNamePlus();
	virtual string getTagWithRoot();

	MO* GetMOByTag(string strTag);//在以自己为根节点的整颗书检索Tag,找到对应的CMO返回
	MP* GetMPByTag(string strTag);
	void GetMOByTag(std::vector<MO*>* tagVec, string strTag);
	void GetMPByTag(std::vector<MP*>* tagVec, string strTag);
	MP* GetMPByName(string strName);
	MO* GetMOByName(string strName);
	vector<string> getAllCustomMoType();

	string TranslateRelateTag(string rtag);
	string m_status;

	//topo management
	vector<void*> m_vecIODev;//挂接的采集设备.此处暂时用void，防止依赖ioDev.h文件，导致不容易多工程复用。需再考虑更好的办法
	void updateDataLink();
	string m_strIoAddrBind; //如果绑定了io地址，该mo是一台智能设备
	database* GetDB();
	string GetStatusSummary();//获得当前状态概要，用于在拓扑图上显示
	void GetAllChildAlarmInfo(string& strSummary);
	vector<string> getTagPartials(string strTag);
	string getTypeLabel(string type);

	static string AppendTagRoot(string& str);
	static string ResolveTag(string strTagExp, string strTagThis); //strTagContext指位号表达式所在mp的父mo的位号
	static string trimProperty(string& strTagExp);
};

MO* createMO(string type);

