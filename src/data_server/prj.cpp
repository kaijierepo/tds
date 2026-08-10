#include "pch.h"
#include "prj.h"
#include "database/tDatabase.h"
#include "ioSrv.h"
#include "obj.h"
#include "mp.h"
#include "logger.h"
#include "yyjson.h"
#include "rpcHandler.h"
#include "../video/StreamNode.h"
#include "../video/streamServer.h"
#include "mongoose.h"

project prj;

static long long getTick() {
	auto now = std::chrono::high_resolution_clock::now();
	auto microsec = std::chrono::time_point_cast<std::chrono::microseconds>(now);
	auto epoch = microsec.time_since_epoch();
	long long microseconds = epoch.count();
	return microseconds;
}

void g_getTagsByTagSelector(TAG_SELECTOR& tagSelector,SELECT_RLT& rlt) {
	prj.getTagsByTagSelector(tagSelector,rlt);
}

project::project()
{
	m_name = "tds";
	m_level = "root"; //level = root/ org/ mo/ mpgroup/ mp
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

bool project::setObjOnline(string tag) {
	// debug code
	//if (tag == "浙江.杭州.杭州607会议室.电源时序器") {
	//	LOG("[对象上线  ]位号:%s", tag.c_str());
	//}

	OBJ* p = prj.queryObj(tag);
	if (p) {
		if (p->m_bOnline == false) {
			p->m_bOnline = true;

			//直属监控点设置为在线
			if (p->m_strIoAddrBind != "") {
				p->setChildMpOnline();
			}

			string sParams = "{\"tag\":\"" + tag + "\"}";
			rpcSrv.notify("objOnline", sParams);
		}
		return true;
	}
	else {
		return false;
	}
}

bool project::setObjOffline(string tag) {
	// debug code
	//if (tag == "浙江.杭州.杭州607会议室.电源时序器") {
	//	LOG("[对象掉线  ]位号:%s", tag.c_str());
	//}

	OBJ* p = prj.queryObj(tag);
	if (p) {
		if (p->m_bOnline) {
			p->m_bOnline = false;

			if (p->m_strIoAddrBind != "") {
				p->setChildMpOffline();
			}

			//LOG("[对象掉线  ]位号:%s", tag.c_str());
			string sParams = "{\"tag\":\"" + tag + "\"}";
			rpcSrv.notify("onObjOffline", sParams);
		}

		if (p->m_bChildTds) { //设置所有子对象掉线
			p->recursiveSetOffline();
		}
		return true;
	}
	else {
		return false;
	}
}

json project::getTypeTagByTag(string tag)
{
	json typeTag;
	//根据相对位号的名字节点，查找子对象的名字，获取到对象
	vector<string> vecNames;
	str::split(vecNames, tag, ".");

	OBJ* toQuery = NULL;
	std::vector<OBJ*>* childMO = &m_childObj;
	bool findMO = false;
	for (int i = 0; i < vecNames.size(); i++)
	{
		string name = vecNames[i];
		bool findNode = false;
		for (int j = 0; j < childMO->size(); j++)
		{
			OBJ* tmp = childMO->at(j);
			string tmpName = tmp->m_name;
			if (tmpName == name)
			{
				toQuery = tmp;
				findNode = true;
				if (i == vecNames.size() - 1)
				{
					findMO = true;
				}
				break;
			}
		}

		if (findNode)
		{
			if (toQuery->m_type != "") {
				typeTag[toQuery->m_type] = name;
			}
			childMO = &toQuery->m_childObj;
		}
		else
		{
			break;
		}
	}

	return typeTag;
}

void thread_rt_data_save() {
	int interval = tds->conf->getInt("rtDBSaveInterval", 15);
	string path = tds->conf->dbPath + "/rtStatus.json";
	while (1) {
		timeopt::sleepMilli(interval * 1000);

		shared_lock<shared_mutex> lock(prj.m_csPrj);
		yyjson_mut_doc* md = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* mr = yyjson_mut_obj(md);
		prj.saveStatus(mr,md);
		size_t len;
		char* s = yyjson_mut_val_write(mr,0,&len);
		if (s) {
			fs::writeFile(path, s,len);
			free(s);
		}
		yyjson_mut_doc_free(md);
	}
}

void project::saveRtStatus()
{
}

void project::loadRtDB() {
	string path = tds->conf->dbPath + "/rtStatus.json";
	string s;
	fs::readFile(path, s);
	if (s != "") {
		try
		{
			yyjson_doc* d = yyjson_read(s.c_str(), s.size(), 0);
			yyjson_val* r = yyjson_doc_get_root(d);
			loadStatus(r);

			yyjson_doc_free(d);
		}
		catch (const std::exception& e)
		{
			string sErr = e.what();
			sErr = "加载" + path + "失败,错误信息:" + sErr;
			LOG(sErr);
		}
	}
}

void project::runRtDB()
{
	thread t(thread_rt_data_save);
	t.detach();
}

bool project::loadConfFile() {
	OBJ::m_bDefaultOnline = tds->conf->getInt("objDefaultOnline", 0) > 0 ? true:false;

	string& conf = m_moConfFileDump;
	if (!fs::readFile(tds->conf->confPath + "/mo.json", conf)) {
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

std::string project::serializeConf() {
	shared_lock<shared_mutex> lock(prj.m_csPrj); //与并发 getObj 读不互斥，仅在写锁持有者变更树时短暂等待
	yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* mut_root = yyjson_mut_doc_get_root(mut_doc);
	mut_root = yyjson_mut_obj(mut_doc);

	OBJ_PROP_SEL q;
	q.getConf = true;
	q.getChild = true;
	q.getMp = true;
	q.getStatus = false;
	q.getConfDetail = false;

	toJson(mut_root, mut_doc, q);

	size_t len = 0;
	char* p = yyjson_mut_val_write(mut_root, YYJSON_WRITE_PRETTY_NO_SPACES|YYJSON_WRITE_PRETTY, &len);
	std::string s;
	if (p != nullptr && len > 0) {
		s.assign(p, len);
	}
	free(p);
	yyjson_mut_doc_free(mut_doc);
	return s;
}

bool project::saveConfFile() {
	std::string s = serializeConf();
	if (s.empty()) {
		LOG("[error]critical error,mo tree to json fail");
		return false;
	}

	bool bSaved = fs::writeFile(tds->conf->confPath + "/mo.json", s);
	if (bSaved) {
		//保存成功后同步刷新整树缓存，避免 getObjTree 继续返回旧的 mo.json 内容。
		//否则前端需要重启 tds 才能看到最新配置。
		lock_guard<mutex> lock(m_csMoConfDump);
		m_moConfFileDump = s;
	}
	return bSaved;
}

bool project::loadConf(string& confStr) {
	//加载空配置
	if (confStr == "")
		return true;

	unique_lock<shared_mutex> lock(prj.m_csPrj);

	try {
		yyjson_doc* doc = yyjson_read(confStr.c_str(), confStr.size(), YYJSON_READ_NOFLAG);
		yyjson_val* root = yyjson_doc_get_root(doc);

		bool ret = loadConf(root, doc);

		yyjson_doc_free(doc);
		return ret;
	}
	catch (std::exception& e) {
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

bool project::loadConf(yyjson_val* conf, bool bCreate) {
	return OBJ::loadConf(conf, bCreate);
}

void project::clear()
{
	clearChildren();
}

void project::getMpTypeList(json& mpTypeList)
{
	map<string, MP*> mapAllMP;
	prj.getMpList(mapAllMP);
	map<string,string> mapTypes;
	for (map<string, MP*>::iterator it = mapAllMP.begin(); it != mapAllMP.end(); it++) {
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

vector<MP*> project::getAllEzvizMp()
{
	vector<MP*> ezvizMps;
	prj.m_csPrj.lock_shared();
	vector<MP*> mps;
	prj.getMpList(mps);

	for (int i = 0; i < mps.size(); i++) {
		MP* pmp = mps[i];
		if (pmp->m_serialNo == "" ||
			pmp->m_appKey == "" ||
			pmp->m_secret == "")
			continue;

		if (pmp->m_valType == "video" && pmp->m_mediaSrcType == "ezviz") {
			ezvizMps.push_back(pmp);
		}
	}
	prj.m_csPrj.unlock_shared();

	return ezvizMps;
}

bool project::rpc_getObjTemplate(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	yyjson_val* yyv_name = yyjson_obj_get(params, "name");
	if (yyv_name) {
		string type = yyjson_get_str(yyv_name);
		auto it = m_mapObjTempalte.find(type);
		if (it != m_mapObjTempalte.end()) {
			rpcResp.result = it->second->tplData;
		} else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::OBJ_templateNotFound, "object template not found");
		}
	} else {
		// 返回全部模板数组
		string s = "[";
		bool first = true;
		for (auto& iter : m_mapObjTempalte) {
			if (!first) s += ",";
			s += "{\"name\":\"" + iter.first + "\",\"data\":" + iter.second->tplData + "}";
			first = false;
		}
		s += "]";
		rpcResp.result = s;
	}
	return true;
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
			try
			{
				yyjson_doc* doc = yyjson_read(s.c_str(), s.size(), 0);
				if (doc) {
					pct->tplData = s;
					pct->obj.loadConf(yyjson_doc_get_root(doc), true);
					yyjson_doc_free(doc);
				}
				string type = str::trimSuffix(fi.name, ".json");
				m_mapObjTempalte[type] = pct;
			}
			catch (const std::exception&)
			{

			}
		}
	}
	return false;
}

bool project::rpc_setObjTemplate(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	OBJ_TEMPLATE* ct = new OBJ_TEMPLATE();

	yyjson_val* yyv_type = yyjson_obj_get(params, "type");
	yyjson_val* yyv_tplData = yyjson_obj_get(params, "tplData");
	if (!yyv_type || !yyv_tplData) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "type and tplData required");
		delete ct;
		return true;
	}

	ct->type = yyjson_get_str(yyv_type);

	// tplData: 将 yyjson_val* 序列化为字符串存入
	char* p = yyjson_val_write(yyv_tplData, 0, NULL);
	if (p) {
		ct->tplData = p;
		free(p);
	}

	yyjson_doc* doc = yyjson_read(ct->tplData.c_str(), ct->tplData.size(), 0);
	if (doc) {
		ct->obj.loadConf(yyjson_doc_get_root(doc), false);
		yyjson_doc_free(doc);
	}

	auto pOld = m_mapObjTempalte.find(ct->type);
	if (pOld != m_mapObjTempalte.end()) {
		delete pOld->second;
	}
	m_mapObjTempalte[ct->type] = ct;

	// 保存索引信息
	string confPath = tds->conf->confPath + "/template/object/conf.json";
	string sConf = "[";
	{
		bool first = true;
		for (auto& i : m_mapObjTempalte) {
			if (!first) sConf += ",";
			sConf += "{\"type\":\"" + i.second->type + "\"}";
			first = false;
		}
	}
	sConf += "]";
	fs::writeFile(confPath, sConf);

	string chanPath = tds->conf->confPath + "/template/object/";
	fs::writeFile(chanPath + "/" + ct->type + ".json", ct->tplData);

	rpcResp.result = RPC_OK;
	return true;
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

string getObjSelLevelByMethod(string method) {
	if (method == "getOrg") {
		return "org";
	}
	else if (method == "getCustomOrg") {
		return "org";
	}
	else if (method == "getMo") {
		return "mo";
	}
	else if (method == "getCustomMo") {
		return "mo";
	}
	else if (method == "getMp") {
		return "mp";
	}
	return "";
}


void project::getTagSel(TAG_SELECTOR& tagSel,string method, yyjson_val* params, RPC_SESSION& session) {
	//位号选择器 参数tag + rootTag
	//用户查询时 tag默认"",rootTag默认""
	//tag是相对于rootTag的相对位号
	//rootTag和tag组合出用户位号。
	//用户位号和用户组织结构组合成系统位号
	string rootTag = "";//查询根
	yyjson_val* yyv_rootTag = yyjson_obj_get(params, "rootTag");
	if (yyv_rootTag != nullptr && yyjson_is_str(yyv_rootTag)) { //获取子树
		rootTag = yyjson_get_str(yyv_rootTag);
	}
	rootTag = TAG::addRoot(rootTag, session.org);//组合为系统查询根

	//类型选择
	string type = ""; //为空表示选中所有，为*表示选中所有自定义类型
	yyjson_val* yyv_type = yyjson_obj_get(params, "type");
	if (yyv_type && yyjson_is_str(yyv_type)) {
		type = yyjson_get_str(yyv_type);
	}

	//层级选择
	//将getOrg,getMp,getMo统一转化为getObj
	string level = "*";
	yyjson_val* yyv_level = yyjson_obj_get(params, "level");
	string level_byMethod = getObjSelLevelByMethod(method);
	if (level_byMethod != "") {
		level = level_byMethod;
	}
	if (yyv_level && yyjson_is_str(yyv_level)) {
		level = yyjson_get_str(yyv_level);
	}

	//位号选择
	yyjson_val* yyv_tagSel = yyjson_obj_get(params, "tag");
	vector<string> vecTagSel;
	if (yyv_tagSel) {
		vecTagSel = parseTagSel(yyv_tagSel, type);
	}
	else {
		vecTagSel.push_back("");
	}


	tagSel.selLanguage = session.language;
	tagSel.init(vecTagSel, rootTag, type, level);
}

void project::getObjSel(OBJ_SELECTOR& objSel, string method, yyjson_val* params, RPC_SESSION& session) {
	yyjson_val* yyv_ioType = yyjson_obj_get(params, "ioType");
	if (yyv_ioType && yyjson_is_str(yyv_ioType)) {
		objSel.ioType = yyjson_get_str(yyv_ioType);
	}

	objSel.mode = "array";
	yyjson_val* yyv_mode = yyjson_obj_get(params, "mode");
	if (yyv_mode && yyjson_is_str(yyv_mode)) {
		objSel.mode = yyjson_get_str(yyv_mode);
	}

	//自定义编组
	objSel.group = ""; //为空表示选中所有，为*表示选中所有自定义编组
	yyjson_val* yyv_group = yyjson_obj_get(params, "group");
	if (yyv_group && yyjson_is_str(yyv_group)) {
		objSel.group = yyjson_get_str(yyv_group);
	}
}


bool project::handleRpc(string method, yyjson_val* params, RPC_RESP& resp, RPC_SESSION& session)
{
	bool handled = true;
	if (method == "setObj") {
		rpc_setObj(params, resp, session);
	}
	else if (method == "getObjTemplate") {
		rpc_getObjTemplate(params, resp, session);
	}
	else if (method == "setObjTemplate") {
		rpc_setObjTemplate(params, resp, session);
	}
	else if (method == "getObjTree") {
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		lock_guard<mutex> dumpLock(prj.m_csMoConfDump);
		resp.result = prj.m_moConfFileDump;
	}
	else if (method == "getObjGroups") {
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		set<string> groups;
		prj.getObjGroups(groups);
		json jGroups = json::array();
		string sGroups = "[";
		int i = 0;
		for (auto it = groups.begin(); it != groups.end(); ++it, ++i) {
			jGroups.push_back(*it);
			sGroups += "\"" + *it + "\"";
			if(i != groups.size() - 1) {
				sGroups += ",";
			}
		}
		sGroups += "]";
		resp.result = sGroups;
	}
	else if (method == "getMo" || method == "getOrg" || method == "getObj" || method == "getMp" || method == "getCustomOrg" || method == "getCustomMo") {
		//整树刷新(getMOTree: tag为空且获取子节点、不带状态/类型过滤)直接返回已缓存的
		//序列化结果，避免每次重新序列化整棵监控对象树(节点越多越慢)，导致前端刷新“卡死”。
		OBJ_PROP_SEL qFast = OBJ::parseQuerier(params);
		yyjson_val* yyv_tag = yyjson_obj_get(params, "tag");
		string fastTag = yyv_tag ? yyjson_get_str(yyv_tag) : "";
		if (qFast.getChild && qFast.leafType == "" && qFast.getStatus == false
			&& fastTag == "" && !prj.m_moConfFileDump.empty()
			&& (session.user == "admin" || session.user == "")) {
			shared_lock<shared_mutex> lock(prj.m_csPrj);
			lock_guard<mutex> dumpLock(prj.m_csMoConfDump);
			resp.result = prj.m_moConfFileDump;
			return true;
		}
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		session.tStartHandle = getTick();

		TAG_SELECTOR tagSel;
		OBJ_SELECTOR objSel;
		getTagSel(tagSel, method, params, session);
		getObjSel(objSel, method, params, session);
		vector<OBJ*> objList;
		prj.getObjByTagSelector(objList, tagSel);
		objList = filterByObjSel(objList, objSel);

		//多选模式
		if (!tagSel.singleSelMode()) {
			OBJ_PROP_SEL q = OBJ::parseQuerier(params);
			q.language = session.language;
			q.getTag = true; //多选模式，没有树结构，因此需要tag信息
			q.rootTag = tagSel.m_rootTag;   // replace to system root

			if (objSel.mode == "array") {
				yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
				yyjson_mut_val* rootRlt = yyjson_mut_arr(doc);

				for (int i = 0; i < objList.size(); i++) {
					OBJ* pObj = objList[i];

					yyjson_mut_val* rootObj = yyjson_mut_obj(doc);

					bool selectedByLeafType = false;
					if (pObj->toJson(rootObj, doc, q, &selectedByLeafType, session.user)) {
						yyjson_mut_arr_append(rootRlt, rootObj);
					}
				}

				resp.info = str::format("objCount=%d", objList.size());

				size_t len = 0;
				char* s = yyjson_mut_val_write(rootRlt, YYJSON_WRITE_NOFLAG, &len);
				if (s) {
					resp.result = s;
					free(s);
				}

				yyjson_mut_doc_free(doc);
			}
			else {
				yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
				yyjson_mut_val* rootRlt = yyjson_mut_obj(doc);

				for (int i = 0; i < objList.size(); i++) {
					OBJ* pObj = objList[i];

					yyjson_mut_val* rootObj = yyjson_mut_obj(doc);

					bool selectedByLeafType = false;
					if (pObj->toJson(rootObj, doc, q, &selectedByLeafType, session.user)) {
						string tag = yyjson_mut_get_str(yyjson_mut_obj_get(rootObj, "tag"));
						tag = str::replace(tag, ".", "_");
						yyjson_mut_val* key = yyjson_mut_strcpy(doc, tag.c_str());
						yyjson_mut_obj_put(rootRlt, key, rootObj);
					}
				}

				size_t len = 0;
				char* s = yyjson_mut_val_write(rootRlt, YYJSON_WRITE_NOFLAG, &len);
				if (s) {
					resp.result = s;
					free(s);
				}

				yyjson_mut_doc_free(doc);
			}
		}
		//精确查找模式，返回一个对象
		else if (objList.size() == 1) {
			OBJ* pmo = objList[0];

			//所有位号以用户位号的方式展示。除非另外指定rootTag
			yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_val* rootObj = yyjson_mut_obj(doc);

			OBJ_PROP_SEL q = OBJ::parseQuerier(params);
			q.pRoot = pmo;
			q.language = session.language;
			q.rootTag = tagSel.m_rootTag;   // replace to system root

			bool selectedByLeafType = false;
			if (pmo->toJson(rootObj, doc, q, &selectedByLeafType, session.user)) {
				size_t len = 0;
				char* s = yyjson_mut_val_write(rootObj, YYJSON_WRITE_NOFLAG, &len);
				if (s) {
					resp.result = s;
					free(s);
				}
			}

			yyjson_mut_doc_free(doc);
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "monitor object of specified tag not found");
		}
	}
	else {
		handled = false;
	}
	return handled;
}

void project::rpc_setObj(yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	session.tStartHandle = rpcSrv.getTick();
	string& result = rpcResp.result;

	if (yyjson_is_obj(params)) {
		if (!yyjson_obj_get(params, "tag")) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
		}
		else {
			string tag = yyjson_get_str(yyjson_obj_get(params, "tag"));

			string rootTag = "";
			if (yyjson_obj_get(params, "rootTag")) {
				rootTag = yyjson_get_str(yyjson_obj_get(params, "rootTag"));
			}

			tag = TAG::addRoot(tag, rootTag);
			tag = TAG::addRoot(tag, session.org);

			OBJ* pmo = prj.queryObj(tag, session.language);
			if (pmo) {
			//要修改树结构,冷重载。锁住对象锁
			if (yyjson_obj_get(params, "children")) {
				{
					unique_lock<shared_mutex> lock(prj.m_csPrj);
					LOCK_THREAD_RECORDER recorder(&prj.m_prjWriteLockThread, sys::getThreadId());

					pmo->loadConf(params, false);

					//更新变量表达式脚本表，需要在锁内遍历树
					prj.getAllVarExpScript();
				}

				//锁外：序列化 + 落盘 + 广播。
				//序列化(serializeConf)内部按 shared_lock 读取，与并发的 getObj 刷新读不互斥；
				//磁盘写与广播也不再持有写锁，避免大配置树下保存时阻塞前端刷新读导致“卡死”。
				std::string serializedMo = prj.serializeConf();
				bool bSaved = false;
				if (!serializedMo.empty()) {
					bSaved = fs::writeFile(tds->conf->confPath + "/mo.json", serializedMo);
					if (bSaved) {
						lock_guard<mutex> dumpLock(prj.m_csMoConfDump);
						prj.m_moConfFileDump = serializedMo;
					}
				}
				if (!bSaved) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "save mo.json file fail; maybe file is set to readonly");
					LOG("[error]保存mo.json失败;检查该文件是否被设置成了只读属性");
					return;
				}

				//数据服务自己缓存状态，并重新加载，此处不应从ioSrv同步数据，后续应当删除。
				//ioSrv.updateTag2IOAddrBinding();
				//ioSrv.updateAllChanVal();
				string sp = "{}";
				rpcSrv.notify("objTreeUpdated", sp);
				result = "\"ok\"";
				std::thread(updateStreamNodeConfig).detach();
			}
				//热重载
				else {
					pmo->loadConf(params);
					prj.saveConfFile();
					result = "\"ok\"";
					std::thread(updateStreamNodeConfig).detach();
				}
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, tag + " specified tag not found");
			}
		}
	}
	else if (yyjson_is_arr(params)) {
		bool ok = true;
		for (int i = 0; i < yyjson_get_len(params); i++) {
			yyjson_val* root_item = yyjson_arr_get(params, i);
			yyjson_val* yyv_tag = yyjson_obj_get(root_item, "tag");
			string tag;
			if (yyv_tag) {
				tag = yyjson_get_str(yyv_tag);
			}
			else {
				rpcResp.error = JSON_STR_VAL("param tag missing in set obj");
				return;
			}

			string rootTag = "";
			if (yyjson_obj_get(root_item, "rootTag")) {
				rootTag = yyjson_get_str(yyjson_obj_get(root_item, "rootTag"));
			}

			tag = TAG::addRoot(tag, rootTag);
			tag = TAG::addRoot(tag, session.org);

			OBJ* pmo = prj.queryObj(tag, session.language);
			if (pmo) {
				pmo->loadConf(root_item);
			}
			else {
				ok = false;
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, tag + " specified tag not found");
				break;
			}
		}

		if (ok) {
			prj.saveConfFile();
			result = "\"ok\"";
			std::thread(updateStreamNodeConfig).detach();
		}
	}
}


// ============================================================================
// ZLM 巡检线程 —— 对 readerCount > 0 的 ondemand 流下发 keepStream 保活
// ============================================================================

struct zlm_http_data {
	std::string head;
	std::string body;
	bool done = false;
	int status = 0;
};

static void zlm_http_cb(struct mg_connection* connect, int ev, void* ev_data) {
	zlm_http_data* data = (zlm_http_data*)connect->fn_data;
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;
		data->head.assign(hm->head.ptr, hm->head.len);
		data->body.assign(hm->body.ptr, hm->body.len);
		data->status = mg_http_status(hm);
		data->done = true;
		connect->is_closing = 1;
	}
	else if (ev == MG_EV_ERROR) {
		data->done = true;
		connect->is_closing = 1;
	}
}

static std::string zlm_url_encode(const std::string& str) {
	const char* in = str.c_str();
	size_t inLen = strlen(in);
	size_t outLen = 3 * inLen + 1;
	char* out = new char[outLen];
	size_t resultLen = mg_url_encode(in, inLen, out, outLen);
	std::string result(out, resultLen);
	delete[] out;
	return result;
}

void project::startZlmPoll() {
	m_zlmPollRunning_ = true;
	m_zlmPollThread_ = std::thread(&project::zlmPollLoop, this);
}

void project::stopZlmPoll() {
	m_zlmPollRunning_ = false;
	if (m_zlmPollThread_.joinable()) {
		m_zlmPollThread_.join();
	}
}

void project::zlmPollLoop() {
	while (m_zlmPollRunning_) {
		std::this_thread::sleep_for(std::chrono::seconds(30));
		if (!m_zlmPollRunning_) break;

		std::string mediaSrvIP = tds->conf->mediaSrvIP;
		if (mediaSrvIP.empty()) continue;

		std::string sPort = tds->conf->getStr("httpMediaPort", "669");
		std::string uri = "/index/api/getMediaList";
		std::string path = uri + "?secret=" + zlm_url_encode("Tds-666666");
		std::string url = "http://" + mediaSrvIP + ":" + sPort + path;

		struct mg_mgr mgr;
		mg_mgr_init(&mgr);

		zlm_http_data data;
		struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), zlm_http_cb, &data);
		if (connect) {
			mg_printf(connect,
				"GET %s HTTP/1.0\r\n"
				"Host: %s\r\n"
				"Connection: close\r\n"
				"\r\n",
				path.c_str(), mediaSrvIP.c_str()
			);

			TIME tStart = timeopt::now();
			while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
				mg_mgr_poll(&mgr, 100);
			}
		}
		mg_mgr_free(&mgr);

		if (data.status != 200) continue;

		yyjson_doc* doc = yyjson_read(data.body.c_str(), data.body.size(), 0);
		if (!doc) continue;

		yyjson_val* root = yyjson_doc_get_root(doc);
		if (!root) { yyjson_doc_free(doc); continue; }

		yyjson_val* yy_code = yyjson_obj_get(root, "code");
		if (!yy_code || yyjson_get_int(yy_code) != 0) { yyjson_doc_free(doc); continue; }

		yyjson_val* yy_data = yyjson_obj_get(root, "data");
		if (!yy_data || !yyjson_is_arr(yy_data)) { yyjson_doc_free(doc); continue; }

		size_t arr_size = yyjson_arr_size(yy_data);
		for (size_t i = 0; i < arr_size; i++) {
			yyjson_val* yy_item = yyjson_arr_get(yy_data, i);
			if (!yy_item) continue;

			yyjson_val* yy_readerCount = yyjson_obj_get(yy_item, "totalReaderCount");
			if (!yy_readerCount || yyjson_get_int(yy_readerCount) <= 0) continue;

			yyjson_val* yy_stream = yyjson_obj_get(yy_item, "stream");
			if (!yy_stream) continue;

			std::string tag = yyjson_get_str(yy_stream);

			// 只保活 ondemand 模式的流
			MP* pmp = GetMPByTag(tag, "zh");
			if (!pmp || pmp->m_srcStreamFetch != "ondemand") continue;

			// 路由：子服务 → 转发 keepStream；本地 → 直接刷新 idle timer
			ioDev* childTds = ioSrv.getOwnerChildTdsDev(tag);
			if (childTds) {
				std::string childTdsTag = childTds->m_strTagBind;
				std::string childTag = TAG::trimRoot(tag, childTdsTag);

				json params;
				params["tag"] = childTag;

				json childRlt, childErr;
				childTds->call("keepStream", params, json(), childRlt, childErr);
			}
		}
		yyjson_doc_free(doc);
	}
}

vector<string> project::parseTagSel(yyjson_val* tagSel, string& type) {
	vector<string> vec;
	if (tagSel == nullptr) { //位号未指定
		if (type == "")  //type未指定
		{
			vec.push_back(""); //选中根位号
		}
		else { //指定了某种自定义对象，认为是一种批量查找
			vec.push_back("*");
		}
	}
	else if (yyjson_is_str(tagSel)) {
		vec.push_back(yyjson_get_str(tagSel));
	}
	else if (yyjson_is_arr(tagSel)) {
		for (size_t i = 0; i < yyjson_arr_size(tagSel); i++) {
			yyjson_val* t = yyjson_arr_get(tagSel, i);
			if (yyjson_is_str(t)) {
				vec.push_back(yyjson_get_str(t));
			}
		}
	}

	return vec;
}