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
class TAG_SELECTOR;
class project : public MO  
{
public:
	bool loadConf();
	bool loadConf(string& confStr);
	bool loadConf(json& jConf);
	void saveConf();
	void clear();
	MP* getMp(string strTagname);
	void getMpList(map<string, MP*>& MPlist, MO* pMO);
	void getMpList(json& mpList);
	void getMpTypeList(json& mpTypeList);
	bool getTags(vector<string>& tags, TAG_SELECTOR& tagSelector);

	string m_strMoTree;
	map<string, MP*> m_mapAllMP;
	map<string, vector<MO*>> m_mapCustomMOType;

public:
	project();
	virtual ~project();

	bool setMo(json& mo, string tag);

	bool toJson(json& conf, json serializeOption) override;

	MP* createMP(string tag, string valType);

	

private:
	json m_jMOTree;
	bool bFirstRefresh;
	void updateMPTable();
	map<string, string> m_mapDataLink;

public:
	shared_mutex m_csPrj;
};

extern project prj;
