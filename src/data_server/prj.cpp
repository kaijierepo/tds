#include "pch.h"
#include "prj.h"
#include "tdb.h"
#include "ioSrv.h"
#include "obj.h"
#include "mp.h"
#include "logger.h"


project prj;

void g_getTagsByTagSelector(vector<string>& tags, TAG_SELECTOR& tagSelector) {
	prj.getTagsByTagSelector(tags, tagSelector);
}

project::project()
{
	m_name = "tds";
	m_level = "org";
	m_enableEzviz = false;
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

string project::getTdsId()
{
	return m_name;
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
	OBJ::m_bDefaultOnline = tds->conf->getInt("objDefaultOnline", 0) > 0 ? true:false;


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
	//json opt;
	//opt["getConf"] = true;
	//opt["getChild"] = true;
	//opt["getMp"] = true;
	//opt["getStatus"] = false;
	//opt["getDetailConf"] = false;

	OBJ_QUERIER q;
	q.getConf = true;
	q.getChild = true;
	q.getMp = true;
	q.getStatus = false;
	q.getConfDetail = false;

	toJson(j, q);
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
		moRoot["level"] = "org";
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

bool project::loadConf(json& jConf,bool bCreate)
{
	bool ret = OBJ::loadConf(jConf, bCreate);
	return ret;
}



void project::clear()
{
	m_mapAllMP.clear();
	clearChildren();
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
			return i.second->tplData;
		}
	}
	return nullptr;
}

bool project::loadObjTemplate()
{
	string p = tds->conf->confPath + "/template/object";

	vector<fs::FILE_INFO> fileList;
	fs::getFileList(fileList, p);


	for (auto& fi : fileList) {
		string path = tds->conf->confPath + "/template/object/" + fi.name;
		if (fi.name == "conf.json") {
			continue;
		}

		string s;
		if (fs::readFile(path, s)) {
			OBJ_TEMPLATE* pct = new OBJ_TEMPLATE;
			pct->tplData = json::parse(s);
			pct->obj.loadConf(pct->tplData);
			string type = str::trimSuffix(fi.name, ".json");
			m_mapObjTempalte[type] = pct;
		}
	}
	return false;
}

void project::setObjTemplate(json& params)
{
	OBJ_TEMPLATE* ct  = new OBJ_TEMPLATE();

	ct->type = params["type"];
	
	//str::hanZi2Pinyin(ct->typeLabel, ct->type);
	ct->tplData = params["tplData"];
	ct->obj.loadConf(ct->tplData);

	auto pOld = m_mapObjTempalte.find(ct->type);
	if (pOld != m_mapObjTempalte.end()) {
		delete pOld->second;
	}
	prj.m_mapObjTempalte[ct->type] = ct;


	//保存索引信息
	string p = tds->conf->confPath + "/template/object/conf.json";
	json jConf = json::array();
	for (auto& i : m_mapObjTempalte) {
		json c;
		c["type"] = i.second->type;
		jConf.push_back(c);
	}
	string sConf = jConf.dump(2);
	fs::writeFile(p, sConf);

	string chanPath = tds->conf->confPath + "/template/object/";
	string s = ct->tplData.dump(2);
	fs::writeFile(chanPath + "/" + ct->type + ".json", s);
}

void project::getAllVarExpScript()
{
	vector<SCRIPT_INFO> expScripts;
	expScripts.clear();
	std::vector<MP*> aryMP;
	prj.GetAllChildMp(aryMP);
	for (int i = 0; i < aryMP.size(); i++) {
		MP* p = aryMP[i];
		if (p->m_ioType == "v" && p->m_expression != "") {
			SCRIPT_INFO i;
			i.script = p->m_expression;
			i.calcMpTag = p->getTag();
			i.callerObjTag = TAG::getParentTag(i.calcMpTag); //计算表达式的脚本，相当于该监控点的父节点作为callerObj调用该脚本
			expScripts.push_back(i);
		}
	}
	scriptManager.updateVarExpScript(expScripts);
}


bool project::openStream(string tag, string pushTo)
{
	bool ret = false;
	MP* pmp = prj.GetMPByTag(tag);
	if (pmp) {
		if (pmp->m_isOpenningStream) {
			LOG("[流媒体  ]当前正在打开媒体源，收到重复打开请求，忽略,位号:%s,当前配置地址:%s",tag.c_str(), pmp->m_mediaUrl.c_str());
			return false;
		}

		pmp->m_isOpenningStream = true;
		if (pmp->m_mpStatus.m_pullingSrcUrl != pmp->m_mediaUrl) {
			LOG("[流媒体  ]监测到媒体源配置变更，先关闭拉流，当前拉流地址:%s,当前配置地址:%s", pmp->m_mpStatus.m_pullingSrcUrl.c_str(), pmp->m_mediaUrl.c_str());
			pmp->stopStreamPull(tag);
		}
		bool retPull = pmp->startStreamPull(); 
		bool pushRet = false;
		if (pushTo != "") {
			if (retPull) {
				timeopt::sleepMilli(500);
				pushRet = pmp->startStreamPush(pushTo);
				LOG("[流媒体  ]向上级服务推流，url=%s", pushTo.c_str());
				if (pushRet) {
					ret = true;
				}
			}
		}
		else
		{
			ret = retPull;
		}
		pmp->m_isOpenningStream = false;
	}
	else {
		LOG("[流媒体  ]请求的位号不存在,tag=" + tag);
	}
	return ret;
}


bool project::closeStream(string tag)
{
	bool ret = false;
	MP* pmp = prj.GetMPByTag(tag);
	if (pmp) {
		pmp->stopStreamPush();
		pmp->stopStreamPull(tag);
	}
	else {
		LOG("[流媒体  ]请求的位号不存在,tag=" + tag);
	}
	return ret;
}