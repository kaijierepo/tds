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
#include "mo.h"
#include "common.hpp"
#include "prj.h"
#include "mp.h"
#include "amo.h"
#include "ioDev.h"
#include "as.h"
#include <algorithm>


MO* createMO(string type)
{
	MO* p = NULL;
	if (type == MO_TYPE::mo || type == MO_TYPE::customMo)
	{
		p = new MO();
	}
	else if (type == MO_TYPE::org)
	{
		p = new MO();
	}
	else if (type == MO_TYPE::customOrg)
	{
		p = new MO();
	}
	else if (type == MO_TYPE::mp)
	{
		p = new MP();
	}
	else if (type == MO_TYPE::mpgroup)
	{
		p = new MO();
	}
	else if (type == MO_TYPE::amo)
	{
		p = new amo();
	}

	return p;
}

MO::MO()
{
	m_pParentMO = NULL;
	m_moType = MO_TYPE::mo;
	m_bOnline = false;
	m_bShow = true;
	m_bDynLocation = false;
	m_dbLongitudeCalib = 0;
	m_dbLatitudeCalib = 0;
	m_bLocationCalib = false;
}

MO::~MO()
{

}

bool MO::loadConf(json& conf)
{
	if (conf.contains("name")) {
		m_strName = conf["name"];
	}
	
	if (conf.contains("type")) {
		m_moType = conf["type"];
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

	if (m_moType == "customMo" && conf.contains("customTypeLabel") && conf["customTypeLabel"].get<string>().length() > 0)
	{
		m_moCustomTypeLabel = conf["customTypeLabel"];//以中文配置为准，转拼音主要为方便内部不支持中文的地方使用。每一次修改了label都要更新type，通过转拼音
		m_moCustomType = m_moCustomTypeLabel;
		str::hanZi2Pinyin(m_moCustomType, m_moCustomType);
		
		if (prj.m_mapCustomMOType.find(m_moCustomType) != prj.m_mapCustomMOType.end())
		{
			vector<MO*>& moList = prj.m_mapCustomMOType[m_moCustomType];
			moList.push_back(this);
		}
		else
		{
			vector<MO*> moList;
			moList.push_back(this);
			prj.m_mapCustomMOType[m_moCustomType] = moList;
		}
	}

	if (m_moType == "customOrg" && conf.contains("customTypeLabel") && conf["customTypeLabel"].get<string>().length() > 0)
	{
		m_moCustomTypeLabel = conf["customTypeLabel"];//以中文配置为准，转拼音主要为方便内部不支持中文的地方使用。每一次修改了label都要更新type，通过转拼音
		m_moCustomType = m_moCustomTypeLabel;
		str::hanZi2Pinyin(m_moCustomType, m_moCustomType);
	}

	
	if (conf.contains("children")) {
		auto children = conf["children"];
		for (auto& child : children)
		{
			MO* pmo = createMO(child["type"]);
			if (pmo)
			{
				pmo->loadConf(child);
				m_childMO.push_back(pmo);
				pmo->m_pParentMO = this;
			}
		}
	}

	return true;
}

//根据leafType选择器，该节点是否要返回
bool MO::isSelectedByLeafType(string leafType)
{
	if (leafType == "")
		return true;
	if (m_moType == "mp")
		return true;
	else if (m_moType == "org")
	{
		return true;
	}
	else if (m_moType == "mo") {
		if(leafType == "mo")
			return true;
	}
	else if (m_moType == MO_TYPE::customOrg) {
		if (leafType == "org" ||leafType == "mo") {
			return true;
		}
		else if (leafType == m_moCustomType || leafType == m_moCustomTypeLabel)
			return true;
	}
	else if (m_moType == MO_TYPE::customMo) {
		if (leafType == "mo") {
			return true;
		}
		else if(leafType == m_moCustomType || leafType == m_moCustomTypeLabel)
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
bool MO::toJson(json& conf, json serializeOption)
{
	MO_QUERIER q = parseQuerier(serializeOption);

	if (m_moType == "mp" && !q.getMp)
		return false;

	//根据请求的moType判断是否需要返回当前节点。
	if (!isSelectedByLeafType(q.leafType))
		return false;

	if (q.getConfDetail) {
		string tag = getTag();
		if (serializeOption["rootTag"] != nullptr)
		{
			string rootTag = serializeOption["rootTag"].get<string>();
			tag = str::trimPrefix(tag, rootTag);
			tag = str::trimPrefix(tag, ".");
		}
		conf["tag"] = tag; //tag = "" 表示根节点。 tds中约定这样表示
		conf["ioAddrBind"] = m_strIoAddrBind;
	}


	if (q.getConf) {
		conf["name"] = m_strName;
		conf["type"] = m_moType;
		if (m_moCustomType != "")
			conf["customType"] = m_moCustomType;
		if (m_moCustomTypeLabel != "")
			conf["customTypeLabel"] = m_moCustomTypeLabel;
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
	}
	

	//运行时状态数据
	if (q.getStatus)
	{
		if (m_strIoAddrBind != "")
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


	//是否需要递归序列化子对象
	if (!q.getChild)
	{
		return true;
	}

	if (m_moType != MO_TYPE::mp)
	{
		json jChildren = json::array();
		for (auto& pmochild : m_childMO)
		{
			if (pmochild->m_moType == "mp" && !q.getMp)
				continue;

			json jChild;
			if (pmochild->toJson(jChild, serializeOption))
				jChildren.push_back(jChild);
		}
		if(jChildren.size() > 0)
			conf["children"] = jChildren;
	}
	
	return true;
}

bool MO::loadStatus(MO* pSrc)
{
	string tag = getTag();
	MO* ptmp = pSrc->GetMOByTag(tag);
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
			MO* pC = m_childMO[i];
			pC->loadStatus(pSrc);
		}
	}
	else
		return false;
}

void MO::removeMp(json& mo)
{
	if (mo["children"] != nullptr)
	{
		json jChildren = mo["children"];
	}
}

void MO::clearChildren()
{
	for (auto& i : m_childMO)
	{
		delete i;
	}
	m_childMO.clear();
}



MO* MO::GetProjectMO()
{
	MO* pTmp = this;
	while (pTmp->m_pParentMO)
	{
		pTmp = pTmp->m_pParentMO;
	}

	return pTmp;
}
json MO::getRT()
{
	json j;
	j["name"] = m_strName;
	j["type"] = m_moType;
	json jChildren;
	for(int i=0;i<m_childMO.size();i++)
	{
		MO* pmo = m_childMO.at(i);
		jChildren.push_back(pmo->getRT());
	}
	j["children"]=jChildren;
	return j;
}

string MO::getTag(string root)
{
	if (m_pParentMO == nullptr)
		return "";

	MO* pTmpParent = m_pParentMO;
	string strTagName = m_strName;

	while (pTmpParent && pTmpParent->m_pParentMO)//第一级位号工程名称默认不显示
	{
		strTagName = pTmpParent->m_strName + "." + strTagName;
		pTmpParent = pTmpParent->m_pParentMO;
	}

	if (root != "")
	{
		strTagName = str::trimPrefix(strTagName, root);
		strTagName = str::trimPrefix(strTagName, ".");
	}
		
	return strTagName;
}

vector<string> MO::GetAlias()
{
	vector<string> vecAlias;
	str::removeChar(m_alias, ' ');
	if (m_alias.length() == 0)
		return vecAlias;
	str::split(vecAlias, m_alias, ",");
	return vecAlias;
}

vector<string> MO::GetAllTagNamePlus()
{
	MO* pTmpParent = m_pParentMO;
	string tag = m_strName;

	//获得名字数组
	vector<string> vecTagName;
	vecTagName.push_back(tag); //原名放前面，别名放后面
	vector<string> vecTagAlias = GetAlias();
	vecTagName.insert(vecTagName.end(), vecTagAlias.begin(), vecTagAlias.end());


	while (pTmpParent)//包含工程节点名称的位号
	{
		vector<string> vecParentName;
		vecParentName.push_back(pTmpParent->m_strName); //原名放前面，别名放后面
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


string MO::getTagWithRoot()
{
	MO* pTmpParent = m_pParentMO;
	string strTagName = m_strName;
	while (pTmpParent)//包含工程节点名称的位号
	{
		strTagName = pTmpParent->m_strName + "." + strTagName;
		pTmpParent = pTmpParent->m_pParentMO;
	}
	return strTagName;
}

void MO::GetMOByTag(std::vector<MO*>* tagVec, string strTag)
{
	if (strTag.find("*") == string::npos)//精确查找
	{
		vector<string> vecNames;
		str::split(vecNames, strTag, ".");

		MO* toQuery = NULL;
		std::vector<MO*>* childMO = &m_childMO;
		bool findMO = false;
		for (int i = 0; i < vecNames.size(); i++)
		{
			string name = vecNames[i];
			bool findNode = false;
			for (int j = 0; j < childMO->size(); j++)
			{
				MO* tmp = childMO->at(j);
				if (tmp->m_strName == name)
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

		if (findMO)
			tagVec->push_back(toQuery);
	}
	else //通配符匹配
	{
		string tagCandidate = getTag();
		TAG_SELECTOR ts;
		ts.init(strTag);
		if (ts.match(tagCandidate))
			tagVec->push_back(this);

		for (int i = 0; i < m_childMO.size(); i++)
		{
			MO* pMOChild = m_childMO.at(i);
			pMOChild->GetMOByTag(tagVec, strTag);
		}
	}
}

void MO::GetMPByTag(std::vector<MP*>* tagVec, string strTag)
{
	std::vector<MO*> vec;
	GetMOByTag(&vec, strTag);
	for (int i = 0; i < vec.size(); i++)
	{
		MO* p = vec.at(i);
		if (p->m_moType == "mp")
		{
			tagVec->push_back((MP*)p);
		}
	}
}

MO* MO::GetMOByTag(string strTag)
{
	if (strTag == "")
		return this;

	vector<MO*> tags;
	GetMOByTag(&tags, strTag);
	if (tags.size() > 0)
		return tags[0];
	else
		return NULL;
}

MP* MO::GetMPByTag(string strTag)
{
	MO* pMO = GetMOByTag(strTag);
	if (pMO && pMO->m_moType == "mp")
		return (MP*)pMO;
	return nullptr;
}

MP* MO::GetMPByName(string strName)
{
	MO* p = GetMOByName(strName);
	if (p && p->m_moType == "mp")
	{
		return (MP*)p;
	}
	return NULL;
}

MO* MO::GetMOByName(string strName)
{
	if (m_strName == strName)
		return this;
	else
	{
		for (int i = 0; i < m_childMO.size(); i++)
		{
			MO* pMOChild = m_childMO.at(i);
			MO* pFind = pMOChild->GetMOByName(strName);
			if (pFind)
				return pFind;
		}
	}

	return NULL;
}

vector<string> MO::getTagPartials(string strTag)
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

string MO::getTypeLabel(string type)
{
	return "";
}

string MO::AppendTagRoot(string& str)
{
	return "";
}



string MO::ResolveTag(string strTagExp, string strTagThis)
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
			MO* p = prj.GetMOByTag("*" + strTagExp);
			if (p)
				tagName = p->getTag();
		}
		else
			tagName = strTagExp;
	}

	//remove root   tagName without root name is a convention
	if(tagName.find(prj.m_strName + ".") == 0)
	{
		tagName = tagName.substr(prj.m_strName.length()+1,tagName.length()-prj.m_strName.length()-1);
	}
	return tagName;
}

string MO::trimProperty(string& strTagExp)
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
string MO::TranslateRelateTag(string rtag)
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


MO* MO::createChildMO(string subTag,string moType)
{
	vector<string> tagNode;
	str::split(tagNode,subTag,".");

	MO* pParent = this;
	MO* pmo = NULL;
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
		pmo->m_strName = strName;
		pmo->m_pParentMO = pParent;
		pParent->m_childMO.push_back(pmo);
		pParent = pmo;
	}

	return pmo;
}

void MO::GetAllChildMO(std::vector<MO*>& aryMO, string type)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		if (m_childMO[i]->m_moType == type)
		{
			aryMO.push_back(m_childMO[i]);
		}
		m_childMO[i]->GetAllChildMO(aryMO, type);
	}
}


map<string,json> MO::getChildCustomMoTypeList()
{
	if (m_childCustomMoTypeList.size() > 0)
		return m_childCustomMoTypeList;

	statisChildCustomMoType(m_childCustomMoTypeList);
	return m_childCustomMoTypeList;
}

void MO::statisChildCustomMoType(map<string, json>& list)
{
	for (auto& i : m_childMO)
	{
		if (i->m_moType == MO_TYPE::customMo)
		{
			json jType;
			jType["type"] = i->m_moCustomType;
			jType["label"] = i->m_moCustomTypeLabel;
			list[i->m_moCustomType] = jType;
		}

		i->statisChildCustomMoType(list);
	}
}

void MO::statisChildMo(json& jStatis)
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



	if (m_moType == MO_TYPE::customOrg) {
		jStatis["project"]= jStatis["project"].get<int>() + 1;
	}
	else if (m_moType == MO_TYPE::customMo) {
		jStatis["smartDev"] = jStatis["smartDev"].get<int>() + 1;

		if (m_bOnline)
			jStatis["online"] = jStatis["online"].get<int>() + 1;
		else
			jStatis["offline"] = jStatis["offline"].get<int>() + 1;
	}

	for (int i = 0; i < m_childMO.size(); i++)
	{
		MO* pC = m_childMO[i];
		pC->statisChildMo(jStatis);
	}
}

MO_QUERIER MO::parseQuerier(json& opt)
{
	MO_QUERIER q;
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
	if (opt["type"] != nullptr)
	{
		q.type = opt["type"].get<string>();
	}
	if (opt["leafType"] != nullptr)
	{
		q.leafType = opt["leafType"].get<string>();
	}
	if (opt["getDetailConf"] != nullptr) {
		q.getConfDetail = opt["getDetailConf"].get<bool>();
	}
	return q;
}


MO* MO::GetRootMO()
{
	if (this == nullptr)
		return nullptr;

	MO* p = this;

	while (p->m_pParentMO)
	{
		p = p->m_pParentMO;
	}

	return p;
}

MO* MO::GetFatherMO(string type)
{
	MO* p = this;

	while (p)
	{
		if (p->m_moType == type)
		{
			return p;
		}

		p = p->m_pParentMO;
	}

	return NULL;
}

MO* MO::GetChildMO(string type)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		MO* pMO = m_childMO.at(i);
		if (pMO->m_moType == type)
			return pMO;

		MO* pChild = pMO->GetChildMO(type);
		if (pChild)
			return pChild;
	}

	return NULL;
}

MO* MO::CopyMO()
{
	MO* pMO = NULL;
	if (m_moType == "mp")pMO = new MP();
	else pMO = new MO();

	*pMO = *this;

	for (int i = 0; i < m_childMO.size(); i++)
	{
		MO* p = m_childMO[i]->CopyMO();
		pMO->m_childMO.push_back(p);
	}

	return pMO;
}

MO& MO::operator=(MO& right)
{
	m_moType = right.m_moType;
	m_strName = right.m_strName;

	//if (right.m_moType == "mp" && m_moType == "mp")
	//{
	//	MP* pl = (MP*)this;
	//	MP* pr = (MP*)&right;
	//	*pl = *pr;
	//}
	return *this;
}

string MO::GetStatusSummary()
{
	string str;
	str += getTag().c_str(); str += "\r\n";
	GetAllChildAlarmInfo(str);


	str = str.substr(0,str.length() - 2);//除掉最后的回车换行
	return str;
}


void MO::GetAllChildAlarmInfo(string& strSummary)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		MO* pChild = m_childMO.at(i);
		pChild->GetAllChildAlarmInfo(strSummary);
	}
}

string TAG::trimRoot(string& tag)
{
	string s = str::trim(tag, prj.m_strName);
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
	if (tag.find(prj.m_strName) == 0)
	{
		return tag;
	}
	else
	{
		if (tag != "")
			return prj.m_strName + "." + tag;
		else
			return prj.m_strName;
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


