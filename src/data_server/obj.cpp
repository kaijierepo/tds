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

#include "pch.h"
#include "obj.h"
#include "common.h"
#include "prj.h"
#include "mp.h"
#include "ioDev.h"
#include "tAlmSrv.h"
#include <algorithm>
#include "userMng.h"
#include "logger.h"

bool OBJ::m_bDefaultOnline = false;


OBJ* createMO(string type)
{
	OBJ* p = NULL;
	if (type == MO_TYPE::mo || type == MO_TYPE::customMo)
	{
		p = new OBJ();
	}
	else if (type == MO_TYPE::org)
	{
		p = new OBJ();
	}
	else if (type == MO_TYPE::customOrg)
	{
		p = new OBJ();
	}
	else if (type == MO_TYPE::mp)
	{
		p = new MP();
	}
	else if (type == MO_TYPE::mpgroup)
	{
		p = new OBJ();
	}
	else
	{
		p = new OBJ();
	}

	return p;
}

OBJ::OBJ()
{
	m_pParentMO = NULL;
	m_level = MO_TYPE::mo;
	m_bOnline = OBJ::m_bDefaultOnline;
	m_bShow = true;
	m_bDynLocation = false;
	m_dbLongitudeCalib = 0;
	m_dbLatitudeCalib = 0;
	m_bLocationCalib = false;
	m_bChildTds = false;
	m_bEnableIO = true;
	m_bEnableTask = true;
	m_longitude = INVALID_COORD;
	m_latitude = INVALID_COORD;
}

OBJ::~OBJ()
{
	for (auto* i : m_childObj) {
		delete i;
	}
	m_childObj.clear();
}

void OBJ::loadTask(json& jTask) {
	m_scheduleTasks.clear();
	for (auto& t : jTask) {
		SCHEDULE_TASK st;
		st.fromJson(t);
		m_scheduleTasks.push_back(st);
	}
}
void OBJ::loadTask(yyjson_val* conf) {
	m_scheduleTasks.clear();
	size_t indx = 0, max = 0;
	yyjson_val* taskVal;
	yyjson_arr_foreach(conf, indx, max, taskVal) {
		SCHEDULE_TASK st;
		st.fromJson(taskVal);
		m_scheduleTasks.push_back(st);
	}
}

bool OBJ::loadConf(json& conf, bool bCreate)
{
	//载入配置
	if (conf.contains("name")) {
		m_name = conf["name"];
		m_name = str::trim(m_name, " "); //界面在编辑时，非常容易不小心输入空格。并且不容易发现
	}

	if (conf.contains("nameTranslate")) {
		for (auto& i : conf["nameTranslate"].items()) {
			m_mapNameTranslate[i.key()] = i.value();
		}
		if (m_mapNameTranslate["zh"] == "") {
			m_mapNameTranslate["zh"] = m_name;
		}
	}

	if (conf.contains("level")) {
		m_level = conf["level"];
	}
	if (conf.contains("type")) {
		m_type = conf["type"];
	}
	
	if (conf.contains("childTds")) {
		m_bChildTds = conf["childTds"].get<bool>();
	}
	if (conf.contains("streamAccess")) {
		m_streamAccess = conf["streamAccess"].get<string>();
	}
	if (conf.contains("group")) {
		m_groupName = conf["group"].get<string>();
	}
	if (conf.contains("dynamicLocation"))
	{
		m_bDynLocation = conf["dynamicLocation"].get<bool>();
	}
	if (conf.contains("locationCalib"))
	{
		m_bLocationCalib = conf["locationCalib"].get<bool>();
	}
	if (conf.contains("longitudeCalib"))
	{
		m_dbLongitudeCalib = conf["longitudeCalib"].get<double>();
	}
	if (conf.contains("latitudeCalib"))
	{
		m_dbLatitudeCalib = conf["latitudeCalib"].get<double>();
	}
	if(conf.contains("longitude"))
		m_longitude = conf["longitude"];
	if(conf.contains("latitude"))
		m_latitude = conf["latitude"];
	//if (conf.contains("map")) {
	//	m_mapConf.merge_patch(conf["map"]);
	//}
		
	if (conf.contains("enableTask")) {
		m_bEnableTask = conf["enableTask"].get<bool>();
	}

	if (conf.contains("tasks")) {
		loadTask(conf["tasks"]);
	}

	if (conf["comment"].is_string()) {
		m_comment = conf["comment"];
	}

	if (conf["objID"].is_string()) {
		m_objID = conf["objID"];
	}

	if (conf["alias"].is_string()) {
		m_comment = conf["alias"];
	}

	if (conf.contains("customConf")) {
		m_customConf = conf["customConf"];
	}

	if (conf.contains("ioAddrBind"))
		m_strIoAddrBind = conf["ioAddrBind"];

	if (conf.contains("enableIO"))
	{
		m_bEnableIO = conf["enableIO"].get<bool>();
	}

	if (conf.contains("children")) {
		auto children = conf["children"];
		if (bCreate) {
			for (auto& child : children)
			{
				OBJ* pmo;
				if ((child.contains("level") && child["level"] == "mp") ||
					(child.contains("type") && child["type"] == "mp")) {  //保持一段时间兼容，后面删除
					pmo = new MP();
				}
				else
					pmo = new OBJ();

				if (pmo)
				{
					pmo->m_pParentMO = this; //放在loadConf之前，loadConf中会使用到m_pParentMO
					pmo->loadConf(child, bCreate);
					m_childObj.push_back(pmo);
				}
			}
		}
		else {
			//增量更新
			map<string, OBJ*> oldChildren;
			for (auto& i : m_childObj)
			{
				oldChildren[i->m_name] = i;
			}
			m_childObj.clear();

			for (auto& child : children) {
				if (child.contains("name")) {
					string childName = child["name"];
					auto iter = oldChildren.find(childName);
					if (iter != oldChildren.end()) {
						OBJ* pmo = iter->second;
						pmo->loadConf(child, false);
						m_childObj.push_back(pmo);
						oldChildren.erase(iter); //删除已处理的对象
					}
					else
					{
						OBJ* pmo;
						if (child.contains("level") && child["level"] == "mp") {
							pmo = new MP();
						}
						else
							pmo = new OBJ();
						if (pmo)
						{
							pmo->m_pParentMO = this; //放在loadConf之前，loadConf中会使用到m_pParentMO
							pmo->loadConf(child, true);
							m_childObj.push_back(pmo);
						}
					}
				}
			}
		}

	}

	return true;
}

bool OBJ::loadConf(yyjson_val* conf, bool bCreate) {
	//载入配置
	yyjson_val* v = yyjson_obj_get(conf, "name");
	if (v) {
		m_name = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "nameTranslate");
	if (v) {
		yyjson_val* key, * val;
		size_t indx = 0, max = 0;
		yyjson_obj_foreach(v, indx, max, key, val) {
			m_mapNameTranslate[yyjson_get_str(key)] = yyjson_get_str(val);
		}

		if (m_mapNameTranslate["zh"] == "") {
			m_mapNameTranslate["zh"] = m_name;
		}
	}

	v = yyjson_obj_get(conf, "level");
	if (v) {
		m_level = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "type");
	if (v) {
		m_type = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "childTds");
	if (v) {
		m_bChildTds = yyjson_get_bool(v);
	}

	v = yyjson_obj_get(conf, "streamAccess");
	if (v) {
		m_streamAccess = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "group");
	if (v) {
		m_groupName = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "dynamicLocation");
	if (v) {
		m_bDynLocation = yyjson_get_bool(v);
	}

	v = yyjson_obj_get(conf, "locationCalib");
	if (v) {
		m_bLocationCalib = yyjson_get_bool(v);
	}

	v = yyjson_obj_get(conf, "longitudeCalib");
	if (v) {
		m_dbLongitudeCalib = yyjson_get_num(v);
	}

	v = yyjson_obj_get(conf, "latitudeCalib");
	if (v) {
		m_dbLatitudeCalib = yyjson_get_num(v);
	}

	v = yyjson_obj_get(conf, "longitude");
	if (v) {
		m_longitude = yyjson_get_num(v);
	}

	v = yyjson_obj_get(conf, "latitude");
	if (v) {
		m_latitude = yyjson_get_num(v);
	}

	yyjson_val* yymap = yyjson_obj_get(conf, "map");
	if (yymap) {
		m_mapConf.enable = true;

		v = yyjson_obj_get(yymap, "center");
		m_mapConf.center[0] = yyjson_get_num(yyjson_arr_get(v, 0));
		m_mapConf.center[1] = yyjson_get_num(yyjson_arr_get(v, 1));

		v = yyjson_obj_get(yymap, "pitch");
		m_mapConf.pitch = yyjson_get_num(v);

		v = yyjson_obj_get(yymap, "rotation");
		m_mapConf.rotation = yyjson_get_num(v);

		v = yyjson_obj_get(yymap, "zoom");
		m_mapConf.zoom = yyjson_get_num(v);

		v = yyjson_obj_get(yymap, "viewMode");
		m_mapConf.viewMode = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "enableTask");
	if (v) {
		m_bEnableTask = yyjson_get_bool(v);
	}

	v = yyjson_obj_get(conf, "tasks");
	if (v) {
		loadTask(v);
	}

	v = yyjson_obj_get(conf, "comment");
	if (v) {
		m_comment = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "objID");
	if (v) {
		m_objID = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "alias");
	if (v) {
		m_alias = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "ioAddrBind");
	if (v) {
		m_strIoAddrBind = yyjson_get_str(v);
	}

	v = yyjson_obj_get(conf, "enableIO");
	if (v) {
		m_bEnableIO = yyjson_get_bool(v);
	}

	v = yyjson_obj_get(conf, "children");
	if (v) {
		yyjson_val* rootChildren = v;
		if (bCreate) {
			size_t indx = 0, max = 0;
			yyjson_val* childVal;
			yyjson_arr_foreach(rootChildren, indx, max, childVal) {
				OBJ* pmo;
				yyjson_val* yylevel = yyjson_obj_get(childVal, "level");
				string sLevel;

				if (yylevel) {
					sLevel = yyjson_get_str(yylevel);
				}

				if (sLevel == "mp"){
					pmo = new MP();
				}
				else {
					pmo = new OBJ();
				}

				if (pmo) {
					pmo->m_pParentMO = this; //放在loadConf之前，loadConf中会使用到m_pParentMO
					pmo->loadConf(childVal, bCreate);
					m_childObj.push_back(pmo);
				}
			}
		}
		else {
			//增量更新
			map<string, OBJ*> oldChildren;
			for (auto& i : m_childObj) {
				oldChildren[i->m_name] = i;
			}

			m_childObj.clear();

			size_t indx = 0, max = 0;
			yyjson_val* childVal;
			yyjson_arr_foreach(rootChildren, indx, max, childVal) {
				if (yyjson_obj_get(childVal, "name")) {
					string childName = yyjson_get_str(yyjson_obj_get(childVal, "name"));

					auto iter = oldChildren.find(childName);
					if (iter != oldChildren.end()) {
						OBJ* pmo = iter->second;
						pmo->loadConf(childVal, false);

						m_childObj.push_back(pmo);
						oldChildren.erase(iter); //删除已处理的对象
					}
					else {
						OBJ* pmo;
						yyjson_val* yylevel = yyjson_obj_get(childVal, "level");
						string sLevel;

						if (yylevel) {
							sLevel = yyjson_get_str(yylevel);
						}

						if (sLevel == "mp") {
							pmo = new MP();
						}
						else {
							pmo = new OBJ();
						}

						if (pmo) {
							pmo->m_pParentMO = this; //放在loadConf之前，loadConf中会使用到m_pParentMO
							pmo->loadConf(childVal, true);
							m_childObj.push_back(pmo);
						}
					}
				}
			}
		}
	}

	return true;
}

//根据leafType选择器，该节点是否要返回
//典型查询
//整颗树  leafType = mp
//部分对象类型  leafType = 风管机 + getMp:false
//部分对象类型加其监控点 leafType = 风管机 + getMp:true
bool OBJ::isSelectedByLeafType(string leafType)
{
	if (leafType == "")
		return true;
	if (leafType == "*" && m_type != "")
		return true;
	if (leafType == m_type)
		return true;
	if (leafType == "org" && m_name.find("子段") == string::npos)
		return true;
	if (leafType == "伤损指数")
	{
		if (m_level == "org" || m_name == "伤损指数" || (m_level == "mo" && m_name.find("子段") == string::npos))
		{
			return true;
		}
	}
	if (leafType == "mo" && m_name.find("子段") == string::npos)
		return true;
	return false;
}

bool OBJ::isSelectedByLeafLevel(string leafLevel)
{
	if (m_level == "")
		return true;
	if (m_level == "root") //根节点的level==root
		return true;

	if (leafLevel == "")
		return true;
	if (leafLevel == "*")
		return true;


	if (leafLevel == "mp")
		return true;
	else if (leafLevel == "mpGroup") {
		if (m_level == "mo" || m_level == "org" || m_level == "mpGroup")
			return true;
	}
	else if (leafLevel == "mo") {
		if (m_level == "mo" || m_level == "org")
			return true;
	}
	else if (leafLevel == "org") {
		if (m_level == "org")
			return true;
	}

	return false;
}

void OBJ::setChildMpOnline()
{
	if (!m_bOnline) {
		m_bOnline = true;
	}
	for (int i = 0; i < m_childObj.size(); i++) {
		OBJ* pC = m_childObj[i];
		if (pC->m_level == "mp") {
			pC->m_bOnline = m_bOnline;
		}
	}
}

void OBJ::setChildMpOffline()
{
	if (m_bOnline) {
		m_bOnline = false;
	}
	for (int i = 0; i < m_childObj.size(); i++) {
		OBJ* pC = m_childObj[i];
		if (pC->m_level == "mp") {
			pC->m_bOnline = m_bOnline;
		}
	}
}

void OBJ::recursiveSetOffline()
{
	if (m_bOnline) {
		m_bOnline = false;
	}
	for (int i = 0; i < m_childObj.size(); i++) {
		OBJ* pC = m_childObj[i];
		pC->recursiveSetOffline();
	}
}

//serializeOption
//root 返回位号的相对根
//type mo类型
//getChild 是否递归
//getStatus 是否包含状态信息
//getMp 是否获取mp。缺省获取

//bool OBJ::toJson(json& conf, json serializeOption)
//{
//	OBJ_QUERIER q = parseQuerier(serializeOption);
//	return toJson(conf, q);
//}

bool OBJ::toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, OBJ_QUERIER q, bool* parentSelectedByLeafType, const string& user) {
	//先进行权限判断
	if (user != "admin" && user != "") {//内部脚本调用时，user == ""
		if (m_level != "mp") {
			string sTag = getTag();
			if (!userMng.checkTagPermission(user, sTag)) {
				return false;
			}
		}
	}

	if (q.leafLevel != "" && !isSelectedByLeafLevel(q.leafLevel)) {
		return false;
	}

	bool selectedByLeafType = false;

	//leafType开关算法逻辑
	//如果指定了叶子节点选择,向该路径上的上级节点返回parentSelectedByLeafType=true,表示该路径被选中
	//路径代表 不同层级节点连成的一条链路

	//1.判断自己是否被leafType选择器选中
	//1.1 自己就是叶子节点
	if (q.leafType != "" && q.leafType == m_type) {
		selectedByLeafType = true;
	}
	//1.2 自己不是叶子节点,根据子节点判断是否被选中
	else {
		//if (m_level != MO_TYPE::mp)
		{
			yyjson_mut_val* rootChildren = yyjson_mut_arr(doc);

			if (!q.flatten) {
				for (auto& pmochild : m_childObj) {
					//可以出现 getMp=true ,getChild=false的组合，因此getChild不代表getMp，虽然Mp也是child
					if (pmochild->m_level == "mp") {
						if (!q.getMp) continue;
					}
					else {
						if (!q.getChild) continue;
					}

					yyjson_mut_val* rootChild = yyjson_mut_obj(doc);
					if (pmochild->toJson(rootChild, doc, q, &selectedByLeafType, user)) {
						yyjson_mut_arr_append(rootChildren, rootChild);
					}
				}
			}
			else {
				vector<MP*> mpList;
				GetAllChildMp(mpList);

				for (auto& mp : mpList) {
					yyjson_mut_val* rootMp = yyjson_mut_obj(doc);

					if (mp->toJson(rootMp, doc, q, nullptr, user)) {
						string flattenName = mp->getTag();
						flattenName = TAG::trimRoot(flattenName, getTag());
						
						yyjson_mut_val* key = yyjson_mut_strcpy(doc, "name");
						yyjson_mut_val* val = yyjson_mut_strcpy(doc, flattenName.c_str());
						yyjson_mut_obj_put(rootMp, key, val);
						yyjson_mut_arr_append(rootChildren, rootMp);
					}
				}
			}

			size_t len = yyjson_mut_arr_size(rootChildren);
			if (len > 0) {
				yyjson_mut_val* key = yyjson_mut_strcpy(doc, "children");
				yyjson_mut_obj_put(conf, key, rootChildren);
			}
		}
	}

	//2.将判断结果传递给父节点
	if (parentSelectedByLeafType && selectedByLeafType) {
		*parentSelectedByLeafType = selectedByLeafType;
	}

	//3.根据自己是否被选中,进行响应的操作
	//子节点遍历后,本节点下面没有找到该leafType类型
	if (q.leafType != "" && !selectedByLeafType) {
		return false;
	}

	yyjson_mut_val* key = yyjson_mut_strcpy(doc, "name");
	yyjson_mut_val* val = yyjson_mut_strcpy(doc, getName(q.language).c_str());
	yyjson_mut_obj_put(conf, key, val);

	if (m_mapNameTranslate.size() > 0) {
		yyjson_mut_val* root = yyjson_mut_obj(doc);

		for (auto& i : m_mapNameTranslate) {
			yyjson_mut_val* key = yyjson_mut_strcpy(doc, i.first.c_str());
			yyjson_mut_val* val = yyjson_mut_strcpy(doc, i.second.c_str());
			yyjson_mut_obj_put(root, key, val);
		}

		yyjson_mut_val* key = yyjson_mut_strcpy(doc, "nameTranslate");
		yyjson_mut_obj_put(conf, key, root);
	}

	key = yyjson_mut_strcpy(doc, "level");
	val = yyjson_mut_strcpy(doc, m_level.c_str());
	yyjson_mut_obj_put(conf, key, val);

	if (q.getTag) {
		string tag = getTag("", q.language);

		if (q.rootTag != "") {
			tag = TAG::trimRoot(tag, q.rootTag);
			key = yyjson_mut_strcpy(doc, "rootTag");
			val = yyjson_mut_strcpy(doc, q.rootTag.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		if (tag != "") {
			key = yyjson_mut_strcpy(doc, "tag");
			val = yyjson_mut_strcpy(doc, tag.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
	}

	if (q.getConfDetail) {
		if (m_strIoAddrBind != "") {
			key = yyjson_mut_strcpy(doc, "ioAddrBind");
			val = yyjson_mut_strcpy(doc, m_strIoAddrBind.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
	}

	if (q.getConf) {
		if (m_bChildTds) {
			key = yyjson_mut_strcpy(doc, "childTds");
			val = yyjson_mut_true(doc);
			yyjson_mut_obj_put(conf, key, val);

			key = yyjson_mut_strcpy(doc, "streamAccess");
			val = yyjson_mut_strcpy(doc, m_streamAccess.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_type != "") {
			
			key = yyjson_mut_strcpy(doc, "type");
			val = yyjson_mut_strcpy(doc, m_type.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_groupName != "") {
			key = yyjson_mut_strcpy(doc, "group");
			val = yyjson_mut_strcpy(doc, m_groupName.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_bDynLocation) {
			key = yyjson_mut_strcpy(doc, "dynamicLocation");
			val = yyjson_mut_bool(doc, m_bDynLocation);
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_bLocationCalib) {
			key = yyjson_mut_strcpy(doc, "locationCalib");
			val = yyjson_mut_bool(doc, m_bLocationCalib);
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_dbLongitudeCalib > 0.000001) {
			key = yyjson_mut_strcpy(doc, "longitudeCalib");
			val = yyjson_mut_real(doc, m_dbLongitudeCalib);
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_dbLatitudeCalib > 0.000001) {
			key = yyjson_mut_strcpy(doc, "latitudeCalib");
			val = yyjson_mut_real(doc, m_dbLatitudeCalib);
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_mapConf.enable) {
			yyjson_mut_val* yyMap = yyjson_mut_obj(doc);
			yyjson_mut_val* yyCenter = yyjson_mut_arr(doc);

			yyjson_mut_arr_append(yyCenter, yyjson_mut_real(doc, m_mapConf.center[0]));
			yyjson_mut_arr_append(yyCenter, yyjson_mut_real(doc, m_mapConf.center[1]));

			key = yyjson_mut_strcpy(doc, "center");
			yyjson_mut_obj_put(yyMap, key, yyCenter);

			key = yyjson_mut_strcpy(doc, "pitch");
			yyjson_mut_obj_put(yyMap, key, yyjson_mut_real(doc, m_mapConf.pitch));

			key = yyjson_mut_strcpy(doc, "rotation");
			yyjson_mut_obj_put(yyMap, key, yyjson_mut_real(doc, m_mapConf.rotation));

			key = yyjson_mut_strcpy(doc, "zoom");
			yyjson_mut_obj_put(yyMap, key, yyjson_mut_real(doc, m_mapConf.zoom));

			key = yyjson_mut_strcpy(doc, "viewMode");
			yyjson_mut_obj_put(yyMap, key, yyjson_mut_strcpy(doc, m_mapConf.viewMode.c_str()));

			key = yyjson_mut_strcpy(doc, "map");
			yyjson_mut_obj_put(conf, key, yyMap);
		}
		

		if (m_longitude != INVALID_COORD) {
			key = yyjson_mut_strcpy(doc, "longitude");
			yyjson_mut_obj_put(conf, key, yyjson_mut_real(doc, m_longitude));
		}

		if (m_latitude != INVALID_COORD) {
			key = yyjson_mut_strcpy(doc, "latitude");
			yyjson_mut_obj_put(conf, key, yyjson_mut_real(doc, m_latitude));
		}

		if (m_comment != "") {
			key = yyjson_mut_strcpy(doc, "comment");
			val = yyjson_mut_strcpy(doc, m_comment.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}
		if (m_alias != "") {
			key = yyjson_mut_strcpy(doc, "alias");
			val = yyjson_mut_strcpy(doc, m_alias.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_objID != "") {
			key = yyjson_mut_strcpy(doc, "objID");
			val = yyjson_mut_strcpy(doc, m_objID.c_str());
			yyjson_mut_obj_put(conf, key, val);
		}

		if (m_scheduleTasks.size() > 0) {
			key = yyjson_mut_strcpy(doc, "enableTask");
			val = yyjson_mut_bool(doc, m_bEnableTask);
			yyjson_mut_obj_put(conf, key, val);

			yyjson_mut_val* yyv_task_list = yyjson_mut_arr(doc);

			for (int i = 0; i < m_scheduleTasks.size(); i++) {
				yyjson_mut_val* yyv_task = yyjson_mut_obj(doc);
				SCHEDULE_TASK& st = m_scheduleTasks[i];
				st.toJson(yyv_task, doc);
				yyjson_mut_arr_append(yyv_task_list, yyv_task);
			}

			yyjson_mut_val* key = yyjson_mut_strcpy(doc, "tasks");
			yyjson_mut_obj_put(conf, key, yyv_task_list);
		}

		if (m_bEnableIO == false) {
			key = yyjson_mut_strcpy(doc, "enableIO");
			val = yyjson_mut_bool(doc, m_bEnableIO);
			yyjson_mut_obj_put(conf, key, val);
		}
	}

	//运行时状态数据
	if (q.getStatus) {
		if (tds->conf->showObjOnline == true) {
			if (m_strIoAddrBind != "" || isCustomMo() || m_bChildTds) {
				key = yyjson_mut_strcpy(doc, "online");
				val = yyjson_mut_bool(doc, m_bOnline);
				yyjson_mut_obj_put(conf, key, val);
			}

			if (m_level == "mp" && m_pParentMO->m_strIoAddrBind != "") {
				key = yyjson_mut_strcpy(doc, "online");
				val = yyjson_mut_bool(doc, m_pParentMO->m_bOnline);
				yyjson_mut_obj_put(conf, key, val);
			}
		}
	}

	if (q.getStatusDesc) {
		if (m_strIoAddrBind != "" || isCustomMo()) {
			if (q.getStatusDesc) {
				string desc = m_bOnline ? "在线" : "离线";
				key = yyjson_mut_strcpy(doc, "onlineDesc");
				val = yyjson_mut_strcpy(doc, desc.c_str());
				yyjson_mut_obj_put(conf, key, val);
			}
		}
	}
	return true;
}

bool OBJ::loadTreeStatus(OBJ* pSrcTree)
{
	string tag = getTag();
	OBJ* pSrcObj = pSrcTree->queryObj(tag,"zh");
	if (pSrcObj) {
		loadObjStatus(pSrcObj);
		for (int i = 0; i < m_childObj.size(); i++)
		{
			OBJ* pC = m_childObj[i];
			pC->loadTreeStatus(pSrcTree);
		}
	}
	else
		return false;
	return true;
}

bool OBJ::loadObjStatus(OBJ* pSrcObj)
{
	m_bOnline = pSrcObj->m_bOnline;
	m_stDataLastUpdate = pSrcObj->m_stDataLastUpdate;
	m_longitudeDyn = pSrcObj->m_longitudeDyn;
	m_latitudeDyn = pSrcObj->m_latitudeDyn;
	m_status = pSrcObj->m_status;
	m_jAlarmStatus = pSrcObj->m_jAlarmStatus;
	m_strIoAddrBind = pSrcObj->m_strIoAddrBind;
	return true;
}

void OBJ::toAttrInfo(nlohmann::ordered_json& attrInfo)
{
}

void OBJ::getVal(yyjson_mut_val*& val, yyjson_mut_doc* doc)
{

}

bool OBJ::isCustomMo()
{
	if (m_level == MO_TYPE::mo && m_type != "")
		return true;
	return false;
}

bool OBJ::isCustomMp()
{
	if (m_level == MO_TYPE::mp && m_type != "")
		return true;
	return false;
}

bool OBJ::isCustomOrg()
{
	if (m_level == MO_TYPE::customOrg)
		return true;
	if (m_level == MO_TYPE::org && m_type != "")
		return true;
	return false;
}


// 有一个MP::loadStatus重载
bool OBJ::loadStatus(yyjson_val* status)
{
	if (yyjson_is_obj(status)){
		yyjson_val* v = yyjson_obj_get(status, "online");
		if(v)
			m_bOnline = yyjson_get_bool(v);
		v = yyjson_obj_get(status, "longitudeDyn");
			//m_longitudeDyn = status["longitudeDyn"];
		//if (status.contains("latitudeDyn"))
		//	m_latitudeDyn = status["latitudeDyn"];
		//if (status.contains("alarmStatus"))
		//	m_jAlarmStatus = status["alarmStatus"];

		v = yyjson_obj_get(status, "children");
		if (v) {
			size_t idx, max;
			yyjson_val* child;
			map<string, OBJ*> mapObj;
			for (auto& i : m_childObj) {
				mapObj[i->m_name] = i;
			}

			yyjson_arr_foreach(v,idx,max,child){
				yyjson_val* yyname = yyjson_obj_get(child, "name");
				auto iter = mapObj.find(yyjson_get_str(yyname));
				if (iter != mapObj.end()) {
					iter->second->loadStatus(child);
				}
			}
		}
	}

	return true;
}

bool OBJ::saveStatus(json& statusNode)
{
	statusNode["name"] = m_name;
	if (m_level == "mp") {
		MP* pmp = (MP*)this;
		statusNode["val"] = json::parse(pmp->m_curVal);
		statusNode["time"] = pmp->m_stDataLastUpdate.toStr();
	}

	if (m_childObj.size() > 0) {
		json jChildren = json::array();
		for (int i = 0; i < m_childObj.size(); i++) {
			json j;
			OBJ* p = m_childObj[i];
			p->saveStatus(j);
			jChildren.push_back(j);
		}
		statusNode["children"] = jChildren;
	}

	return true;
}

bool OBJ::saveStatus(yyjson_mut_val* statusNode,yyjson_mut_doc* doc)
{
	yyjson_mut_val* yykey, *yyval;
	yykey = yyjson_mut_strcpy(doc, "name");
	yyjson_mut_obj_put(statusNode, yykey, yyjson_mut_strcpy(doc, m_name.c_str()));
	if (m_level == "mp") {
		MP* pmp = (MP*)this;
		yykey = yyjson_mut_strcpy(doc, "val");
		pmp->m_curValLock.lock();
		string sCurVal = pmp->m_curVal;
		string sTime = pmp->m_stDataLastUpdate.toStr();
		pmp->m_curValLock.unlock();
		yyjson_doc* d = yyjson_read(sCurVal.c_str(), sCurVal.size(), 0);
		yyjson_val* r = yyjson_doc_get_root(d);
		yyval = yyjson_val_mut_copy(doc, r);
		yyjson_doc_free(d);
		yyjson_mut_obj_put(statusNode, yykey, yyval);

		yykey = yyjson_mut_strcpy(doc, "time");
		yyval = yyjson_mut_strcpy(doc, sTime.c_str());
		yyjson_mut_obj_put(statusNode, yykey, yyval);
	}

	if (m_childObj.size() > 0) {
		yyjson_mut_val* yyChildren = yyjson_mut_arr(doc);
		for (int i = 0; i < m_childObj.size(); i++) {
			OBJ* p = m_childObj[i];
			yyjson_mut_val* yyChild = yyjson_mut_obj(doc);
			p->saveStatus(yyChild, doc);
			yyjson_mut_arr_append(yyChildren, yyChild);
		}
		yykey = yyjson_mut_strcpy(doc, "children");
		yyjson_mut_obj_put(statusNode, yykey, yyChildren);
	}
	return false;
}

bool OBJ::toJson(YY_OBJ_VAL& yyObj, OBJ_QUERIER querier, bool* isSelectedByLeafType, const string& user)
{
	return toJson(yyObj.val, yyObj.doc, querier, isSelectedByLeafType, user);
}


void OBJ::removeMp(json& mo)
{
	if (mo["children"] != nullptr)
	{
		json jChildren = mo["children"];
	}
}

void OBJ::clearChildren()
{
	for (auto& i : m_childObj)
	{
		delete i;
	}
	m_childObj.clear();
}

OBJ* OBJ::getOwnerChildTds()
{
	OBJ* pTmp = this;
	while (pTmp)
	{
		if (pTmp->m_bChildTds && pTmp->m_pParentMO!=nullptr)
			return pTmp;

		pTmp = pTmp->m_pParentMO;
	}

	return nullptr;
}


OBJ* OBJ::GetProjectMO()
{
	OBJ* pTmp = this;
	while (pTmp->m_pParentMO)
	{
		pTmp = pTmp->m_pParentMO;
	}

	return pTmp;
}


bool OBJ::hasOnlineStatus() {
	//内部变量即使在绑定设备的对象下方，也是没有在线状态的
	if (m_level == "mp") {
		MP* pmp = (MP*)this;
		if (pmp->m_ioType == IO_TYPE::InnerVar) {
			return false;
		}
		if (pmp->m_ioType == IO_TYPE::Const) {
			return false;
		}
	}

	if (isChildObjOfIntelliDev())
		return true;

	return false;
}


bool OBJ::isChildObjOfIntelliDev()
{
	OBJ* p = this;
	while (p) {
		if (p->m_strIoAddrBind != "") {
			return true;
		}
		p = p->m_pParentMO;
	}

	return false;
}
OBJ* OBJ::createObjBranchByTag(string tag)
{
	vector<string> nodes;
	str::split(nodes, tag, ".");
	OBJ* pParent = &prj;
	OBJ* pChild = nullptr;
	for (int i = 0; i < nodes.size(); i++) {
		pChild = pParent->GetChildObjByName(nodes[i]);
		if (!pChild) {
			pChild = new OBJ();
			pChild->m_name = nodes[i];
			pParent->m_childObj.push_back(pChild);
			pChild->m_pParentMO = pParent;
		}
		pParent = pChild;
	}
	return pChild;
}
void OBJ::treeStatus2ListStatus(json& tree, json& list,json& onlineStatus, string tag)
{
	if (tree["level"] == "mp") {
		json jDe;
		jDe["tag"] = tag;
		jDe["time"] = tree["time"];
		jDe["val"] = tree["val"];
		list.push_back(jDe);
	}

	//自定义对象同步在线状态
	if (tree["type"] != "" && tree["online"].is_boolean()) {
		json jos;
		jos["tag"] = tag;
		jos["online"] = tree["online"];
		onlineStatus.push_back(jos);
		//LOG("同步对象在线状态,位号:%s,状态;%d", tag.c_str(), jos["online"].get<bool>() ? 1 : 0);
	}

	string name = tree["name"];
	if (tree["children"].is_array()) {
		json& jChildren = tree["children"];
		for (int i = 0; i < jChildren.size(); i++) {
			json& jC = jChildren[i];
			string cName = jC["name"];
			string cTag = TAG::addRoot(cName, tag);
			treeStatus2ListStatus(jC, list, onlineStatus, cTag);
		}
	}
}
json OBJ::getRT()
{
	json j;
	j["name"] = m_name;
	j["type"] = m_level;
	json jChildren;
	for(int i=0;i<m_childObj.size();i++)
	{
		OBJ* pmo = m_childObj.at(i);
		jChildren.push_back(pmo->getRT());
	}
	j["children"]=jChildren;
	return j;
}

string& OBJ::getName(const string& language)
{
	if (language == "")
		return m_name;
	map<string, string>::iterator iter = m_mapNameTranslate.find(language);
	if (iter != m_mapNameTranslate.end())
		return iter->second;
	if (language == "pinyin") {
		str::hanZi2Pinyin(m_name, m_namePinyin);
		return m_namePinyin;
	}
	return m_name;
}

string OBJ::getTag()
{
	return getTag("", "");
}

string OBJ::getTag(string root,string language)
{
	if (m_pParentMO == nullptr)
		return "";

	OBJ* pTmpParent = m_pParentMO;
	string strTagName = getName(language);

	while (pTmpParent && pTmpParent->m_pParentMO)//第一级位号工程名称默认不显示
	{
		strTagName = pTmpParent->getName(language) + "." + strTagName;
		pTmpParent = pTmpParent->m_pParentMO;
	}

	if (root != "")
	{
		strTagName = str::trimPrefix(strTagName, root);
		strTagName = str::trimPrefix(strTagName, ".");
	}
		
	return strTagName;
}

json OBJ::getTypeTag()
{
	OBJ* p = this;
	json j;
	while(p){
		if (p->m_type != "") {
			j[p->m_type] = p->m_name;
		}
		p = p->m_pParentMO;
	}
	return j;
}

vector<string> OBJ::GetAlias()
{
	vector<string> vecAlias;
	str::removeChar(m_alias, ' ');
	if (m_alias.length() == 0)
		return vecAlias;
	str::split(vecAlias, m_alias, ",");
	return vecAlias;
}

vector<string> OBJ::GetAllTagNamePlus()
{
	OBJ* pTmpParent = m_pParentMO;
	string tag = m_name;

	//获得名字数组
	vector<string> vecTagName;
	vecTagName.push_back(tag); //原名放前面，别名放后面
	vector<string> vecTagAlias = GetAlias();
	vecTagName.insert(vecTagName.end(), vecTagAlias.begin(), vecTagAlias.end());


	while (pTmpParent)//包含工程节点名称的位号
	{
		vector<string> vecParentName;
		vecParentName.push_back(pTmpParent->m_name); //原名放前面，别名放后面
		vector<string> vecParentNameAlias = pTmpParent->GetAlias();
		vecParentName.insert(vecParentName.end(), vecParentNameAlias.begin(), vecParentNameAlias.end());


		//排列组合所有可能的位号名称
		vector<string> vecChildSubTag = vecTagName;
		vecTagName.clear();
		for (int i = 0; i < vecChildSubTag.size(); i++)
		{
			string childtag = vecChildSubTag.at(i);
			for (int j = 0; j < vecParentName.size(); j++)
			{
				string parentname = vecParentName.at(j);
				tag = parentname + "." + childtag;
				vecTagName.push_back(tag);
			}
		}
		pTmpParent = pTmpParent->m_pParentMO;
	}

	return vecTagName;
}


string OBJ::getTagWithRoot()
{
	OBJ* pTmpParent = m_pParentMO;
	string strTagName = m_name;
	while (pTmpParent)//包含工程节点名称的位号
	{
		strTagName = pTmpParent->m_name + "." + strTagName;
		pTmpParent = pTmpParent->m_pParentMO;
	}
	return strTagName;
}

void OBJ::queryObj(std::vector<OBJ*>* tagVec, string strTag, string language,string type, string level) {
	if (strTag.find("*") == string::npos) {//精确查找
		//如果这是一个TDS子服务对象，m_rootTag不为空。
		//使用父服务的位号，在子服务中查询对象，需要先除去rootTag
		if (m_rootTag != "") {
			strTag = TAG::trimRoot(strTag, m_rootTag);
		}

		//判断自身位号是否是搜索位号的上级位号，如果不是一定搜索不到
		string tagThis = getTag("",language);
		if (strTag.find(tagThis) == string::npos) {
			return;
		}

		strTag = TAG::trimRoot(strTag, tagThis);
		if (strTag == ""){
			tagVec->push_back(this);
		}

		//根据相对位号的名字节点，查找子对象的名字，获取到对象
		vector<string> vecNames;
		str::split(vecNames, strTag, ".");

		OBJ* toQuery = NULL;
		std::vector<OBJ*>* childMO = &m_childObj;
		bool findMO = false;

		for (int i = 0; i < vecNames.size(); i++) {
			string name = vecNames[i];

			bool findNode = false;
			for (int j = 0; j < childMO->size(); j++) {
				OBJ* tmp = childMO->at(j);

				string tmpName = tmp->getName(language);
				if (tmpName == name) {
					toQuery = tmp;
					findNode = true;

					if (i == vecNames.size() - 1) {
						findMO = true;
					}

					break;
				}
			}

			if (findNode) {
				childMO = &toQuery->m_childObj;
			}
			else {
				break;
			}
		}

		if (findMO) {
			if (toQuery->isSelectedByLevel(level) && toQuery->isSelectedByType(type)) {
				tagVec->push_back(toQuery);
			}
		}
	}
	else { //通配符匹配
		if (isSelectedByLevel(level) && isSelectedByType(type)) {
			string tagCandidate = getTag("",language);

			TAG_SELECTOR ts;
			ts.init(strTag);
			ts.selLanguage = language;

			if (ts.match(tagCandidate)) {
				tagVec->push_back(this);
			}
		}

		for (int i = 0; i < m_childObj.size(); i++) {
			OBJ* pMOChild = m_childObj.at(i);
			pMOChild->queryObj(tagVec, strTag, language,type, level);
		}
	}
}

void OBJ::GetMPByTag(std::vector<MP*>* tagVec, string strTag, string language)
{
	std::vector<OBJ*> vec;
	queryObj(&vec, strTag, language);
	for (int i = 0; i < vec.size(); i++)
	{
		OBJ* p = vec.at(i);
		if (p->m_level == "mp")
		{
			tagVec->push_back((MP*)p);
		}
	}
}

void OBJ::getMpList(vector<MP*>& MPlist)
{
	if (m_level == "mp") {
		MPlist.push_back((MP*)this);
	}

	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* p = m_childObj.at(i);
		if (p->m_level == "mp")
		{
			MPlist.push_back((MP*)p);
		}
		else
			p->getMpList(MPlist);
	}
}


void OBJ::getMpList(map<string, MP*>& MPlist)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* p = m_childObj.at(i);
		if (p->m_level == "mp")
		{
			MPlist[p->getTag().c_str()] = (MP*)p;
		}
		else
			p->getMpList(MPlist);
	}
}

OBJ* OBJ::queryObj(string strTag,string language)
{
	if (strTag == "")
		return this;

	vector<OBJ*> tags;
	queryObj(&tags, strTag, language);
	if (tags.size() > 0)
		return tags[0];
	else
		return NULL;
}

MP* OBJ::GetMPByTag(string strTag, string language)
{
	OBJ* pMO = queryObj(strTag, language);
	if (pMO && pMO->m_level == "mp")
		return (MP*)pMO;
	return nullptr;
}

MP* OBJ::GetMPByTagPinyin(string strTag)
{
	return GetMPByTag(strTag, "pinyin");
}

OBJ* OBJ::GetChildObjByName(string strName)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* pMOChild = m_childObj.at(i);
		if (pMOChild->m_name == strName) {
			return pMOChild;
		}
	}
	return NULL;
}

MP* OBJ::GetDescendantMPByName(string strName)
{
	OBJ* p = GetDescendantObjByName(strName);
	if (p && p->m_level == "mp")
	{
		return (MP*)p;
	}
	return NULL;
}

OBJ* OBJ::GetDescendantObjByName(string strName)
{
	if (m_name == strName)
		return this;
	else
	{
		for (int i = 0; i < m_childObj.size(); i++)
		{
			OBJ* pMOChild = m_childObj.at(i);
			OBJ* pFind = pMOChild->GetDescendantObjByName(strName);
			if (pFind)
				return pFind;
		}
	}

	return NULL;
}

bool OBJ::isSelectedByIOType(const string& ioType) {
	if (ioType == "")
		return true;

	if (m_level != "mp") {
		return false;
	}

	MP* pmp = (MP*)this;
	if (pmp->m_ioType == ioType)
		return true;
	return false;
}


bool OBJ::isSelectedByLevel(const string& level)
{
	if (level == "" || level == "*")
		return true;

	if (level == "org") {
		if (m_level == "org")
			return true;
		else
			return false;
	}							
	else if (level == "mo") {
		if (m_level == "mo")
			return true;
		else
			return false;
	}
	else if (level == "mp") {
		if (m_level == "mp")
			return true;
		else
			return false;
	}
	else {
		return false;
	}
}

bool OBJ::isSelectedByType(const string& type)
{
	if (type == "") //全选
		return true;
	else if (type == "*" && m_type != "")//选中所有自定义类型
		return true;
	else if (type == m_type) {
		return true;
	}
	return false;
}

vector<string> OBJ::getTagPartials(string strTag)
{
	//使用*分割
	vector<string> ary;
	str::split(ary,strTag, "*");
	//除去头尾的.号
	vector<string> aryPartials;
	for (int i = 0; i < ary.size(); i++)
	{
		string str = ary.at(i);
		if (str.at(0) == '.')
		{
			str = str.substr(1,str.length() - 1);
		}
		if (str.at(str.length() - 1) == '.')
		{
			str = str.substr(0,str.length() - 1);
		}
		aryPartials.push_back(str);
	}

	return aryPartials;
}

string OBJ::getTypeLabel(string type)
{
	return "";
}

string OBJ::getUpdateTimeDesc()
{
	if (timeopt::isValidTime(m_stDataLastUpdate)) {
		return timeopt::st2str(m_stDataLastUpdate);
	}
	else {
		return "-";
	}
}


OBJ* OBJ::createChildMO(string subTag,string moType)
{
	vector<string> tagNode;
	str::split(tagNode,subTag,".");

	OBJ* pParent = this;
	OBJ* pmo = NULL;
	for (int i = 0; i < tagNode.size(); i++)
	{
		string strName = tagNode[i];
		if (i == tagNode.size() - 1) //last node
		{
			pmo = createMO(moType);
		}
		else
		{
			pmo = createMO(MO_TYPE::mo);
		}
		pmo->m_name = strName;
		pmo->m_pParentMO = pParent;
		pParent->m_childObj.push_back(pmo);
		pParent = pmo;
	}

	return pmo;
}

void OBJ::GetAllChildObj(std::vector<OBJ*>& aryMO, string type)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		if (m_childObj[i]->m_level == type)
		{
			aryMO.push_back(m_childObj[i]);
		}
		m_childObj[i]->GetAllChildObj(aryMO, type);
	}
}

void OBJ::GetAllChildMp(std::vector<MP*>& aryMP)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		if (m_childObj[i]->m_level == "mp")
		{
			aryMP.push_back((MP*)m_childObj[i]);
		}
		m_childObj[i]->GetAllChildMp(aryMP);
	}
}

void OBJ::GetAttriMp(std::vector<MP*>& aryMP)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		if (m_childObj[i]->m_level == "mp")
		{
			aryMP.push_back((MP*)m_childObj[i]);
		}
		if (m_childObj[i]->m_level == MO_TYPE::mpgroup) {
			m_childObj[i]->GetAttriMp(aryMP);
		}
	}
}


map<string, json> OBJ::getChildCustomTypeList(string level)
{
	if (m_childCustomMoTypeList.size() == 0)
		statisChildCustomMoType(m_childCustomMoTypeList);

	
	if(level == "*")
		return m_childCustomMoTypeList;
	else {
		map<string, json> retMap;
		for (auto& i: m_childCustomMoTypeList)
		{
			json& j = i.second;
			string sLevel = j["level"];
			//支持 mp,mo 逗号分隔的多选模式
			if (level.find(sLevel)!=string::npos) {
				retMap[i.first] = i.second;
			}
		}
		return retMap;
	}
}

void OBJ::statisChildCustomMoType(map<string, json>& list)
{
	for (auto& i : m_childObj)
	{
		if (i->m_type!="")
		{
			string type = i->m_type;
			map<string, json>::iterator iter = list.find(type);
			if (iter != list.end()) {
				json& j = iter->second;
				int count = j["count"].get<int>();
				count++;
				j["count"] = count;
			}
			else {
				json jType;
				jType["type"] = type;
				jType["count"] = 1;
				if (i->isCustomOrg())
					jType["level"] = "org";
				else if(i->isCustomMo())
					jType["level"] = "mo";
				else if (i->m_level == "mp")
					jType["level"] = "mp";

				list[type] = jType;
			}
		}

		i->statisChildCustomMoType(list);
	}
}

string OBJ::getChildObjStatis()
{
	map<string,json> mapCustomObj;
	statisChildCustomMoType(mapCustomObj);

	vector<MP*> mpList;
	getMpList(mpList);

	string s = str::format("监控点:%d", mpList.size());
	for (auto& iter : mapCustomObj) {
		string cs = str::format(",%s:%d", iter.second["type"].get<string>().c_str(), iter.second["count"].get<int>());
		s += cs;
	}

	return s;
}

void OBJ::statisChildObj(map<string, OBJ_STATIS>& rlt) {
	if (isCustomMo() || isCustomMp()) {
		OBJ_STATIS os;
		if (rlt.find(m_type) != rlt.end()) {
			os = rlt[m_type];
		}
		else {
			os.customType = m_type;
		}

		os.count++;

		//监控点暂时全做在线处理
		if (m_level == "mp") {
			os.online++;
		}
		else {
			if (m_bOnline) {
				os.online++;
			}
			else {
				os.offline++;
			}
		}


		if (m_level == "mp") {
			MP* pmp = (MP*)this;
			if (JSON_STR::is_str(pmp->m_curVal)) {
				string sCur = JSON_STR::get_str(pmp->m_curVal);
				if (sCur == "报警") {
					os.alarm++;
				}
				else if(sCur == "故障") {
					os.fault++;
				}
				else {
					os.normal++;
				}
			}
			else {
				os.normal++;
			}
		}
		else {
			m_mxAlarmStatus.lock();
			if (m_jAlarmStatus != nullptr) {
				os.alarm++;
			}
			else {
				os.normal++;
			}
			m_mxAlarmStatus.unlock();
		}

		rlt[m_type] = os;
	}

	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* pC = m_childObj[i];
		pC->statisChildObj(rlt);
	}
}

void OBJ::statisChildMo(json& jStatis)
{
	if (jStatis["customOrg"] == nullptr) {
		jStatis["customOrg"] = 0;
	}

	if (jStatis["online"] == nullptr) {
		jStatis["online"] = 0;
	}

	if (jStatis["offline"] == nullptr) {
		jStatis["offline"] = 0;
	}

	if (jStatis["smartDev"] == nullptr) {
		jStatis["smartDev"] = 0;
	}



	if (isCustomOrg()) {
		jStatis["customOrg"] = jStatis["customOrg"].get<int>() + 1;
	}
	else if (isCustomMo()) {
		jStatis["smartDev"] = jStatis["smartDev"].get<int>() + 1;

		if (m_bOnline)
			jStatis["online"] = jStatis["online"].get<int>() + 1;
		else
			jStatis["offline"] = jStatis["offline"].get<int>() + 1;
	}

	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* pC = m_childObj[i];
		pC->statisChildMo(jStatis);
	}
}

OBJ_QUERIER OBJ::parseQuerier(json& opt)
{
	OBJ_QUERIER q;
	if (opt == nullptr)
		return q;

	if (opt.contains("getStatus")) {
		q.getStatus = opt["getStatus"].get<bool>();
	};
	if (opt.contains("getMp")) {
		q.getMp = opt["getMp"].get<bool>();
	}
	if (opt["getChild"] != nullptr) {
		q.getChild = opt["getChild"].get<bool>();
	}
	if (opt["getConf"] != nullptr) {
		q.getConf = opt["getConf"].get<bool>();
	}
	if (opt["leafType"] != nullptr)
	{
		q.leafType = opt["leafType"].get<string>();
	}
	if (opt["leafLevel"] != nullptr) {
		q.leafLevel = opt["leafLevel"].get<string>();
	}
	if (opt["match"] != nullptr) {
		q.match = opt["match"].get<string>();
	}
	if (opt["getDetailConf"] != nullptr) {
		q.getConfDetail = opt["getDetailConf"].get<bool>();
	}
	if (opt["rootTag"] != nullptr){
		q.rootTag = opt["rootTag"].get<string>();
	}
	if (opt["getStatusDesc"] != nullptr) {
		q.getStatusDesc = opt["getStatusDesc"].get<bool>();
	}
	if (opt["getUnit"] != nullptr) {
		q.getUnit = opt["getUnit"].get<bool>();
	}
	if (opt["getVal"] != nullptr) {
		q.getVal = opt["getVal"].get<bool>();
	}
	if (opt["getTag"] != nullptr) {
		q.getTag = opt["getTag"].get<bool>();
	}
	if (opt["getValDesc"].is_boolean()) {
		q.getValDesc = opt["getValDesc"].get<bool>();
	}
	if (opt["flatten"].is_boolean()) {
		q.flatten = opt["flatten"].get<bool>();
	}
	if (opt["dataSaveMp"].is_boolean()) {
		q.dataSaveMp = opt["dataSaveMp"].get<bool>();
	}
	if (opt["getStatusDetail"].is_boolean()) {
		q.getStatusDetail = opt["getStatusDetail"].get<bool>();
	}
	return q;
}


OBJ* OBJ::GetRootMO()
{
	if (this == nullptr)
		return nullptr;

	OBJ* p = this;

	while (p->m_pParentMO)
	{
		p = p->m_pParentMO;
	}

	return p;
}

OBJ* OBJ::GetFatherMO(string type)
{
	OBJ* p = this;

	while (p)
	{
		if (p->m_level == type)
		{
			return p;
		}

		p = p->m_pParentMO;
	}

	return NULL;
}

OBJ* OBJ::GetChildMO(string type)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* pMO = m_childObj.at(i);
		if (pMO->m_level == type)
			return pMO;

		OBJ* pChild = pMO->GetChildMO(type);
		if (pChild)
			return pChild;
	}

	return NULL;
}

OBJ* OBJ::CopyMO()
{
	OBJ* pMO = NULL;
	if (m_level == "mp")pMO = new MP();
	else pMO = new OBJ();

	*pMO = *this;

	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* p = m_childObj[i]->CopyMO();
		pMO->m_childObj.push_back(p);
	}

	return pMO;
}

OBJ& OBJ::operator=(OBJ& right)
{
	m_level = right.m_level;
	m_name = right.m_name;

	//if (right.m_moType == "mp" && m_moType == "mp")
	//{
	//	MP* pl = (MP*)this;
	//	MP* pr = (MP*)&right;
	//	*pl = *pr;
	//}
	return *this;
}

string OBJ::GetStatusSummary()
{
	string str;
	str += getTag().c_str(); str += "\r\n";
	GetAllChildAlarmInfo(str);


	str = str.substr(0,str.length() - 2);//除掉最后的回车换行
	return str;
}


void OBJ::GetAllChildAlarmInfo(string& strSummary)
{
	for (int i = 0; i < m_childObj.size(); i++)
	{
		OBJ* pChild = m_childObj.at(i);
		pChild->GetAllChildAlarmInfo(strSummary);
	}
}

bool OBJ::getTagsByTagSelector(TAG_SELECTOR& tagSelector, SELECT_RLT& rlt)
{
	//精确匹配直接返回
	for (int i = 0; i < tagSelector.exactMatchExp.size(); i++) {
		string& exp = tagSelector.exactMatchExp[i];
		vector<OBJ*> tagSet;
		prj.queryObj(&tagSet, exp, tagSelector.selLanguage, tagSelector.type, tagSelector.level);
		for (auto& i : tagSet)
		{
			rlt.dbFileTagSet.push_back(i->getTag("", tagSelector.rltLanguage));
			rlt.tagSet.push_back(i->getTag("", tagSelector.selLanguage));
		}
	}


	for (int i = 0; i < tagSelector.fuzzyMatchExp.size(); i++) {
		string& exp = tagSelector.fuzzyMatchExp[i];
		vector<OBJ*> tagSet;
		prj.queryObj(&tagSet, exp, tagSelector.selLanguage, tagSelector.type, tagSelector.level);
		for (auto& i : tagSet)
		{
			rlt.dbFileTagSet.push_back(i->getTag("", tagSelector.rltLanguage));
			rlt.tagSet.push_back(i->getTag("", tagSelector.selLanguage));
		}
	}
	return true;
}

void OBJ::getObjByTagSelector(vector<OBJ*>& objList, TAG_SELECTOR& tagSelector) {
	for (int i = 0; i < tagSelector.exactMatchExp.size(); i++) {
		string& exp = tagSelector.exactMatchExp[i];

		OBJ* p = prj.queryObj(exp, tagSelector.selLanguage);
		if (p) {
			if (!p->isSelectedByIOType(tagSelector.ioType)) {
				continue;
			}

			objList.push_back(p);
		}
	}

	for (int i = 0; i < tagSelector.fuzzyMatchExp.size(); i++) {
		string& exp = tagSelector.fuzzyMatchExp[i];

		vector<OBJ*> tagSet;
		prj.queryObj(&tagSet, exp, tagSelector.selLanguage, tagSelector.type, tagSelector.level);

		for (auto& i : tagSet) {
			if (!i->isSelectedByIOType(tagSelector.ioType)) {
				continue;
			}
			objList.push_back(i);
		}
	}
}

OBJ* OBJ::getObjByID(string id)
{
	if (m_objID == id)
		return this;

	for (int i = 0; i < m_childObj.size(); i++) {
		OBJ* pC = m_childObj[i];
		OBJ* pFind = pC->getObjByID(id);
		if (pFind) {
			return pFind;
		}
	}
	return nullptr;
}

void OBJ::getMpByTagSelector(vector<MP*>& mpList, TAG_SELECTOR& tagSelector)
{
	for (int i = 0; i < tagSelector.exactMatchExp.size(); i++) {
		string& exp = tagSelector.exactMatchExp[i];
		MP* p = GetMPByTag(exp,tagSelector.selLanguage);
		if (p) {
			mpList.push_back(p);
		}
	}

	for (int i = 0; i < tagSelector.fuzzyMatchExp.size(); i++) {
		string& exp = tagSelector.fuzzyMatchExp[i];
		vector<MP*> tagSet;
		vector<MP*> tagSetTmp;
		prj.GetMPByTag(&tagSetTmp, exp, tagSelector.selLanguage);
		if (tagSelector.specifyType())//has type filter //load from database 监测点类型过滤
		{
			for (auto& it : tagSetTmp)
			{
				if (it->getMpType() == tagSelector.type)
				{
					tagSet.push_back(it);
				}
			}
		}
		else
		{
			tagSet = tagSetTmp;
		}
		for (auto& i : tagSet)
		{
			mpList.push_back(i);
		}
	}
}

TIME SCHEDULE_TASK::getExeTime()
{
  TIME exeTime = timeopt::now();
  exeTime.setHMS(time);
  return exeTime;
}

bool SCHEDULE_TASK::isInterval()
{
  int intervalSeconds = time.wHour * 3600 + time.wMinute * 60 + time.wSecond;
  int pass = timeopt::CalcTimePassSecond(lastExecuteTime);
  return pass >= intervalSeconds;
}

void SCHEDULE_TASK::updateExecuteTime()
{
	lastExecuteTime.setNow();
}
