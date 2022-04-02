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
	if (type == MO_TYPE::mo || type == MO_TYPE::custom)
	{
		p = new MO();
	}
	else if (type == MO_TYPE::org)
	{
		p = new MO();
	}
	else if (type == MO_TYPE::project)
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
	m_bShow = true;
}

MO::~MO()
{

}

bool MO::loadConf(json& conf)
{
	m_strName = conf["name"];
	m_moType = conf["type"];
	if (m_moType == "custom" && conf.contains("customType") && conf["customType"].get<string>().length() > 0)
	{
		m_moCustomType = conf["customType"];
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
	return true;
}

//serializeOption
//root 返回位号的相对根
//type mo类型
//recursive 是否递归
//onlyConf 仅配置
bool MO::toJson(json& conf, json serializeOption)
{
	conf["name"] = m_strName;
	conf["type"] = m_moType;
	if (m_moCustomType != "")
		conf["customType"] = m_moCustomType;

	if (serializeOption["onlyConf"] != nullptr && serializeOption["onlyConf"].get<bool>() == true)
	{

	}
	else
	{
		if (m_strIoAddrBind != "")
			conf["ioAddrBind"] = m_strIoAddrBind;
		string tag = getTag();
		if (serializeOption["root"] != nullptr)
		{
			string root = serializeOption["root"].get<string>();
			tag = str::trimPrefix(tag, root);
			tag = str::trimPrefix(tag, ".");
		}
		conf["tag"] = tag; //tag = "" 表示根节点。 tds中约定这样表示
	}
	

	if (serializeOption["type"] != nullptr)
	{
		string type = serializeOption["type"].get<string>();
		if (type == "org") //组织结构
		{
			if (m_moType != type && m_moType != "project")
				return false; //请求组织结构，遇到不是组织结构的MO节点，不返回该节点
		}
		else if (type == "mo")
		{
			if (m_moType == "mp")
				return false;
		}
	}

	//是否需要递归序列化子对象
	if (serializeOption["recursive"] != nullptr && serializeOption["recursive"].get<bool>() == false)
	{
		return true;
	}

	if (m_moType != MO_TYPE::mp)
	{
		json jChildren = json::array();
		for (auto& pmochild : m_childMO)
		{
			json jChild;
			if (pmochild->toJson(jChild, serializeOption))
				jChildren.push_back(jChild);
		}
		conf["children"] = jChildren;
	}
	
	return true;
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

MO* MO::GetMO(string strName)
{
	MP* ret = (MP*)GetMOByName(strName);
	if (ret == NULL)
	{
		ret = new MP();
		ret->m_strName = strName;
	}
	return ret;
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

void MO::updateDataLink()
{
	for (int i = 0; i < m_vecIODev.size(); i++)
	{
		ioDev* p = (ioDev*) m_vecIODev.at(i);
		p->m_strTagBind = getTag().c_str();
	}

	for (int i = 0; i < m_childMO.size(); i++)
	{
		MO* pMO = m_childMO.at(i);
		pMO->updateDataLink();
	}
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

void MO::getAllSmartDev(std::vector<MO*>& aryMO)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		if (m_childMO[i]->m_strIoAddrBind != "")
		{
			aryMO.push_back(m_childMO[i]);
		}
		m_childMO[i]->getAllSmartDev(aryMO);
	}
}

map<string,string> MO::getChildCustomMoTypeList()
{
	if (m_childCustomMoTypeList.size() > 0)
		return m_childCustomMoTypeList;

	statisChildCustomMoType(m_childCustomMoTypeList);
	return m_childCustomMoTypeList;
}

void MO::statisChildCustomMoType(map<string,string>& list)
{
	for (auto& i : m_childMO)
	{
		if (i->m_moType == MO_TYPE::custom)
		{
			list[i->m_moCustomType] = i->m_moCustomType;
		}

		i->statisChildCustomMoType(list);
	}
}


database* MO::GetDB()
{
	project* p = (project*)GetRootMO();
	if (p)
	{
		return p->DB;
	}
	return nullptr;
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
	tag = str::trimPrefix(tag, root + ".");
	return tag;
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


