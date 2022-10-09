#include "pch.h"
#include "prj.h"
#include "db.h"
#include "ioSrv.h"
#include "mo.h"
#include "amo.h"
#include "mp.h"
#include "logger.h"


project prj;

project::project()
{
	m_strName = "tds";
	m_moType = "project";
#ifdef ENABLE_GENICAM
	MP* p = new MP();
	p->m_valType = VAL_TYPE::video;
	p->m_strName = "genicam_0";
	m_mapSpecialMP["genicam_0"] = p;
#endif
}

project::~project()
{

}

bool project::setMo(json& mo, string tag)
{
	MO* pmo = GetMOByTag(tag);
	if (pmo)
	{
		pmo->loadConf(mo);
		return true;
	}
	return false;
}

MP* project::createMP(string tag,string valType)
{
	MP* pmp = (MP*)prj.createChildMO(tag, MO_TYPE::mp);
	prj.m_mapAllMP[tag] = pmp;		
	pmp->m_valType = valType;
	return pmp;
}

bool project::loadConfFile()
{
	string& conf = m_strMoTree;
	if (!fs::readFile(tds->conf->confPath + "/mo.json", conf))
	{
		LOG("[keyinfo]未找到监控对象配置mo.json，新建配置");
		m_strName = "empty project";
		conf = "";
		SYSTEMTIME st;
		GetLocalTime(&st);
		m_strLastModify = timeopt::st2str(st);
	}
	else {
		TDS_INI ini;
		ini.load(tds->conf->confPath + "/lastModify.ini");
		string stime = ini.getValStr("mo","");
		m_strLastModify = stime;
	}

	return loadConf(conf);
}

void project::saveConfFile()
{
	json j;
	json opt;
	opt["getConf"] = true;
	opt["getChild"] = true;
	opt["getMp"] = true;
	opt["getStatus"] = false;
	opt["getDetailConf"] = false;

	toJson(j, opt);
	string s = j.dump(2);

	if (s != m_strMoTree) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		TDS_INI ini;
		ini.load(tds->conf->confPath + "/lastModify.ini");
		ini.setVal("mo", timeopt::st2str(st));
		m_strMoTree = s;
		fs::writeFile(tds->conf->confPath + "/mo.json", s);
	}
}

bool project::loadConf(string& confStr)
{
	//加载空配置
	if (confStr == "")
		return true;

	try {
		json moRoot = json::parse(confStr.c_str());
		moRoot["type"] = "project";
		bool ret = loadConf(moRoot);
		return ret;
	}
	catch (std::exception& e)
	{
		string s = e.what();
		LOG("[error]加载监控对象配置mo.json异常,错误信息:" + s);
		return false;
	}
	return false;
}

bool project::loadConf(json& jConf)
{
	bool ret = MO::loadConf(jConf);
	if (ret)
		updateMPTable();
	return ret;
}



void project::clear()
{
	m_mapAllMP.clear();
	m_mapCustomMOType.clear();
	clearChildren();
}


void project::getMpList(map<string, MP*>& MPlist, MO* pMO)
{
	for (int i = 0; i < pMO->m_childMO.size(); i++)
	{
		MO* p = pMO->m_childMO.at(i);
		if (p->m_moType == "mp")
		{
			MPlist[p->getTag().c_str()] = (MP*)p;
		}
		getMpList(MPlist, p);
	}
}


void project::updateMPTable()
{
	m_mapAllMP.clear();
	getMpList(m_mapAllMP, this);
}


MP* project::getMp(string strSysTag)
{
	map<string, MP*>::iterator it = m_mapAllMP.find(strSysTag);
	if (it != m_mapAllMP.end())
	{
		return it->second;
	}
	return NULL;
}

void project::getMpList(json& mpList)
{
	for (map<string, MP*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++) {
		string strKey = it->first;
		mpList.push_back(it->second->getTag());
	}
}

void project::getMpTypeList(json& mpTypeList)
{
	map<string,string> mapTypes;
	for (map<string, MP*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++) {
		MP* pmp = (MP*)it->second;
		string mpType = "";

		//模拟量和开关量的监测点名称 name 作为 mptype
		//因为实际使用中，需要用监测点名称区分类似温度、湿度等类型概念
		//视频监测点一般可能使用位置命名，因此不是类型，是一个具体的位置，不作为mpType
		//json类型数据 用户需要自己指定监测点类型，在mo配置中配置
		//mpType是可阅读字符串
		mpType = pmp->getMpType();

		if(mapTypes.find(mpType) != mapTypes.end())
			continue;

		mapTypes[mpType] = mpType;

		json oneType;
		oneType["valType"] = pmp->m_valType;
		oneType["type"] = mpType;
		mpTypeList.push_back(oneType);
	}
}

bool project::getTags(vector<string>& tags, TAG_SELECTOR& tagSelector)
{
	if (tagSelector.singleMode)
	{
		tags.push_back(tagSelector.tagExp);
	}
	else
	{
		vector<MP*> tagSet;
		vector<MP*> tagSetTmp;
		prj.GetMPByTag(&tagSetTmp, tagSelector.tagExp);
		if (tagSelector.type != "")//has type filter //load from database 监测点类型过滤
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
			tags.push_back(i->getTag());
		}
	}

	return false;
}


