#pragma once
#include "obj.h"
#include "tdsConf.h"
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
class project : public OBJ  
{
public:
	bool loadConfFile();
	bool loadConf(string& confStr);
	bool loadConf(json& jConf);
	void saveConfFile();
	void clear();
	MP* getMp(string strTagname);
	void getMpList(vector<MP*>& MPlist, OBJ* pMO);
	void getMpList(map<string, MP*>& MPlist, OBJ* pMO);
	void getMpList(json& mpList);
	void getMpTypeList(json& mpTypeList);
	bool getTagsByTagSelector(vector<string>& tags, TAG_SELECTOR& tagSelector);


	string m_strMoTree; //字符串配置数据
	map<string, MP*> m_mapAllMP;
	map<string, vector<OBJ*>> m_mapCustomMOType;

public:
	project();
	virtual ~project();

	bool setMo(json& mo, string tag);
	MP* createMP(string tag, string valType);

private:
	json m_jMOTree;
	void updateMPTable();
	map<string, string> m_mapDataLink;

public:
	shared_mutex m_csPrj;
};

extern project prj;
