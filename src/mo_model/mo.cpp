#include "pch.h"
#include "mo.h"
#include "common.h"
#include "prj.h"
#include "mp.h"
#include "amo.h"
#include "ioDev.h"
#include "as.h"


mo* createMO(string type)
{
	mo* p = NULL;
	if (type == MO_TYPE::mo)
	{
		p = new mo();
	}
	if (type == MO_TYPE::project)
	{
		p = new mo();
	}
	else if (type == MO_TYPE::mp)
	{
		p = new mp();
	}
	else if (type == MO_TYPE::mpgroup)
	{
		p = new mo();
	}
	else if (type == MO_TYPE::amo)
	{
		p = new amo();
	}

	return p;
}

mo::mo()
{
	m_pParentMO = NULL;
	m_moType = MO_TYPE::mo;
	m_bShow = true;
}

mo::~mo()
{

}

bool mo::loadConf(json& conf)
{
	m_strName = conf["name"];
	m_moType = conf["type"];
	auto children = conf["children"];
	for (auto& child : children)
	{
		mo* pmo = createMO(child["type"]);
		if (pmo)
		{
			pmo->loadConf(child);
			m_childMO.push_back(pmo);
			pmo->m_pParentMO = this;
		}
	}
	return true;
}

bool mo::saveConf(json& conf, string opt)
{
	if (opt == "exclude-common-mp" && m_moType == MO_TYPE::mp)
	{
		mp* p = (mp*)this;
		if(p->m_valType != "json")
   			return false;
	}
	conf["name"] = m_strName;
	conf["type"] = m_moType;

	if (m_moType == "mp")
	{
		mp* p = (mp*)this;
		conf["val_type"] = p->m_valType;
		if (p->m_valType == "json")
			conf["custom_val_type"] = p->m_customValType;
	}

	json jChildren;
	for (auto& pmochild : m_childMO)
	{
		json jChild;
		if(pmochild->saveConf(jChild,opt))
			jChildren.push_back(jChild);
	}
	if(!jChildren.is_null())
		conf["children"] = jChildren;
	return true;
}

void mo::removeMp(json& mo)
{
	if (mo["children"] != nullptr)
	{
		json jChildren = mo["children"];
	}
}

mo* mo::GetProjectMO()
{
	mo* pTmp = this;
	while (pTmp->m_pParentMO)
	{
		pTmp = pTmp->m_pParentMO;
	}

	return pTmp;
}
json mo::getRT()
{
	json j;
	j["name"] = m_strName;
	j["type"] = m_moType;
	json jChildren;
	for(int i=0;i<m_childMO.size();i++)
	{
		mo* pmo = m_childMO.at(i);
		jChildren.push_back(pmo->getRT());
	}
	j["children"]=jChildren;
	return j;
}

string mo::getTag()
{
	mo* pTmpParent = m_pParentMO;
	string strTagName = m_strName;

	while (pTmpParent && pTmpParent->m_pParentMO)//第一级位号工程名称默认不显示
	{
		strTagName = pTmpParent->m_strName + "." + strTagName;
		pTmpParent = pTmpParent->m_pParentMO;
	}

	return strTagName;
}

vector<string> mo::GetAlias()
{
	vector<string> vecAlias;
	str::removeChar(m_alias, ' ');
	if (m_alias.length() == 0)
		return vecAlias;
	str::split(vecAlias, m_alias, ",");
	return vecAlias;
}

vector<string> mo::GetAllTagNamePlus()
{
	mo* pTmpParent = m_pParentMO;
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


string mo::getTagWithRoot()
{
	mo* pTmpParent = m_pParentMO;
	string strTagName = m_strName;
	while (pTmpParent)//包含工程节点名称的位号
	{
		strTagName = pTmpParent->m_strName + "." + strTagName;
		pTmpParent = pTmpParent->m_pParentMO;
	}
	return strTagName;
}

void mo::GetMOByTag(std::vector<mo*>* tagVec, string strTag)
{
	string tagCandidate = getTag();
	TAG_SELECTOR ts;
	ts.init(strTag);
	if(ts.match(tagCandidate))
		tagVec->push_back(this);

	for (int i = 0; i < m_childMO.size(); i++)
	{
		mo* pMOChild = m_childMO.at(i);
		pMOChild->GetMOByTag(tagVec, strTag);
	}
}

void mo::GetMPByTag(std::vector<mp*>* tagVec, string strTag)
{
	std::vector<mo*> vec;
	GetMOByTag(&vec, strTag);
	for (int i = 0; i < vec.size(); i++)
	{
		mo* p = vec.at(i);
		if (p->m_moType == "mp")
		{
			tagVec->push_back((mp*)p);
		}
	}
}

mo* mo::GetMOByTag(string strTag)
{
	vector<mo*> tags;
	GetMOByTag(&tags, strTag);
	if (tags.size() > 0)
		return tags[0];
	else
		return NULL;
}

mp* mo::GetMPByTag(string strTag)
{
	mo* pMO = GetMOByTag(strTag);
	if (pMO && pMO->m_moType == "mp")
		return (mp*)pMO;
	return nullptr;
}

mp* mo::GetMPByName(string strName)
{
	mo* p = GetMOByName(strName);
	if (p && p->m_moType == "mp")
	{
		return (mp*)p;
	}
	return NULL;
}

mo* mo::GetMOByName(string strName)
{
	if (m_strName == strName)
		return this;
	else
	{
		for (int i = 0; i < m_childMO.size(); i++)
		{
			mo* pMOChild = m_childMO.at(i);
			mo* pFind = pMOChild->GetMOByName(strName);
			if (pFind)
				return pFind;
		}
	}

	return NULL;
}

mo* mo::GetMO(string strName)
{
	mp* ret = (mp*)GetMOByName(strName);
	if (ret == NULL)
	{
		ret = new mp();
		ret->m_strName = strName;
	}
	return ret;
}

vector<string> mo::getTagPartials(string strTag)
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

string mo::getTypeLabel(string type)
{
	return "";
}

string mo::AppendTagRoot(string& str)
{
	return "";
}



string mo::ResolveTag(string strTagExp, string strTagThis)
{
	string tagName = strTagExp;
	//this的解析，this后面可能带 .std 等后缀
	if (strTagExp.find("this") != string::npos)
	{
		str::replace(tagName, "this", strTagThis);
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
		str::replace(rtag, "\\", "/");
		str::replace(rtag, "\\\\", "/");

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
		str::replace(rtag, "./", "");

		if (rtag.length() > 0)
			tag = tag + "." + rtag;
		tagName = tag;
	}
	//位号全名
	else
	{
		if (strTagExp.find(".") == string::npos)//仅指定name
		{
			mo* p = prj.GetMOByTag("*" + strTagExp);
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

string mo::trimProperty(string& strTagExp)
{
	string strTagProperty;
	if (strTagExp.substr(strTagExp.length() - 4, 4) == ".std")
	{
		strTagExp = strTagExp.substr(0, strTagExp.length() - 4);
		strTagProperty = ".std";
	}
	return strTagProperty;
}

void mo::updateDataLink()
{
	for (int i = 0; i < m_vecIODev.size(); i++)
	{
		ioDev* p = (ioDev*) m_vecIODev.at(i);
		p->m_installedMoTag = getTag().c_str();
	}

	for (int i = 0; i < m_childMO.size(); i++)
	{
		mo* pMO = m_childMO.at(i);
		pMO->updateDataLink();
	}
}

//该函数的参数必须是相对位号格式
string mo::TranslateRelateTag(string rtag)
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
	str::replace(rtag, "./", "");
	if (rtag.find("?") != string::npos) {
		int pos1 = tag.find("#");
		int pos2 = -1;
		string strDC = "";//dao cha
		if (pos1 >= 0) {
			pos2 = tag.rfind(".", pos1);
			if (pos2 >= 0 && pos1 - pos2 - 1 >= 0) {
				strDC = tag.substr(pos2 + 1, pos1 - pos2 - 1);
				str::replace(rtag, "?", strDC.c_str());
			}

		}
	}
	if (rtag.length() > 0)
		tag = tag + "." + rtag;

	return tag;
}


void mo::GetAllChildMO(std::vector<mo*>& aryMO, string type)
{
	m_childMO;
	for (int i = 0; i < m_childMO.size(); i++)
	{
		if (m_childMO[i]->m_moType == type)
		{
			aryMO.push_back(m_childMO[i]);
		}
		m_childMO[i]->GetAllChildMO(aryMO, type);
	}
}


database* mo::GetDB()
{
	project* p = (project*)GetRootMO();
	if (p)
	{
		return p->DB;
	}
	return nullptr;
}

mo* mo::GetRootMO()
{
	if (this == nullptr)
		return nullptr;

	mo* p = this;

	while (p->m_pParentMO)
	{
		p = p->m_pParentMO;
	}

	return p;
}

mo* mo::GetFatherMO(string type)
{
	mo* p = this;

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

mo* mo::GetChildMO(string type)
{
	for (int i = 0; i < m_childMO.size(); i++)
	{
		mo* pMO = m_childMO.at(i);
		if (pMO->m_moType == type)
			return pMO;

		mo* pChild = pMO->GetChildMO(type);
		if (pChild)
			return pChild;
	}

	return NULL;
}

mo* mo::CopyMO()
{
	mo* pMO = NULL;
	if (m_moType == "mp")pMO = new mp();
	else pMO = new mo();

	*pMO = *this;

	for (int i = 0; i < m_childMO.size(); i++)
	{
		mo* p = m_childMO[i]->CopyMO();
		pMO->m_childMO.push_back(p);
	}

	return pMO;
}

mo& mo::operator=(mo& right)
{
	m_moType = right.m_moType;
	m_strName = right.m_strName;

	if (right.m_moType == "mp" && m_moType == "mp")
	{
		mp* pl = (mp*)this;
		mp* pr = (mp*)&right;
		*pl = *pr;
	}
	return *this;
}

string mo::GetStatusSummary()
{
	string str;
	str += getTag().c_str(); str += "\r\n";
	GetAllChildAlarmInfo(this, str);


	str = str.substr(0,str.length() - 2);//除掉最后的回车换行
	return str;
}


void mo::GetAllChildAlarmInfo(mo* pMO, string& strSummary)
{
	for (int i = 0; i < pMO->m_childMO.size(); i++)
	{
		mo* pChild = pMO->m_childMO.at(i);
		GetAllChildAlarmInfo(pChild, strSummary);
	}
}
