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
#include "amo.h"
#include "ioDev.h"
#include "as.h"
#include <algorithm>


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
	else if (type == MO_TYPE::amo)
	{
		p = new amo();
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
	m_type = MO_TYPE::mo;
	m_bOnline = false;
	m_bShow = true;
	m_bDynLocation = false;
	m_dbLongitudeCalib = 0;
	m_dbLatitudeCalib = 0;
	m_bLocationCalib = false;
	m_bChildTds = false;
}

OBJ::~OBJ()
{

}

bool OBJ::loadConf(json& conf)
{
	//载入配置
	if (conf.contains("name")) {
		m_name = conf["name"];
	}
	if (conf.contains("parentTag")) {
		m_parentTag = conf["parentTag"];
	}
	if (conf.contains("type")) {
		m_type = conf["type"];
	}
	if (conf.contains("childTds")) {
		m_bChildTds = conf["childTds"].get<bool>();
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
	if(conf.contains("map"))
		m_mapConf = conf["map"];

	if (m_type == "customMo" && conf.contains("customTypeLabel") && conf["customTypeLabel"].get<string>().length() > 0)
	{
		m_customTypeLabel = conf["customTypeLabel"];//以中文配置为准，转拼音主要为方便内部不支持中文的地方使用。每一次修改了label都要更新type，通过转拼音
		m_customType = m_customTypeLabel;
		str::hanZi2Pinyin(m_customType, m_customType);
		
		project* pPrj = (project*)GetRootMO();

		if (pPrj->m_mapCustomMOType.find(m_customType) != pPrj->m_mapCustomMOType.end())
		{
			vector<OBJ*>& moList = pPrj->m_mapCustomMOType[m_customType];
			moList.push_back(this);
		}
		else
		{
			vector<OBJ*> moList;
			moList.push_back(this);
			pPrj->m_mapCustomMOType[m_customType] = moList;
		}
	}

	if (m_type == "customOrg" && conf.contains("customTypeLabel") && conf["customTypeLabel"].get<string>().length() > 0)
	{
		m_customTypeLabel = conf["customTypeLabel"];//以中文配置为准，转拼音主要为方便内部不支持中文的地方使用。每一次修改了label都要更新type，通过转拼音
		m_customType = m_customTypeLabel;
		str::hanZi2Pinyin(m_customType, m_customType);
	}

	if (conf.contains("ioAddrBind"))
		m_strIoAddrBind = conf["ioAddrBind"];


	//载入状态
	if (conf.contains("lastModify")) {
		m_strLastModify = conf["lastModify"];
	}
	if(conf.contains("online"))
		m_bOnline = conf["online"].get<bool>();
	if (conf.contains("longitudeDyn"))
		m_longitudeDyn = conf["longitudeDyn"];
	if (conf.contains("latitudeDyn"))
		m_latitudeDyn = conf["latitudeDyn"];
	if (conf.contains("alarmStatus"))
		m_jAlarmStatus = conf["alarmStatus"];

	
	if (conf.contains("children")) {
		auto children = conf["children"];
		for (auto& child : children)
		{
			OBJ* pmo;
			if (child.contains("type") && child["type"] == "mp") {
				pmo = new MP();
			}
			else
				pmo = new OBJ();

			if (pmo)			
			{
				pmo->m_pParentMO = this; //放在loadConf之前，loadConf中会使用到m_pParentMO
				pmo->loadConf(child);
				m_childMO.push_back(pmo);
			}
		}
	}

	return true;
}



//根据leafType选择器，该节点是否要返回
bool OBJ::isSelectedByLeafType(string leafType)
{
	if (leafType == "")
		return true;
	if (m_type == "mp")
		return true;
	if (m_type == "project") {
		return true;
	}
	else if (m_type == "org")
	{
		return true;
	}
	else if (m_type == "mo") {
		if(leafType == "mo")
			return true;
	}
	else if (m_type == MO_TYPE::customOrg) {
		if (leafType == "org" ||leafType == "mo") {
			return true;
		}
		else if (leafType == m_customType || leafType == m_customTypeLabel)
			return true;
	}
	else if (m_type == MO_TYPE::customMo) {
		if (leafType == "mo") {
			return true;
		}
		else if(leafType == m_customType || leafType == m_customTypeLabel)
			return true;
	}
	
	return false;
}

//serializeOption
//root 返回位号的相对根
//type mo类型
//getChild 是否递归
//getStatus 是否包含状态信息
//getMp 是否获取mp。缺省获取

bool OBJ::toJson(json& conf, json serializeOption)
{
	OBJ_QUERIER q = parseQuerier(serializeOption);
	return toJson(conf, q);
}

bool OBJ::toJson(json& conf, OBJ_QUERIER q)
{
	if (m_type == "mp" && !q.getMp)
		return false;

	//根据请求的moType判断是否需要返回当前节点。
	if (!isSelectedByLeafType(q.leafType))
		return false;

	conf["name"] = m_name;
	conf["type"] = m_type;

	if (q.getConfDetail) {
		string tag = getTag();
		if (q.rootTag !=  "")
		{
			tag = TAG::trimRoot(tag, q.rootTag);
		}
		if(tag!="")
			conf["tag"] = tag; //tag = "" 表示根节点。 tds中约定这样表示
		if(m_strIoAddrBind!="")
			conf["ioAddrBind"] = m_strIoAddrBind;
	}


	if (q.getConf) {
		if (m_bChildTds)
			conf["childTds"] = true;
		if (m_parentTag != "")
			conf["parentTag"] = m_parentTag;
		if (m_customType != "")
			conf["customType"] = m_customType;
		if (m_customTypeLabel != "")
			conf["customTypeLabel"] = m_customTypeLabel;
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
		if (m_strLastModify != "") {
			conf["lastModify"] = m_strLastModify;
		}
	}
	

	//运行时状态数据
	if (q.getStatus)
	{
		if (m_strIoAddrBind != "" || m_type == MO_TYPE::customMo)
		{
			conf["online"] = m_bOnline;
		}
			
		if (m_longitudeDyn != nullptr)
			conf["longitudeDyn"] = m_longitudeDyn;
		if (m_latitudeDyn != nullptr)
			conf["latitudeDyn"] = m_latitudeDyn;

		if (m_jAlarmStatus != nullptr)
			conf["alarmStatus"] = m_jAlarmStatus;
	}

	if (q.getStatusDesc) {
		if (m_strIoAddrBind != "" || m_type == MO_TYPE::customMo)
		{
			if (q.getStatusDesc) {
				conf["onlineDesc"] = m_bOnline?"在线":"离线";
			}
		}

	}


	//是否需要递归序列化子对象
	if (!q.getChild)
	{
		return true;
	}

	if (m_type != MO_TYPE::mp)
	{
		json jChildren = json::array();
		for (auto& pmochild : m_childMO)
		{
			if (pmochild->m_type == "mp" && !q.getMp)
				continue;

			json jChild;
			if (pmochild->toJson(jChild, q))
				jChildren.push_back(jChild);
		}
		if(jChildren.size() > 0)
			conf["children"] = jChildren;
	}
	
	return true;
}

bool OBJ::loadStatus(OBJ* pSrc,bool saveToDB)
{
	string tag = getTag();
	OBJ* ptmp = pSrc->queryObj(tag);
	if (ptmp) {
		m_bOnline = pSrc->m_bOnline;
		m_stDataLastUpdate = pSrc->m_stDataLastUpdate;
		m_longitudeDyn = pSrc->m_longitudeDyn;
		m_latitudeDyn = pSrc->m_latitudeDyn;
		m_status = pSrc->m_status;
		m_jAlarmStatus = pSrc->m_jAlarmStatus;
		m_strIoAddrBind = pSrc->m_strIoAddrBind;

		for (int i = 0; i < m_childMO.size(); i++)
		{
			OBJ* pC = m_childMO[i];
			pC->loadStatus(pSrc,saveToDB);
		}
	}
	else
		return false;
}

bool OBJ::loadStatus(json& jMpList)
{
	for (auto& i : jMpList) {
		string tag = i["tag"].get<string>();
		//MP* pmp = 
	}
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
	for (auto& i : m_childMO)
	{
		delete i;
	}
	m_childMO.clear();
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
		pChild = pParent->GetMOByName(nodes[i]);
		if (!pChild) {
			pChild = new OBJ();
			pChild->m_name = nodes[i];
			pParent->m_childMO.push_back(pChild);
			pChild->m_pParentMO = pParent;
		}
		pParent = pChild;
	}
	return pChild;
}
json OBJ::getRT()
{
	json j;
	j["name"] = m_name;
	j["type"] = m_type;
	json jChildren;
	for(int i=0;i<m_childMO.size();i++)
	{
		OBJ* pmo = m_childMO.at(i);
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

void OBJ::queryObj(std::vector<OBJ*>* tagVec, string strTag,string type)
{
	if (strTag.find("*") == string::npos)//精确查找
	{
		//如果这是一个TDS子服务对象，m_rootTag不为空。
		//使用父服务的位号，在子服务中查询对象，需要先除去rootTag
		if (m_rootTag != "")
			strTag = TAG::trimRoot(strTag, m_rootTag);
		if (strTag == "")
		{
			tagVec->push_back(this);
		}

		vector<string> vecNames;
		str::split(vecNames, strTag, ".");

		OBJ* toQuery = NULL;
		std::vector<OBJ*>* childMO = &m_childMO;
		bool findMO = false;
		for (int i = 0; i < vecNames.size(); i++)
		{
			string name = vecNames[i];
			bool findNode = false;
			for (int j = 0; j < childMO->size(); j++)
			{
				OBJ* tmp = childMO->at(j);
				if (tmp->m_name == name)
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
				childMO = &toQuery->m_childMO;
			}
			else
			{
				break;
			}
		}

		if (findMO) {
			if (toQuery->isSelectedByType(type)) {
				tagVec->push_back(toQuery);
			}
		}
	}
	else //通配符匹配
	{
		if (isSelectedByType(type)) {
			string tagCandidate = getTag();
			TAG_SELECTOR ts;
			ts.init(strTag);
			if (ts.match(tagCandidate))
				tagVec->push_back(this);
		}

		for (int i = 0; i < m_childMO.size(); i++)
		{
			OBJ* pMOChild = m_childMO.at(i);
			pMOChild->queryObj(tagVec, strTag,type);
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
		if (p->m_type == "mp")
		{
			tagVec->push_back((MP*)p);
		}
	}
}

OBJ* OBJ::queryObj(string strTag)
{
	if (strTag == "")
		return this;

	vector<OBJ*> tags;
	queryObj(&tags, strTag);
	if (tags.size() > 0)
		return tags[0];
	else
		return NULL;
}

MP* OBJ::GetMPByTag(string strTag)
{
	OBJ* pMO = queryObj(strTag);
	if (pMO && pMO->m_type == "mp")
		return (MP*)pMO;
	return nullptr;
}

MP* OBJ::GetMPByName(string strName)
{
	OBJ* p = GetMOByName(strName);
	if (p && p->m_type == "mp")
	{
		return (MP*)p;
	}
	return NULL;
}

OBJ* OBJ::GetMOByName(string strName)
{
	if (m_name == strName)
		return this;
	else
	{
		for (int i = 0; i < m_childMO.size(); i++)
		{
			OBJ* pMOChild = m_childMO.at(i);
			OBJ* pFind = pMOChild->GetMOByName(strName);
			if (pFind)
				return pFind;
		}
	}

	return NULL;
}

bool OBJ::isSelectedByType(string type)
{
	if (type == "")
		return true;
	if (type == "obj")
		return true;
	else if (type == "org") {
		if (m_type == "org" || m_type == "customOrg")
			return true;
		else
			return false;
	}							
	else if (type == "mo") {
		if (m_type == "mo" || m_type == "customMo")
			return true;
		else
			return false;
	}
	else if (type == "mp") {
		if (m_type == "mp")
			return true;
		else
			return false;
	}
	else if(type == "customMo"){
		if (m_type == "customMo")
			return true;
		else
			return false;
	}
	else if (type == "customOrg")
	{
		if (m_type == "customOrg")
			return true;
		else
			return false;
	}
	else {
		if (m_customType == type || m_customTypeLabel == type)
			return true;
		else
			return false;
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

string OBJ::AppendTagRoot(string& str)
{
	return "";
}



string OBJ::ResolveTag(string strTagExp, string strTagThis)
{
	string tagName = strTagExp;
	//this的解析，this后面可能带 .std 等后缀
	if (strTagExp.find("this") != string::npos)
	{
		tagName = str::replace(tagName, "this", strTagThis);
	}
	//解析仅名字的情况，等效于 ./XXX（使用当前监测点的父监测对象组成完整名字）
	else if (strTagExp.find(".") == string::npos && strTagExp.find("*") == string::npos && strTagThis != "")
	{
		string strTagContext; //父监测对象的tag
		int iPos = strTagThis.rfind('.');
		if (iPos <= 0)return "";
		strTagContext = strTagThis.substr(0, iPos);
		tagName = strTagContext + "." + strTagExp;
	}
	//使用相对位号的格式 ./或者../ ,./表示环境位号（父mo的位号）,../表示环境位号向上一级
	else if (strTagExp.find("./") != string::npos || strTagExp.find(".\\") != string::npos || strTagExp.find("..") != string::npos)
	{
		//替换../   ../必须也只能写前边
		string strTagContext; //父监测对象的tag
		int iPos = strTagThis.rfind('.');
		if (iPos <= 0)return "";
		strTagContext = strTagThis.substr(0, iPos);
		string tag = strTagContext;
		string rtag = strTagExp;
		//先规范化 替换\为/  替换\\为/  
		rtag = str::replace(rtag, "\\", "/");
		rtag = str::replace(rtag, "\\\\", "/");

		while (1) {
			int ipos = rtag.find("../");
			if (ipos != string::npos) {
				int dotPos = tag.rfind(".");
				if (dotPos != string::npos) {
					tag = tag.substr(0, dotPos);
				}
				else {
					return "";
				}
				rtag = rtag.substr(ipos + 3);
			}
			else {
				break;
			}
		}
		//替换./
		rtag = str::replace(rtag, "./", "");

		if (rtag.length() > 0)
			tag = tag + "." + rtag;
		tagName = tag;
	}
	//位号全名
	else
	{
		if (strTagExp.find(".") == string::npos)//仅指定name
		{
			OBJ* p = prj.queryObj("*" + strTagExp);
			if (p)
				tagName = p->getTag();
		}
		else
			tagName = strTagExp;
	}

	//remove root   tagName without root name is a convention
	if(tagName.find(prj.m_name + ".") == 0)
	{
		tagName = tagName.substr(prj.m_name.length()+1,tagName.length()-prj.m_name.length()-1);
	}
	return tagName;
}

string OBJ::trimProperty(string& strTagExp)
{
	string strTagProperty;
	if (strTagExp.substr(strTagExp.length() - 4, 4) == ".std")
	{
		strTagExp = strTagExp.substr(0, strTagExp.length() - 4);
		strTagProperty = ".std";
	}
	return strTagProperty;
}


//该函数的参数必须是相对位号格式
string OBJ::TranslateRelateTag(string rtag)
{
	string tag = getTag();

	//替换../
	while (1)
	{
		int ipos = rtag.find("../");
		if (ipos != string::npos)
		{
			int dotPos = tag.rfind(".");
			if (dotPos != string::npos)
			{
				tag = tag.substr(0, dotPos);
			}
			else
			{
				return "";
			}
			rtag = rtag.substr(ipos + 3);
		}
		else
		{
			break;
		}
	}

	//替换./
	rtag = str::replace(rtag, "./", "");
	if (rtag.find("?") != string::npos) {
		int pos1 = tag.find("#");
		int pos2 = -1;
		string strDC = "";//dao cha
		if (pos1 >= 0) {
			pos2 = tag.rfind(".", pos1);
			if (pos2 >= 0 && pos1 - pos2 - 1 >= 0) {
				strDC = tag.substr(pos2 + 1, pos1 - pos2 - 1);
				rtag = str::replace(rtag, "?", strDC.c_str());
			}

		}
	}
	if (rtag.length() > 0)
		tag = tag + "." + rtag;

	return tag;
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
		pParent->m_childMO.push_back(pmo);
		pParent = pmo;
	}

	return pmo;
}

void OBJ::GetAllChildMO(std::vector<OBJ*>& aryMO, string type)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		if (m_childMO[i]->m_type == type)
		{
aryMO.push_back(m_childMO[i]);
		}
		m_childMO[i]->GetAllChildMO(aryMO, type);
	}
}


map<string, json> OBJ::getChildCustomMoTypeList()
{
	if (m_childCustomMoTypeList.size() > 0)
		return m_childCustomMoTypeList;

	statisChildCustomMoType(m_childCustomMoTypeList);
	return m_childCustomMoTypeList;
}

void OBJ::statisChildCustomMoType(map<string, json>& list)
{
	for (auto& i : m_childMO)
	{
		if (i->m_type == MO_TYPE::customMo)
		{
			json jType;
			jType["type"] = i->m_customType;
			jType["label"] = i->m_customTypeLabel;
			list[i->m_customType] = jType;
		}

		i->statisChildCustomMoType(list);
	}
}

void OBJ::statisChildMo(json& jStatis)
{
	if (jStatis["project"] == nullptr) {
		jStatis["project"] = 0;
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



	if (m_type == MO_TYPE::customOrg) {
		jStatis["project"] = jStatis["project"].get<int>() + 1;
	}
	else if (m_type == MO_TYPE::customMo) {
		jStatis["smartDev"] = jStatis["smartDev"].get<int>() + 1;

		if (m_bOnline)
			jStatis["online"] = jStatis["online"].get<int>() + 1;
		else
			jStatis["offline"] = jStatis["offline"].get<int>() + 1;
	}

	for (int i = 0; i < m_childMO.size(); i++)
	{
		OBJ* pC = m_childMO[i];
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
	if (opt["getDetailConf"] != nullptr) {
		q.getConfDetail = opt["getDetailConf"].get<bool>();
	}
	if (opt["rootTag"] != nullptr){
		q.rootTag = opt["rootTag"].get<string>();
	}
	if (opt["getStatusDesc"] != nullptr) {
		q.getStatusDesc = opt["getStatusDesc"].get<bool>();
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
		if (p->m_type == type)
		{
			return p;
		}

		p = p->m_pParentMO;
	}

	return NULL;
}

OBJ* OBJ::GetChildMO(string type)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		OBJ* pMO = m_childMO.at(i);
		if (pMO->m_type == type)
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
	if (m_type == "mp")pMO = new MP();
	else pMO = new OBJ();

	*pMO = *this;

	for (int i = 0; i < m_childMO.size(); i++)
	{
		OBJ* p = m_childMO[i]->CopyMO();
		pMO->m_childMO.push_back(p);
	}

	return pMO;
}

OBJ& OBJ::operator=(OBJ& right)
{
	m_type = right.m_type;
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
	for (int i = 0; i < m_childMO.size(); i++)
	{
		OBJ* pChild = m_childMO.at(i);
		pChild->GetAllChildAlarmInfo(strSummary);
	}
}

string TAG::trimRoot(string& tag)
{
	string s = str::trim(tag, prj.m_name);
	s = str::trim(s,".");
	return s;
}

string TAG::trimRoot(string& tag, string root)
{
	tag = str::trimPrefix(tag, root);
	tag = str::trimPrefix(tag, ".");
	return tag;
}

string TAG::userTag2sysTag(string userTag, string userOrg)
{
	return TAG::addRoot(userTag, userOrg);
}

string TAG::sysTag2userTag(string sysTag, string userOrg)
{
	return TAG::trimRoot(sysTag, userOrg);
}

string TAG::addRoot(string& tag)
{
	if (tag.find(prj.m_name) == 0)
	{
		return tag;
	}
	else
	{
		if (tag != "")
			return prj.m_name + "." + tag;
		else
			return prj.m_name;
	}
}

string TAG::addRoot(string tag, string root)
{
	if (root == "")
		return tag;

	//tag是相对于root的相对位号
	if (tag == "")
		return root;

	return root + "." + tag;
}

bool TAG::hasTag(json& tree, string tag)
{
	vector<string> nodeNames;
	str::split(nodeNames, tag, ".");
	json* node = &tree;
	for (int i = 0; i < nodeNames.size(); i++)
	{
		string name = nodeNames[i];


		//查找子节点中有没有是 指定name的节点。如果有node指向该节点，继续查找node的子节点中是否有下一个name
		if ((*node)["children"] == nullptr)
			return false;
		json& jChildren = (*node)["children"];
		bool bHaveChild = false;
		if (jChildren.is_array())//具体指定
		{
			for (int j = 0; j < jChildren.size(); j++)
			{
				json& child = jChildren[j];
				if (child["name"].get<string>() == name)
				{
					bHaveChild = true;
					node = &child;
				}
			}
		}
		else if (jChildren.is_string() && jChildren.get<string>() == "*") //通配符指定，所有子节点
		{
			return true;
		}
		
		if (!bHaveChild)return false;
	}

	return true;
}

int TAG::getMoLevel(string tag)
{
	tag = TAG::addRoot(tag);
	return std::count(tag.begin(),tag.end(),'.');
}

json TAG::mapTree2List(json mapTree)
{
	for (auto& [k,v] : mapTree.items())
	{

	}

	return json();
}


