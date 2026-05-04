#include "pch.h"
#include "prj.h"
#include "tdb.h"
#include "ioSrv.h"
#include "obj.h"
#include "mp.h"
#include "logger.h"
#include "yyjson.h"
#include "rpcHandler.h"
#include "../video/RtspClient.h"

project prj;

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

bool project::saveConfFile() {
	yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* mut_root = yyjson_mut_doc_get_root(mut_doc);
	mut_root = yyjson_mut_obj(mut_doc);

	OBJ_QUERIER q;
	q.getConf = true;
	q.getChild = true;
	q.getMp = true;
	q.getStatus = false;
	q.getConfDetail = false;

	bool bSaved = false;
	toJson(mut_root, mut_doc, q);

	size_t len;
	char* p = yyjson_mut_val_write(mut_root, YYJSON_WRITE_PRETTY_NO_SPACES|YYJSON_WRITE_PRETTY, &len);
	if (len == 0) {
		LOG("[error]critical error,mo tree to json fail");
	}
	else {
		if (p) {
			bSaved = fs::writeFile(tds->conf->confPath + "/mo.json", p, len);
			free(p);
		}
	}
	
	yyjson_mut_doc_free(mut_doc);
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
			try
			{
				pct->tplData = json::parse(s);
				pct->obj.loadConf(pct->tplData);
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
	m_enableZLM = tds->conf->getInt("enableZLM", 0) != 0;
	MP* pmp = prj.GetMPByTag(tag, "zh");
	if (!pmp) {
		LOG("[流媒体] 请求的位号不存在, tag=" + tag);
		return false;
	}

	if (pmp->m_isOpenningStream) {
		LOG("[流媒体] 当前正在打开媒体源，收到重复打开请求，忽略, 位号:%s, 当前配置地址:%s",
			tag.c_str(), pmp->m_mediaUrl.c_str());
		return false;
	}

	pmp->m_isOpenningStream = true;

	// 如果拉流地址变更，先关闭旧流
	if (pmp->m_mpStatus.m_pullingSrcUrl != pmp->m_mediaUrl && pmp->m_mpStatus.m_pullingSrcUrl != "") {
		LOG("[流媒体] 监测到媒体源配置变更，先关闭拉流，当前拉流地址:%s, 配置地址:%s",
			pmp->m_mpStatus.m_pullingSrcUrl.c_str(), pmp->m_mediaUrl.c_str());

		closeStream(tag);
	}

	bool ret = false;
	if (m_enableZLM) {
		bool retPull = pmp->startStreamPull();   //zlm  addStreamProxy
		bool pushRet = false;
		if (pushTo != "") {
			if (retPull) {
				timeopt::sleepMilli(500);
				pushRet = pmp->startStreamPush(pushTo);  //zlm  addStreamPusherProxy
				LOG("[流媒体] 向上级服务推流（ZLM模式），url=%s", pushTo.c_str());
				ret = pushRet;
			}
		}
		else {
			ret = retPull;
		}
	}
	else {
		if (prj.getRtspClient(tag)) {
			LOG("[流媒体] RtspClient 已在运行 for tag: %s", tag.c_str());
			pmp->m_isOpenningStream = false;
			return true;
		}

		// 创建新的 RtspClient
		auto rtspClt = std::make_unique<RtspClient>();

		// 设置回调
		rtspClt->setFrameCallback([](const uint8_t* data, size_t size, uint32_t timestamp) {
				// 可以在这里处理帧，例如存档或分析
				});
		rtspClt->setStatusCallback([](RtspClient::State state, const std::string& msg) {
				LOG("[RtspClient] Status: %d - %s", static_cast<int>(state), msg.c_str());
				});
		rtspClt->setErrorCallback([](const std::string& error, int code) {
				LOG("[RtspClient] Error (%d): %s", code, error.c_str());
				});
		

		// 3. 配置 relay
		RtspClient::Config config;
		config.source_url = pmp->m_mediaUrl; // 源地址
		// 提取用户名和密码
		bool isSuccess = rtspClt->extractRtspAuthInfo(config);

		config.target_url = pushTo; // 目标地址
		config.retry_interval = 3000;
		config.max_retries = 0; // 无限重试
		config.rtp_timeout = 10000;

		// 4. 启动 relay
		LOG("[流媒体] 启动 RtspClient (内置模式)，源: %s, 目标: %s",
			config.source_url.c_str(), config.target_url.c_str());

		if (rtspClt->start(config)) {
			std::lock_guard<std::mutex> lock(m_relayMutex);
			m_mapRtspClients[tag] = std::move(rtspClt);
			ret = true;
		}
		else {
			LOG("[流媒体] 启动 RtspClient 失败 for tag: %s", tag.c_str());
			ret = false;
		}
	}

	pmp->m_isOpenningStream = false;
	return ret;
}

bool project::closeStream(string tag)
{
	if (m_enableZLM) {
		MP* pmp = prj.GetMPByTag(tag, "zh");
		if (pmp) {
			pmp->stopStreamPush();
			pmp->stopStreamPull(tag);
		}
	}
	else {
		// 再尝试关闭 RtspClient
		std::unique_ptr<RtspClient> relayToStop;
		{
			std::lock_guard<std::mutex> lock(m_relayMutex);
			auto it = m_mapRtspClients.find(tag);
			if (it != m_mapRtspClients.end()) {
				relayToStop = std::move(it->second);
				m_mapRtspClients.erase(it);
				LOG("[流媒体] 正在停止 RtspClient for tag: %s", tag.c_str());
			}
		}

		// 在锁外停止 relay，避免潜在死锁
		if (relayToStop) {
			relayToStop->stop();
			LOG("[流媒体] RtspClient 已停止 for tag: %s", tag.c_str());
		}
	}

	return true;
}

RtspClient* project::getRtspClient(std::string tag)
{
	std::lock_guard<std::mutex> lock(m_relayMutex);
	auto it = m_mapRtspClients.find(tag);
	if (it != m_mapRtspClients.end()) {
		return it->second.get();
	}
	return nullptr;
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
					unique_lock<shared_mutex> lock(prj.m_csPrj);
					LOCK_THREAD_RECORDER recorder(&prj.m_prjWriteLockThread, sys::getThreadId());

					pmo->loadConf(params, false);

					//持久化
					bool bSaved = prj.saveConfFile();
					if (!bSaved) {
						rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "save mo.json file fail; maybe file is set to readonly");
						LOG("[error]保存mo.json失败;检查该文件是否被设置成了只读属性");
						return;
					}

					std::map<string, SCRIPT_INFO> expScripts;
					prj.getAllVarExpScript();

					//数据服务自己缓存状态，并重新加载，此处不应从ioSrv同步数据，后续应当删除。
					//ioSrv.updateTag2IOAddrBinding();
					//ioSrv.updateAllChanVal();
					string sp = "{}";
					rpcSrv.notify("objTreeUpdated", sp);
					result = "\"ok\"";
				}
				//热重载
				else {
					pmo->loadConf(params);
					prj.saveConfFile();
					result = "\"ok\"";
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
		}
	}
}