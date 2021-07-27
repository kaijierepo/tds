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
}

project::~project()
{

}

bool project::loadConf()
{
	string conf;
	if (!fs::readFile(tds->conf->projectConfPath + "\\mo.json", conf))
	{
		LOG("[项目配置  ] 未找到配置，打开空项目。路径:" + tds->conf->projectConfPath);
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
			UpdateAllMPList();
		return ret;
	}
	catch (std::exception& e)
	{
		std::cout<<e.what()<<std::endl;
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


void project::UpdateAllMPList()
{
	if (!bFirstRefresh) m_mapAllMP.clear();
	getMpList(m_mapAllMP, this);
}


MP* project::getMp(string strTagname)
{
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
		string typeLabel = "";
		
		mpType = pmp->getMpType();//common MP name is used as mptype; custom MP has a user defined MP type
		typeLabel = pmp->getMpTypeLabel();

		if(mapTypes.find(mpType) != mapTypes.end())
			continue;

		mapTypes[mpType] = mpType;

		json oneType;
		oneType["valType"] = pmp->m_valType;
		oneType["type"] = mpType;
		oneType["label"] = typeLabel;
		mpTypeList.push_back(oneType);
	}
}