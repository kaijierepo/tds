#pragma once
#include <string>
#include "kvIni.h"
using namespace std;
#include "json.hpp"
using json = nlohmann::json;




class tdsConfig : public iTDSConf
{
public:
	tdsConfig();
	void generateDefaultConfFile(string m);
	string defaultConf_tds();
	string defaultConf_tdb();
	string defaultConf_rphttp();
	void loadConf_httpServer(vector<KV_CONF_ITEM>& vecConf);
	void loadConf_tds(vector<KV_CONF_ITEM>& vecConf);
	void loadConf_rphttp(vector<KV_CONF_ITEM>& vecConf);
	void loadConf() override;
	json toJson();
	
	bool checkKey(string toCheck, string key);
	string normalizationKey(string key);

	KV_INI tdsIni;

	int getInt(string key, int iDef) override;
	string getStr(string key, string sDef) override;
	bool setStr(string key, string val) override;
	bool setInt(string key, string val) override;


	json jsonConf;
	string m_confFileName;
};

extern tdsConfig tdsConf;