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
	void generateDefaultProjectConfFile(std::string prjConfPath);
	std::string defaultAppConf_tds();
	std::string defaultProjectConf_tds();

	void loadConfPath(std::vector<KV_INI_LINE>& vecConf);
	void loadConf_tds(std::vector<KV_INI_LINE>& vecConf);
	void loadConf() override;
	void loadCurrentData() override;
	json toJson();
	bool checkKey(std::string toCheck, std::string key);

	KV_INI project_ini;
	KV_INI app_ini;

	int getInt(std::string key, int iDef) override;
	std::string getStr(std::string key, std::string sDef) override;
	bool setStr(std::string key, std::string val) override;
	bool setInt(std::string key, int val) override;


	KV_INI curIni;
	int getCurrentInt(std::string key, int iDef) override;
	std::string getCurrentStr(std::string key, std::string sDef) override;
	bool setCurrentStr(std::string key, std::string val) override;
	bool setCurrentInt(std::string key, int val) override;

	json jsonConf;
	std::string m_confFileName;
};

extern tdsConfig tdsConf;