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
		LOG("project conf mo.json not find,use empty config!");
		m_strName = "empty project";
		return true;
	}

	try {
		json moRoot = json::parse(conf.c_str());
		bool ret = mo::loadConf(moRoot);
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


void project::getMpList(map<string, mp*>& MPlist, mo* pMO)
{
	for (int i = 0; i < pMO->m_childMO.size(); i++)
	{
		mo* p = pMO->m_childMO.at(i);
		if (p->m_moType == "mp")
		{
			MPlist[p->getTag().c_str()] = (mp*)p;
		}
		getMpList(MPlist, p);
	}
}


void project::UpdateAllMPList()
{
	if (!bFirstRefresh) m_mapAllMP.clear();
	getMpList(m_mapAllMP, this);
}


mp* project::getMp(string strTagname)
{
	for (map<string, mp*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++)
	{
		if (it->second->getTag().c_str() == strTagname) return it->second;
	}
	return NULL;
}

void project::getMpList(json& mpList)
{
	for (map<string, mp*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++) {
		string strKey = it->first;
		mpList.push_back(it->second->getTag());
	}
}

void project::getMpTypeList(json& mpTypeList)
{
	map<string,string> mapTypes;
	for (map<string, mp*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++) {
		mp* pmp = (mp*)it->second;
		string mpType = "";
		string typeLabel = "";
		
		mpType = pmp->getMpType();//common mp name is used as mptype; custom mp has a user defined mp type
		typeLabel = pmp->getMpTypeLabel();

		if(mapTypes.find(mpType) != mapTypes.end())
			continue;

		mapTypes[mpType] = mpType;

		json oneType;
		oneType.push_back(mpType);
		oneType.push_back(typeLabel);
		mpTypeList.push_back(oneType);
	}
}