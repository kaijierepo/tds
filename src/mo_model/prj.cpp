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
	m_ioSrv = new ioServer();
	m_strName = "tds";
	DB = &db;

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


bool project::toJson(json& conf, json serializeOption)
{
	conf["name"] = m_strName;
	conf["type"] = m_moType;
	if (m_moCustomType != "")
		conf["customType"] = m_moCustomType;

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

MP* project::createMP(string tag,string valType)
{
	MP* pmp = (MP*)prj.createChildMO(tag, MO_TYPE::mp);
	prj.m_mapAllMP[tag] = pmp;		
	pmp->m_valType = valType;
	return pmp;
}

bool project::loadConf()
                                {
	m_mapAllMP.clear();
	m_mapCustomMOType.clear();

	string& conf = m_strMoTree;
	if (!fs::readFile(tds->conf->projectConfPath + "/mo.json", conf))
	{
		LOG("[warn][项目配置  ] 未找到配置，打开空项目。路径:" + tds->conf->projectConfPath);
		m_strName = "empty project";
		return true;
	}
	else
	{
		LOG("[项目配置  ] 路径:" + tds->conf->projectConfPath);
	}

	try {
		json moRoot = json::parse(conf.c_str());
		bool ret = MO::loadConf(moRoot);
		if (ret)
			updateMPTable();
		return ret;
	}
	catch (std::exception& e)
	{
		string s = e.what();
		std::cout<<s<<std::endl;
		return false;
	}
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
	if (!bFirstRefresh) m_mapAllMP.clear();
	getMpList(m_mapAllMP, this);
}


MP* project::getMp(string strTagname)
{
	for (map<string, MP*>::iterator it = m_mapSpecialMP.begin(); it != m_mapSpecialMP.end(); it++)
	{
		if (it->second->getTag().c_str() == strTagname) return it->second;
	}

	for (map<string, MP*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++)
	{
		if (it->second->getTag().c_str() == strTagname) return it->second;
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


