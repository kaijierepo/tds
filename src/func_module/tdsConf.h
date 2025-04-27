#pragma once
#include <string>
#include "kvIni.h"
#include "tds.h"//not good, but if not include this,it will show the error: undefined iTDSConf.
using namespace std;
#include "json.hpp"
using json = nlohmann::json;



class tdsConfig : public iTDSConf
{
public:
	tdsConfig();
	void generateDefaultAppConfFile();
	void generateDefaultProjectConfFile(string prjConfPath);
	string defaultAppConf_tds();
	string defaultProjectConf_tds();
	string defaultConf_tdb();
	string defaultConf_rphttp();
	void loadConf_httpServer(vector<KV_CONF_ITEM>& vecConf);
	void loadConfPath(vector<KV_INI_LINE>& vecConf);
	void loadConf_tds(vector<KV_INI_LINE>& vecConf);
	void loadConf_rphttp(vector<KV_CONF_ITEM>& vecConf);
	void loadConf() override;
	void loadCurrentData() override;
	json toJson();
	bool checkKey(string toCheck, string key);

	KV_INI project_ini;
	KV_INI app_ini;

	int getInt(string key, int iDef) override;
	string getStr(string key, string sDef) override;
	bool setStr(string key, string val) override;
	bool setInt(string key, int val) override;


	KV_INI curIni;
	int getCurrentInt(string key, int iDef) override;
	string getCurrentStr(string key, string sDef) override;
	bool setCurrentStr(string key, string val) override;
	bool setCurrentInt(string key, int val) override;

	json jsonConf;
	string m_confFileName;
};

extern tdsConfig tdsConf;