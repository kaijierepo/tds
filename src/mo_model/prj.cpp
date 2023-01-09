#include "pch.h"
#include "prj.h"
#include "db.h"
#include "ioSrv.h"
#include "obj.h"
#include "amo.h"
#include "mp.h"
#include "logger.h"


project prj;

project::project()
{
	m_name = "tds";
	m_type = "org";
#ifdef ENABLE_GENICAM
	MP* p = new MP();
	p->m_valType = VAL_TYPE::video;
	p->m_strName = "genicam_0";
	m_mapSpecialMP["genicam_0"] = p;
#endif
}

project::~project()
{

}

bool project::setMo(json& mo, string tag)
{
	OBJ* pmo = queryObj(tag);
	if (pmo)
	{
		pmo->loadConf(mo);
		return true;
	}
	return false;
}

MP* project::createMP(string tag,string valType)
{
	MP* pmp = (MP*)prj.createChildMO(tag, MO_TYPE::mp);
	prj.m_mapAllMP[tag] = pmp;		
	pmp->m_valType = valType;
	return pmp;
}

bool project::loadConfFile()
{
	string& conf = m_strMoTree;
	if (!fs::readFile(tds->conf->confPath + "/mo.json", conf))
	{
		LOG("[keyinfo]未找到监控对象配置mo.json，新建配置");
		m_name = "empty project";
		conf = "";
		TIME st;
		timeopt::now(&st);
		m_strLastModify = timeopt::st2str(st);
	}
	else {
		KV_INI ini;
		ini.load(tds->conf->confPath + "/lastModify.ini");
		string stime = ini.getValStr("mo","");
		m_strLastModify = stime;
	}

	return loadConf(conf);
}

void project::saveConfFile()
{
	json j;
	json opt;
	opt["getConf"] = true;
	opt["getChild"] = true;
	opt["getMp"] = true;
	opt["getStatus"] = false;
	opt["getDetailConf"] = false;

	toJson(j, opt);
	string s = j.dump(2);

	if (s != m_strMoTree) {
		TIME st;
		timeopt::now(&st);
		KV_INI ini;
		ini.load(tds->conf->confPath + "/lastModify.ini");
		ini.setVal("mo", timeopt::st2str(st));
		m_strMoTree = s;
		fs::writeFile(tds->conf->confPath + "/mo.json", s);
	}
}

bool project::loadConf(string& confStr)
{
	//加载空配置
	if (confStr == "")
		return true;

	try {
		json moRoot = json::parse(confStr.c_str());
		moRoot["type"] = "org";
		bool ret = loadConf(moRoot);
		return ret;
	}
	catch (std::exception& e)
	{
		string s = e.what();
		LOG("[error]加载监控对象配置mo.json异常,错误信息:" + s);
		return false;
	}
	return false;
}

bool project::loadConf(json& jConf)
{
	bool ret = OBJ::loadConf(jConf);
	if (ret)
		updateMPTable();
	return ret;
}



void project::clear()
{
	m_mapAllMP.clear();
	clearChildren();
}

void project::getMpList(vector<MP*>& MPlist, OBJ* pMO)
{
	for (int i = 0; i < pMO->m_childObj.size(); i++)
	{
		OBJ* p = pMO->m_childObj.at(i);
		if (p->m_type == "mp")
		{
			MPlist.push_back((MP*)p);
		}
		getMpList(MPlist, p);
	}
}


void project::getMpList(map<string, MP*>& MPlist, OBJ* pMO)
{
	for (int i = 0; i < pMO->m_childObj.size(); i++)
	{
		OBJ* p = pMO->m_childObj.at(i);
		if (p->m_type == "mp")
		{
			MPlist[p->getTag().c_str()] = (MP*)p;
		}
		getMpList(MPlist, p);
	}
}


void project::updateMPTable()
{
	m_mapAllMP.clear();
	getMpList(m_mapAllMP, this);
}


MP* project::getMp(string strSysTag)
{
	map<string, MP*>::iterator it = m_mapAllMP.find(strSysTag);
	if (it != m_mapAllMP.end())
	{
		return it->second;
	}
	return NULL;
}

void project::getMpList(json& mpList)
{
	for (map<string, MP*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++) {
		string strKey = it->first;
		mpList.push_back(it->second->getTag());
	}
}

void project::getMpTypeList(json& mpTypeList)
{
	map<string,string> mapTypes;
	for (map<string, MP*>::iterator it = m_mapAllMP.begin(); it != m_mapAllMP.end(); it++) {
		MP* pmp = (MP*)it->second;
		string mpType = "";

		//模拟量和开关量的监测点名称 name 作为 mptype
		//因为实际使用中，需要用监测点名称区分类似温度、湿度等类型概念
		//视频监测点一般可能使用位置命名，因此不是类型，是一个具体的位置，不作为mpType
		//json类型数据 用户需要自己指定监测点类型，在mo配置中配置
		//mpType是可阅读字符串
		mpType = pmp->getMpType();

		if(mapTypes.find(mpType) != mapTypes.end())
			continue;

		mapTypes[mpType] = mpType;

		json oneType;
		oneType["valType"] = pmp->m_valType;
		oneType["type"] = mpType;
		mpTypeList.push_back(oneType);
	}
}

json project::getObjTemplate(string devTplType)
{
	for (auto& i : m_mapObjTempalte) {
		string tplName = i.first;
		if (devTplType.find(tplName) != string::npos) {
			return i.second.tplData;
		}
	}
	return nullptr;
}

bool project::loadObjTemplate()
{
	string p = tds->conf->confPath + "/template/object/conf.json";
	string tplListStr;
	if (fs::readFile(p, tplListStr)) {
		try {
			json jTplList = json::parse(tplListStr);
			for (auto& i : jTplList) {
				OBJ_TEMPLATE ct;
				ct.type = i["type"];
				ct.typeLabel = i["typeLabel"];
				string tplDataStr;
				string p1 = tds->conf->confPath + "/template/object/" + ct.type + ".json";
				if (fs::readFile(p1, tplDataStr)) {
					ct.tplData = json::parse(tplDataStr);
					m_mapObjTempalte[ct.type] = ct;
				}
			}
		}
		catch (exception& e) {
			LOG("[error]加载/template/object/conf.json失败,error=%s", e.what());
		}
	}
	return false;
}

void project::saveObjTemplate(OBJ_TEMPLATE& ot)
{
	prj.m_mapObjTempalte[ot.type] = ot;


	//保存索引信息
	string p = tds->conf->confPath + "/template/object/conf.json";
	json jConf = json::array();
	for (auto& i : m_mapObjTempalte) {
		json c;
		c["type"] = i.second.type;
		c["typeLabel"] = i.second.typeLabel;
		jConf.push_back(c);
	}
	string sConf = jConf.dump(2);
	fs::writeFile(p, sConf);



	string chanPath = tds->conf->confPath + "/template/object/";
	string s = ot.tplData.dump(2);
	fs::writeFile(chanPath + "/" + ot.type + ".json", s);
}

void project::getAllVarExpScript()
{
	std::map<string, SCRIPT_INFO> expScripts;
	expScripts.clear();
	std::vector<MP*> aryMP;
	prj.GetAllChildMp(aryMP);
	for (int i = 0; i < aryMP.size(); i++) {
		MP* p = aryMP[i];
		if (p->m_ioType == "v" && p->m_expression != "") {
			SCRIPT_INFO i;
			i.script = p->m_expression;
			i.tagThis = p->getTag();
			expScripts[p->getTag()] = i;
		}
	}
	scriptManager.updateVarExpScript(expScripts);
}
