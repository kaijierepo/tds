#pragma once
#include "mo.h"
#include "conf.h"
#include "json.hpp"
using json = nlohmann::json;
#include "tdsSession.h"
#include "tds.h"

class ioServer;
class database;
class ioDev;
class amo;
class MP;
class ioServer;
class project : public MO  
{
public:
	project();
	virtual ~project();

	bool loadConf();
	json m_jMOTree;

	MP* getMp(string strTagname);
	void getMpList(map<string, MP*>& MPlist, MO* pMO);
	void getMpList(json& mpList);
	void getMpTypeList(json& mpTypeList);
	
	database* DB;
	map<string, MP*> m_mapAllMP;
	bool bFirstRefresh;
	void UpdateAllMPList();
	map<string, string> m_mapDataLink;
	ioServer* m_ioSrv;
};

extern project prj;
