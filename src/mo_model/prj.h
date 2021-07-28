#pragma once
#include "mo.h"
#include "conf.h"
#include "json.hpp"
using json = nlohmann::json;
#include "tdsSession.h"
#include "tds.h"
#include <shared_mutex>

class ioServer;
class database;
class ioDev;
class amo;
class MP;
class ioServer;
class project : public MO  
{
public:
	bool loadConf();
	MP* getMp(string strTagname);
	void getMpList(map<string, MP*>& MPlist, MO* pMO);
	void getMpList(json& mpList);
	void getMpTypeList(json& mpTypeList);
	
	database* DB;
	ioServer* m_ioSrv;
	map<string, MP*> m_mapAllMP;
public:
	project();
	virtual ~project();

private:
	json m_jMOTree;
	bool bFirstRefresh;
	void updateMPTable();
	map<string, string> m_mapDataLink;

public:
	shared_mutex m_csPrj;
};

extern project prj;
