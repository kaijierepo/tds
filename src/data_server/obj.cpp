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
#include "as.h"
#include <algorithm>
#include "userMng.h"

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
	m_bEnableAlarm = true;
	m_bEnableIO = true;
}

OBJ::~OBJ()
{
	for (auto& i : m_childObj) {
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

bool OBJ::loadConf(json& conf, bool bCreate)
{
	//载入配置
	if (conf.contains("name")) {
		m_name = conf["name"];
		m_name = str::trim(m_name, " "); //界面在编辑时，非常容易不小心输入空格。并且不容易发现
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
	if (conf.contains("map")) {
		m_mapConf.merge_patch(conf["map"]);
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

	if (conf.contains("enableAlarm"))
	{
		m_bEnableAlarm = conf["enableAlarm"].get<bool>();
	}

	if (conf.contains("enableIO"))
	{
		m_bEnableIO = conf["enableIO"].get<bool>();
	}

	if (conf.contains("children")) {
		auto children = conf["children"];
		for (auto& child : children)
		{
			if (bCreate) {
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
			else {
				if (child.contains("name")) {
					string childName = child["name"];
					OBJ* pmo = GetChildObjByName(childName);
					if(pmo)
						pmo->loadConf(child, bCreate);
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

void OBJ::recursiveSetOffline()
{
	m_bOnline = false;
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

bool OBJ::toJson(json& conf, OBJ_QUERIER q, bool* parentSelectedByLeafType, const string& user)
{
	//先进行权限判断
	if (user != "admin" && user != "") //内部脚本调用时，user == ""
	{
		string sTag = getTag();
		if (!userMng.checkTagPermission(user, sTag))
			return false;
	}

	if (q.leafLevel != "" && !isSelectedByLeafLevel(q.leafLevel))
		return false;

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
		if (m_level != MO_TYPE::mp)
		{
			json jChildren = json::array();
			if (!q.flatten) {
				for (auto& pmochild : m_childObj)
				{
					//可以出现 getMp=true ,getChild=false的组合，因此getChild不代表getMp，虽然Mp也是child
					if (pmochild->m_level == "mp") {
						if (!q.getMp) continue;
					}
					else {
						if (!q.getChild) continue;
					}

					json jChild;
					if (pmochild->toJson(jChild, q, &selectedByLeafType, user))
						jChildren.push_back(jChild);
				}
			}
			else {
				vector<MP*> mpList;
				GetAllChildMp(mpList);
				for (auto& mp : mpList) {
					json jMp;
					if (mp->toJson(jMp, q, nullptr,user)) {
						string flattenName = mp->getTag();
						flattenName = TAG::trimRoot(flattenName, getTag());
						jMp["name"] = flattenName;
						jChildren.push_back(jMp);
					}
				}
			}

			if (jChildren.size() > 0)
				conf["children"] = jChildren;
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
	

	conf["name"] = m_name;
	conf["level"] = m_level;

	if(m_bEnableAlarm == false)
		conf["enableAlarm"] = m_bEnableAlarm;

	if (m_bEnableIO == false)
		conf["enableIO"] = m_bEnableIO;

	if (q.getConfDetail) {
		string tag = getTag();
		if (q.rootTag !=  "")
		{
			tag = TAG::trimRoot(tag, q.rootTag);
			conf["rootTag"] = q.rootTag;
		}
		if(tag!="")
			conf["tag"] = tag; //tag = "" 表示根节点。 tds中约定这样表示
		if(m_strIoAddrBind!="")
			conf["ioAddrBind"] = m_strIoAddrBind;
	}


	if (q.getConf) {
		if (m_bChildTds) {
			conf["childTds"] = true;
			conf["streamAccess"] = m_streamAccess;
		}
			
		if (m_type != "")
			conf["type"] = m_type;
		if (m_groupName != "") {
			conf["group"] = m_groupName;
		}
		if (m_bDynLocation)
		{
			conf["dynamicLocation"] = m_bDynLocation;
		}
		if (m_bLocationCalib)
		{
			conf["locationCalib"] = m_bLocationCalib;
		}
		if (m_dbLongitudeCalib > 0.000001)
			conf["longitudeCalib"] = m_dbLongitudeCalib;
		if (m_dbLatitudeCalib > 0.000001)
			conf["latitudeCalib"] = m_dbLatitudeCalib;
		if (m_mapConf != nullptr)
			conf["map"] = m_mapConf;
		if (m_longitude != nullptr)
			conf["longitude"] = m_longitude;
		if (m_latitude != nullptr)
			conf["latitude"] = m_latitude;
		if (m_comment != "") {
			conf["comment"] = m_comment;
		}
		if (m_alias != "") {
			conf["alias"] = m_alias;
		}

		if (m_objID != "") {
			conf["objID"] = m_objID;
		}

		if (m_scheduleTasks.size() > 0) {
			json jTasks = json::array();
			json jT;
			for (int i = 0; i < m_scheduleTasks.size(); i++) {
				SCHEDULE_TASK& st = m_scheduleTasks[i];
				st.toJson(jT);
				jTasks.push_back(jT);
			}
			conf["tasks"] = jTasks;
		}

		if (m_customConf != nullptr) {
			conf["customConf"] = m_customConf;
		}
	}
	

	//运行时状态数据
	if (q.getStatus)
	{
		if (tds->conf->showObjOnline == true)
		{
			if (m_strIoAddrBind != "" || isCustomMo() || m_bChildTds)
			{
				conf["online"] = m_bOnline;
			}
		}

			
		if (m_longitudeDyn != nullptr)
			conf["longitudeDyn"] = m_longitudeDyn;
		if (m_latitudeDyn != nullptr)
			conf["latitudeDyn"] = m_latitudeDyn;

		if (m_jAlarmStatus != nullptr)
			conf["alarmStatus"] = m_jAlarmStatus;
	}

	if (q.getStatusDesc) {
		if (m_strIoAddrBind != "" || isCustomMo())
		{
			if (q.getStatusDesc) {
				conf["onlineDesc"] = m_bOnline?"在线":"离线";
			}
		}

	}
	
	return true;
}

bool OBJ::loadStatus(OBJ* pSrcRoot)
{
	string tag = getTag();
	OBJ* ptmp = pSrcRoot->queryObj(tag);
	if (ptmp) {
		m_bOnline = ptmp->m_bOnline;
		m_stDataLastUpdate = ptmp->m_stDataLastUpdate;
		m_longitudeDyn = ptmp->m_longitudeDyn;
		m_latitudeDyn = ptmp->m_latitudeDyn;
		m_status = ptmp->m_status;
		m_jAlarmStatus = ptmp->m_jAlarmStatus;
		m_strIoAddrBind = ptmp->m_strIoAddrBind;

		for (int i = 0; i < m_childObj.size(); i++)
		{
			OBJ* pC = m_childObj[i];
			pC->loadStatus(pSrcRoot);
		}
	}
	else
		return false;
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
bool OBJ::loadStatus(json& status)
{
	if (status.is_object()) {
		//载入状态
		if (status.contains("online"))
			m_bOnline = status["online"].get<bool>();
		if (status.contains("longitudeDyn"))
			m_longitudeDyn = status["longitudeDyn"];
		if (status.contains("latitudeDyn"))
			m_latitudeDyn = status["latitudeDyn"];
		if (status.contains("alarmStatus"))
			m_jAlarmStatus = status["alarmStatus"];

		json jChildren = status["children"];
		if (jChildren != nullptr) {
			for (auto& childStatus : jChildren) {
				string name = childStatus["name"];
				OBJ* pChildObj = GetChildObjByName(name);
				if (pChildObj) {
					pChildObj->loadStatus(childStatus);
				}
			}
		}
	}
	else if (status.is_array()) {
		for (auto& i : status) {
			string tag = i["tag"].get<string>();
			//MP* pmp = 
		}
	}

	return false;
}

bool OBJ::saveStatus(json& statusNode)
{
	statusNode["name"] = m_name;
	statusNode["level"] = m_level;
	if (m_level == "mp") {
		MP* pmp = (MP*)this;
		statusNode["val"] = pmp->m_curVal;
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
	//yyjson_mut_val* key = yyjson_mut_strcpy(doc, "name");
	//yyjson_mut_obj_put(statusNode, key, yyjson_mut_strcpy(doc, m_name.c_str()));
	//if (m_level == "mp") {
	//	yyjson_mut_val* key = yyjson_mut_strcpy(doc, "val");
	//}

	//if (m_childObj.size() > 0) {
	//	json jChildren = json::array();
	//	for (int i = 0; i < m_childObj.size(); i++) {
	//		OBJ* p = m_childObj[i];
	//	}
	//}
	return false;
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
void OBJ::treeStatus2ListStatus(json& tree, json& list, string parentTag)
{
	string name = tree["name"];
	string tag = TAG::addRoot(name, parentTag);
	if (tree["level"] == "mp") {
		json jDe;
		jDe["tag"] = tag;
		jDe["time"] = tree["time"];
		jDe["val"] = tree["val"];
		list.push_back(jDe);
	}

	if (tree["children"].is_array()) {
		json& jChildren = tree["children"];
		for (int i = 0; i < jChildren.size(); i++) {
			json& jC = jChildren[i];
			treeStatus2ListStatus(jC, list, tag);
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

string OBJ::getTag(string root)
{
	if (m_pParentMO == nullptr)
		return "";

	OBJ* pTmpParent = m_pParentMO;
	string strTagName = m_name;

	while (pTmpParent && pTmpParent->m_pParentMO)//第一级位号工程名称默认不显示
	{
		strTagName = pTmpParent->m_name + "." + strTagName;
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

void OBJ::queryObj(std::vector<OBJ*>* tagVec, string strTag, bool usePinyin,string type, string level)
{
	if (strTag.find("*") == string::npos)//精确查找
	{
		//如果这是一个TDS子服务对象，m_rootTag不为空。
		//使用父服务的位号，在子服务中查询对象，需要先除去rootTag
		if (m_rootTag != "")
			strTag = TAG::trimRoot(strTag, m_rootTag);

		//判断自身位号是否是搜索位号的上级位号，如果不是一定搜索不到
		string tagThis = getTag();
		if (strTag.find(tagThis) == string::npos) {
			return;
		}
		strTag = TAG::trimRoot(strTag, tagThis);
		if (strTag == "")
		{
			tagVec->push_back(this);
		}


		//根据相对位号的名字节点，查找子对象的名字，获取到对象
		vector<string> vecNames;
		str::split(vecNames, strTag, ".");

		OBJ* toQuery = NULL;
		std::vector<OBJ*>* childMO = &m_childObj;
		bool findMO = false;
		for (int i = 0; i < vecNames.size(); i++)
		{
			string name = vecNames[i];
			bool findNode = false;
			for (int j = 0; j < childMO->size(); j++)
			{
				OBJ* tmp = childMO->at(j);
				string tmpName = tmp->m_name;
				if (usePinyin) {
					str::hanZi2Pinyin(tmpName, tmpName);
				}
				if (tmpName == name)
				{
					toQuery = tmp;
					findNode = true;
					if (i == vecNames.size() - 1)
					{
						findMO = true;
					}
					break;
				}
			}

			if (findNode)
			{
				childMO = &toQuery->m_childObj;
			}
			else
			{
				break;
			}
		}

		if (findMO) {
			if (toQuery->isSelectedByLevel(level) && toQuery->isSelectedByType(type)) {
				tagVec->push_back(toQuery);
			}
		}
	}
	else //通配符匹配
	{
		if (isSelectedByLevel(level) && isSelectedByType(type)) {
			string tagCandidate = getTag();
			TAG_SELECTOR ts;
			ts.init(strTag);
			if (ts.match(tagCandidate))
				tagVec->push_back(this);
		}

		for (int i = 0; i < m_childObj.size(); i++)
		{
			OBJ* pMOChild = m_childObj.at(i);
			pMOChild->queryObj(tagVec, strTag,usePinyin,type, level);
		}
	}
}

void OBJ::GetMPByTag(std::vector<MP*>* tagVec, string strTag)
{
	std::vector<OBJ*> vec;
	queryObj(&vec, strTag);
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

OBJ* OBJ::queryObj(string strTag,bool usePinyin)
{
	if (strTag == "")
		return this;

	vector<OBJ*> tags;
	queryObj(&tags, strTag,usePinyin);
	if (tags.size() > 0)
		return tags[0];
	else
		return NULL;
}

MP* OBJ::GetMPByTag(string strTag, bool usePinyin)
{
	OBJ* pMO = queryObj(strTag, usePinyin);
	if (pMO && pMO->m_level == "mp")
		return (MP*)pMO;
	return nullptr;
}

MP* OBJ::GetMPByTagPinyin(string strTag)
{
	return GetMPByTag(strTag, true);
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


bool OBJ::isSelectedByLevel(string level)
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

bool OBJ::isSelectedByType(string type)
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
			if (j["level"] == level) {
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
			if (pmp->m_curVal.is_string()) {
				string sCur = pmp->m_curVal.get<string>();
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
			if (m_jAlarmStatus != nullptr) {
				os.alarm++;
			}
			else {
				os.normal++;
			}
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
	if (opt["getValDesc"].is_boolean()) {
		q.getValDesc = opt["getValDesc"].get<bool>();
	}
	if (opt["flatten"].is_boolean()) {
		q.flatten = opt["flatten"].get<bool>();
	}
	if (opt["dataSaveMp"].is_boolean()) {
		q.dataSaveMp = opt["dataSaveMp"].get<bool>();
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

bool OBJ::getTagsByTagSelector(vector<string>& tags, TAG_SELECTOR& tagSelector)
{
	//精确匹配直接返回
	for (int i = 0; i < tagSelector.exactMatchExp.size(); i++) {
		string& exp = tagSelector.exactMatchExp[i];
		tags.push_back(exp);
	}


	for (int i = 0; i < tagSelector.fuzzyMatchExp.size(); i++) {
		string& exp = tagSelector.fuzzyMatchExp[i];
		vector<OBJ*> tagSet;
		prj.queryObj(&tagSet, exp, false, tagSelector.type, tagSelector.level);
		for (auto& i : tagSet)
		{
			tags.push_back(i->getTag());
		}
	}
	return true;
}

void OBJ::getObjByTagSelector(vector<OBJ*>& objList, TAG_SELECTOR& tagSelector) {
	for (int i = 0; i < tagSelector.exactMatchExp.size(); i++) {
		string& exp = tagSelector.exactMatchExp[i];
		OBJ* p = prj.queryObj(exp);
		if (p) {
			objList.push_back(p);
		}
	}

	for (int i = 0; i < tagSelector.fuzzyMatchExp.size(); i++) {
		string& exp = tagSelector.fuzzyMatchExp[i];
		vector<OBJ*> tagSet;
		prj.queryObj(&tagSet, exp, false, tagSelector.type,tagSelector.level);
		for (auto& i : tagSet)
		{
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
		MP* p = GetMPByTag(exp);
		if (p) {
			mpList.push_back(p);
		}
	}

	for (int i = 0; i < tagSelector.fuzzyMatchExp.size(); i++) {
		string& exp = tagSelector.fuzzyMatchExp[i];
		vector<MP*> tagSet;
		vector<MP*> tagSetTmp;
		prj.GetMPByTag(&tagSetTmp, exp);
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
	if (cyclic) {
		TIME exeTime = timeopt::now();
		exeTime.setHMS(time);
		return exeTime;
	}
	else {
		TIME exeTime;
		exeTime.setDate(dateStart);
		exeTime.setHMS(time);
		return exeTime;
	}
}
