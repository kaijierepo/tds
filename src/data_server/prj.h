#pragma once
#include "obj.h"
#include "tdsConf.h"
#include "json.hpp"
using json = nlohmann::json;
#include "tdsSession.h"
#include "tds.h"
#include <shared_mutex>
#include "scriptManager.h"

struct OBJ_TEMPLATE {
	string type;
	json tplData;
	OBJ obj;

	OBJ_TEMPLATE() {

	}

	~OBJ_TEMPLATE() {

	}
};

class ioServer;
class TDB;
class ioDev;
class MP;
class TAG_SELECTOR;

struct EZVIZ_ACCESS_INFO {
	string tag;
	string serialNo;
	string appKey;
	string secret;
	string token;
	string flvUrl;
	string ezopenUrl;
	TIME lastUpdate;
};



class project : public OBJ  
{
public:
	bool loadConfFile();
	bool loadConf(string& confStr);
	bool loadConf(json& jConf);
	void saveConfFile();
	void clear();
	MP* getMp(string strTagname);
	void getMpTypeList(json& mpTypeList);

	string m_strMoTree; //字符串配置数据//最近一次保存的缓存，如果前端获取整颗树，直接获取此处加快速度
	map<string, MP*> m_mapAllMP;

	bool m_enableEzviz;
	map<string, EZVIZ_ACCESS_INFO> m_mapEzvizAccess;

public:
	project();
	virtual ~project();

	string getTdsId();

	bool setMo(json& mo, string tag);
	MP* createMP(string tag, string valType);

	//对象模版配置
	json getObjTemplate(string objTplType);
	bool loadObjTemplate();
	void setObjTemplate(json& params);
	void getAllVarExpScript();
	map<string, OBJ_TEMPLATE*> m_mapObjTempalte;

	bool openStream(string tag,string pushTo = "");

	bool closeStream(string tag);

private:
	json m_jMOTree;
	map<string, string> m_mapDataLink;

public:
	shared_mutex m_csPrj;
};

extern void g_getTagsByTagSelector(vector<string>& tags, TAG_SELECTOR& tagSelector);
extern project prj;
