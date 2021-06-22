#pragma once
#include "mo.h"
#include "conf.h"
#include "json.hpp"
using json = nlohmann::json;

class ioServer;
class database;
class ioDev;
class amo;
class mp;
class ioServer;
class project : public mo  
{
public:
	project();
	virtual ~project();

	bool loadConf();
	json m_jMOTree;

	mp* getMp(string strTagname);
	void getMpList(map<string, mp*>& MPlist, mo* pMO);
	void getMpList(json& mpList);
	void getMpTypeList(json& mpTypeList);
	
	database* DB;
	map<string, mp*> m_mapAllMP;
	bool bFirstRefresh;
	void UpdateAllMPList();
	map<string, string> m_mapDataLink;
	ioServer* m_ioSrv;
};

extern project prj;
