#include "pch.h"

#include "json.hpp"
#include "ioDev_tdsp.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "tAlmSrv.h"
#include "webSrv.h"

namespace ns_ioDev_tdsp {
	ioDev* createDev()
	{
		return new ioDev_tdsp();
	}
	class createReg{
	public:
		createReg() {
			mapDevCreateFunc["tdsp-device"] = createDev;
			mapDevTypeLabel["tdsp-device"] = "TDSP";
		};
	};
	createReg reg;
}




ioDev_tdsp::ioDev_tdsp()
{
	m_devType = "tdsp-device";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_iRpcId = 0;
	m_bAcqThreadRunning = false;
	m_childTdsHttpPort = 667;
	m_childTdsHttpsPort = 666;
	m_level = "devcie";
	m_childTdsUpgradeStatus = "未开始";
}

ioDev_tdsp::~ioDev_tdsp()
{
	stop();
}

void ioDev_tdsp::syncDataToMasterTds(yyjson_val* objTree, yyjson_doc* doc)
{
	if (m_devSubType == TDSP_SUB_TYPE::childTds) {
		if (m_strTagBind != "") {
			yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_val* yyv_objTree = yyjson_val_mut_copy(mdoc, objTree);
			yyjson_mut_val* yyv_rootTag_key = yyjson_mut_strcpy(mdoc, "rootTag");
			yyjson_mut_val* yyv_rootTag_val = yyjson_mut_strcpy(mdoc, m_strTagBind.c_str());
			yyjson_mut_obj_put(yyv_objTree, yyv_rootTag_key, yyv_rootTag_val);//子服务绑定到的主服务树节点

			string ss;

			size_t len;
			char* s = yyjson_mut_val_write(yyv_objTree, 0, &len);
			if (s) {
				ss = s;
				free(s);
			}
			
			tds->callAsyn("input", ss);
			yyjson_mut_doc_free(mdoc);
		}
	}
	else {
		json valList;
		getChanVal(valList);
		tds->callAsyn("input", valList);
	}
}


void ioDev_tdsp::stop()
{
	m_bRunning = false;
	//停止所有同步通信调用的突发线程
	for (auto& i : m_mapSyncRPCInfo)
	{
		i.second->respSignal.notify();
	}
	//停止周期采集的常驻线程
	while (m_bAcqThreadRunning)
	{
		timeopt::sleepMilli(1);
	}
}

void ioDev_tdsp::output(string chanAddr, json jVal, json& rlt,json& err, bool sync)
{
	json params;
	params[chanAddr] = jVal;
	call("output", params,nullptr, rlt, err);
}

void ioDev_tdsp::output(ioChannel* pC, json jVal, json& rlt, json& err, bool sync)
{
	json params;
	params[pC->getDevAddrStr()] = jVal;
	call("output", params, nullptr, rlt, err);
}

void ioDev_tdsp::handleAlarmStatusData(yyjson_val* yyv_alarmStatus) {
	string s;

	size_t len;
	char* p = yyjson_val_write(yyv_alarmStatus,0,&len);
	if (p) {
		s = p;
		free(p);
	}

	json alarmStatus = json::parse(s);
	string tagBind = getTagBind();

	json querier;
	querier["tag"] = tagBind;
	querier["isRecover"] = false;

	//当前有的报警，新的里面没有的，消除
	m_csAlmStatus.lock();
	for (auto& almCur : m_jAlarmStatus) {
		bool bIsAlarm = false;

		for (auto& almNew : alarmStatus) {
			if (almNew["type"] == almCur["type"]) {
				bIsAlarm = true;
			}
		}

		if (!bIsAlarm) {
			json jStatus;
			jStatus["tag"] = tagBind;
			jStatus["type"] = almCur["type"];
			jStatus["level"] = "normal";

			tds->callAsyn("updateAlarm", jStatus);
		}
	}
	m_csAlmStatus.unlock();

	//原来没有现在有的，产生报警
	for (auto& almNew : alarmStatus) {
		json jStatus;
		jStatus["tag"] = tagBind;
		jStatus["type"] = almNew["type"];
		jStatus["level"] = "alarm";

		tds->callAsyn("updateAlarm", jStatus);
	}
	
	m_csAlmStatus.lock();
	m_jAlarmStatus = alarmStatus;
	m_csAlmStatus.unlock();

	OBJ* pObj = prj.queryObj(tagBind,"zh");
	if (pObj) {
		pObj->m_mxAlarmStatus.lock();
		pObj->m_jAlarmStatus = alarmStatus;
		pObj->m_mxAlarmStatus.unlock();
	}

	timeopt::now(&m_stLastAlarmStatusTime);
	saveStatusBuff();
}


string getValTypeByJsonVal(json& jVal) {
	if (jVal.is_boolean()) {
		return VAL_TYPE::boolean;
	}
	else if (jVal.is_number_integer()) {
		return VAL_TYPE::integer;
	}
	else if (jVal.is_number_float()) {
		return VAL_TYPE::Float;
	}

	return "";
}

string getValTypeByJsonVal(yyjson_val* jVal) {
	if (yyjson_is_bool(jVal)) {
		return VAL_TYPE::boolean;
	}
	else if (yyjson_is_int(jVal)){
		return VAL_TYPE::integer;
	}
	else if (yyjson_is_real(jVal)){
		return VAL_TYPE::Float;
	}

	return "";
}

//猜测，不一定对
string getIOTypeByChanAddr(string addr) {
	if (addr.find("静音") != string::npos) {
		return CHAN_IO_TYPE::IO;
	}
	else if (addr.find("开关") != string::npos) {
		return CHAN_IO_TYPE::IO;
	}
	else if (addr.find("增益") != string::npos) {
		return CHAN_IO_TYPE::IO;
	}
	else if (addr.find("switch") != string::npos) {
		return CHAN_IO_TYPE::IO;
	}
	else if (addr.find("电源") != string::npos) {
		return CHAN_IO_TYPE::IO;
	}
	else {
		return CHAN_IO_TYPE::I;
	}
}

ioChannel* ioDev_tdsp::createChan(yyjson_val* jVal, string addr) {
	ioChannel* pC = new ioChannel();
	pC->m_valType = getValTypeByJsonVal(jVal);
	pC->m_ioType = getIOTypeByChanAddr(addr);
	pC->m_dispositionMode = DEV_DISPOSITION_MODE::spare;
	pC->m_jDevAddr = addr;
	pC->m_devAddr = pC->getDevAddrStr();
	return pC;
}

bool ioDev_tdsp::handle_AcqOrInput(yyjson_val* chanData, yyjson_doc* doc) {
	//缓存数据，但不一定是所有通道。后续优化
	//lock_conf_unique();
	//m_jAcq = chanData;
	//unlock_conf_unique();
	

	timeopt::now(&m_stLastChanDataTime);

	//更新数据到通道。并更新到绑定对象
	//列表输入
	if (yyjson_is_arr(chanData)) {
		DEV_CONF_LOCK_UNIQUE(this);

		size_t i, iMax;
		yyjson_val* yyvDe;
		yyjson_arr_foreach(chanData,i,iMax, yyvDe) {
			//格式为 [{"ioAddr":"voltage","val":25.1},{"ioAddr":"current","val":35.1}]
			//通道地址通过ioAddr来指定
			if (yyjson_is_obj(yyvDe)) {
				yyjson_val* yyv_addr = yyjson_obj_get(yyvDe, "chan");
				yyjson_val* yyv_tag  = yyjson_obj_get(yyvDe, "tag");

				if (yyv_addr == nullptr) {
					yyv_addr = yyjson_obj_get(yyvDe, "ioAddr"); //兼容老的命名方式
				}

				if (yyv_addr == nullptr) {
					yyv_addr = yyjson_obj_get(yyvDe, "addr"); //兼容老的命名方式
				}

				if (yyv_addr) {
					string addr = yyjson_get_str(yyv_addr);

					ioChannel* pC = getChanByDevAddr(addr);
					yyjson_val* yyv_val = yyjson_obj_get(yyvDe, "val");

					if (pC == nullptr) {
						pC = createChan(yyv_val, addr);
						addChannel(pC);
					}

					if (pC) {
						pC->input(yyv_val);
					}
				}
				else if (yyv_tag) {
					string s;

					char* p = yyjson_val_write(yyvDe, YYJSON_WRITE_NOFLAG, nullptr);
					if (p) {
						s = p;
						free(p);
					}

					json j = json::parse(s);

					string tag = yyjson_get_str(yyv_tag);
					tag = m_strTagBind + "." + tag;

					j["tag"] = tag;
					tds->callAsyn("input", j);
				}
			}
			//格式为 [1.3,1.2,2,3,true] ,数组序号就是通道号
			else if (yyjson_is_bool(yyvDe) || yyjson_is_num(yyvDe)) {
				string addr = str::format("%d", i);
				ioChannel* pC = getChanByDevAddr(addr);
				if (pC == nullptr) {
					pC = createChan(yyvDe, addr);
					addChannel(pC);
				}

				if (pC) {
					pC->input(yyvDe);
				}
			}
		}

		
	}
	//树形输入.设备本身也通过 树状位号模式来管理通道，直接转发到数据服务
	//子服务首次连接发送 acq命令，子服务 首次返回树形全部实时值
	else if (yyjson_is_obj(chanData)) {
		yyjson_val* yyv_name = yyjson_obj_get(chanData, "name");
		if (yyv_name != nullptr) {
			syncDataToMasterTds(chanData,doc);
		}
		//通道key,val模式输入
		else {
			DEV_CONF_LOCK_UNIQUE(this);

			size_t i,iMax;
			yyjson_val* key;
			yyjson_val* val;
			yyjson_obj_foreach(chanData,i,iMax,key,val){
				string chanAddr = yyjson_get_str(key);
				ioChannel* pC = getChanByDevAddr(chanAddr);

				if (pC == nullptr) {
					pC = createChan(val, chanAddr);
					addChannel(pC);
				}

				if (pC) {
					pC->input(val);
				}
			}

			
		}
	}

	return true;
}

bool ioDev_tdsp::handleDevRequest(yyjson_val* jRequest, yyjson_doc* doc) {
	yyjson_val* params = yyjson_obj_get(jRequest, "params");
	yyjson_val* yyv_method = yyjson_obj_get(jRequest, "method");
	yyjson_val* yyv_id = yyjson_obj_get(jRequest, "id");

	if (yyv_id == nullptr)
		return false;

	string sId;

	size_t len_id;
	char* p_id = yyjson_val_write(yyv_id, 0, &len_id);
	if (p_id) {
		sId = p_id;
		free(p_id);
	}
	
	string method;
	if (yyv_method)
		method = yyjson_get_str(yyv_method);

	yyjson_val* yyv_tag = yyjson_obj_get(params, "tag");
	if (yyv_tag) {
		string tag = yyjson_get_str(yyv_tag);
		string tagInMaster = TAG::addRoot(tag, m_strTagBind);

		yyjson_mut_doc* mdoc = yyjson_doc_mut_copy(doc, NULL);
		yyjson_mut_val* yymv_req = yyjson_mut_doc_get_root(mdoc);

		yyjson_mut_val* yymv_params = yyjson_mut_obj_get(yymv_req, "params");

		yyjson_mut_val* yymv_key = yyjson_mut_strcpy(mdoc, "tag");
		yyjson_mut_val* yymv_val = yyjson_mut_strcpy(mdoc, tagInMaster.c_str());

		yyjson_mut_obj_put(yymv_params, yymv_key, yymv_val);

		string sReq;

		size_t len;
		char* p = yyjson_mut_val_write(yymv_req, 0, &len);
		if (p) {
			sReq = p;
			free(p);
		}
		
		yyjson_mut_doc_free(mdoc);

		//转发到中心端的rpcHandler
		std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
		RPC_RESP rpcResp;
		rpcSrv.handleRpcCall(sReq, rpcResp, pSession);

		sendData((unsigned char*)rpcResp.strResp.c_str(), rpcResp.strResp.length());
	}

	return false;
}

bool ioDev_tdsp::handleSyncResp(yyjson_val* jResp, yyjson_val* yyv_id, yyjson_doc* doc) {
	int id = yyjson_get_int(yyv_id);
	if (m_mapSyncRPCInfo.find(id) != m_mapSyncRPCInfo.end()) {
		TDSP_SYNC_INFO* p = m_mapSyncRPCInfo[id];

		string str;

		size_t len;
		char* s = yyjson_val_write(jResp, 0, &len);
		if (s) {
			str = s;
			free(s);
		}

		p->strResp = str;
		p->respSignal.notify();

		return true;
	}
	return false;
}

bool ioDev_tdsp::handleAsynResp(yyjson_val* jResp,yyjson_doc* doc) {
	yyjson_val* rlt = yyjson_obj_get(jResp,"result");
	yyjson_val* err = yyjson_obj_get(jResp,"error");
	yyjson_val* yyv_method = yyjson_obj_get(jResp, "method");

	string method;
	if(yyv_method)
		method = yyjson_get_str(yyv_method);

	if (err != nullptr) {
		string str;

		size_t len = 0;
		char* s = yyjson_val_write(jResp, YYJSON_WRITE_NOFLAG, &len);
		if (s) {
			str = s;
			free(s);
		}
		
		LOG("[warn]TDSP设备,返回error\r\n" + str);
		return false;
	}
	
	if (rlt == nullptr) {
		LOG("[warn]TDSP设备,错误的tdsp响应数据包,没有返回result或error");
		return false;
	}

	bool handled = true;
	if (method == "acq") {
		handle_AcqOrInput(rlt,doc);
	}
	else if (method == "heartbeat")
	{
		yyjson_val* yyv_tdsVer = yyjson_obj_get(rlt, "tdsVersion");
		if (yyv_tdsVer) {
			string version = yyjson_get_str(yyv_tdsVer);
			if (m_tdsVersion != "" && version != m_tdsVersion) {
				m_childTdsUpgradeStatus = "升级成功! " + m_tdsVersion + "->" + version;
			}
			m_tdsVersion = version;
		}

		yyjson_val* yyv_serverStatus = yyjson_obj_get(rlt, "serverStatus");
		if (yyv_serverStatus) {
			yyjson_val* yyv_cpu = yyjson_obj_get(yyv_serverStatus, "cpu");
			if(yyv_cpu)
				m_tdsSrvStatus.cpu = yyjson_get_real(yyv_cpu);

			yyjson_val* yyv_mem = yyjson_obj_get(yyv_serverStatus, "mem");
			if(yyv_mem)
				m_tdsSrvStatus.mem = yyjson_get_real(yyv_mem);

			yyjson_val* yyv_handle = yyjson_obj_get(yyv_serverStatus, "handle");
			if(yyv_handle)
				m_tdsSrvStatus.handle = yyjson_get_int(yyv_handle);

			m_statusUpdateTime = timeopt::nowStr();
		}
	}
	else if (method == "getAlarmStatus")
	{
		handleAlarmStatusData(rlt);
	}
	else if (method == "getDevConf")
	{
		LOG("[warn]收到异步getDevConf");
	}
	else if (method == "getDevInfo") {
		yyjson_val* yyv_softVer = yyjson_obj_get(rlt, "softVer");
		if (yyv_softVer) {

		}
		//string sOld = m_jInfo.dump();
		//string sNew = rlt.dump();
		//DEV_CONF_LOCK_UNIQUE(this);
		//m_jInfo = rlt;
		//saveInfoBuff();
		//

		//if ( (sOld != sNew) && m_jInfo.contains("deviceType")) {
		//	string devType = m_jInfo["deviceType"];
		//	json tplData = ioSrv.getDevTemplate(devType);
		//	if (tplData != nullptr) {
		//		m_strChanTemplate = tplData["name"];
		//		json conf;
		//		conf["channels"] = tplData["channels"];
		//		loadConf(conf);
		//		json jDev;
		//		DEV_QUERIER query;
		//		toJson(jDev, query);
		//		rpcSrv.notify("devModified",jDev);
		//	}
		//}

		//if (m_strTagBind == "" && m_jInfo.contains("tagBind")) {
		//	m_strTagBind = m_jInfo["tagBind"];
		//}
	}
	else if (method == "getObj") {
		yyjson_val* yyv_parentTag = yyjson_obj_get(rlt, "parentTag");
		if (yyv_parentTag) { //响应当中包含了配置
			//获取参数
			string parentTag = yyjson_get_str(yyv_parentTag);
			yyjson_val* yyv_name = yyjson_obj_get(rlt, "name");
			string tag = yyjson_get_str(yyv_name);
			tag = TAG::addRoot(tag, parentTag);
			m_childTdsTag = tag;

			LOG("[主从服务]获取到子服务对象树配置,子服务位号:%s", tag.c_str());

			//如果上次修改时间和本地保存的一致，忽略
			//根据修改时间自动同步机制取消，统一改为手动设置
			OBJ* p = prj.queryObj(tag);


			//将最新子服务配置保存到本地
			unique_lock<shared_mutex> lock(prj.m_csPrj);
			if (!p) {
				LOG("[主从服务]主服务中未包含子服务对象,创建子服务对象树并保存到主服务");
				p = prj.createObjBranchByTag(tag);
				p->loadConf(rlt);
				p->m_bChildTds = true;
				p->m_bOnline = true;
				prj.saveConfFile();
			}
			else {
				p->m_bChildTds = true;
				p->m_bOnline = true;
			}

			if (p) {
				//project prjTmp;
				//prjTmp.loadConf(jRlt);
				//prjTmp.m_rootTag = m_childTdsTag; //使得prjTmp	返回的tag都加上rootTag
				//TIME stNow;
				//timeopt::now(&stNow);
				////此处不再保存到数据库，第3个参数需要重构掉
				//prjTmp.m_bOnline = true;//根节点就是子服务，当前在线
				p->loadStatus(rlt);
			}
		}
	}
	else {
		handled = false;
	}

	return handled;
}

bool ioDev_tdsp::onRecvData(unsigned char* pData, size_t iLen) {
	yyjson_doc* doc = yyjson_read((const char*)pData, iLen, 0);
	if (!doc) {
		LOG("[error]解析tdsp数据包失败,不是正确的json格式,%s:\n%s",getIOAddrStr().c_str(),pData);
		return false;
	}

	yyjson_val* yyv_resp = yyjson_doc_get_root(doc);
	onRecvPkt(yyv_resp, doc);

	yyjson_doc_free(doc);
	return false;
}

bool ioDev_tdsp::onRecvPkt(yyjson_val* jResp, yyjson_doc* doc) {
	setOnline();

	std::unique_lock<mutex> lock(m_csSyncRPCInfo);
	timeopt::now(&m_stLastActiveTime);

	try {
		if (isSingleTransaction()) { //同一时刻只有一个命令会话模式
			yyjson_val* yyv_method = yyjson_obj_get(jResp, "method");

			string method;
			if (yyv_method) {
				method = yyjson_get_str(yyv_method);
			}

			if (method == "input") {
				handleNotify(jResp,doc);
			}
			else{
				//SingleTransaction模式时m_mapSyncRPCInfo只有一个缓存会话
				auto iter = m_mapSyncRPCInfo.begin();
				if (iter != m_mapSyncRPCInfo.end()) {
					TDSP_SYNC_INFO* p = iter->second;

					string str;

					size_t len;
					char* s = yyjson_val_write(jResp,0,&len);
					if (s) {
						str = s;
						free(s);
					}

					p->strResp = str;
					p->respSignal.notify();
				}
				else {
					handleAsynResp(jResp,doc);
				}
			}
		}
		else {
			yyjson_val* yyv_id = yyjson_obj_get(jResp, "id");
			yyjson_val* yyv_params = yyjson_obj_get(jResp, "params");
			yyjson_val* yyv_result = yyjson_obj_get(jResp, "result");
			yyjson_val* yyv_err = yyjson_obj_get(jResp, "error");

			//接收到的设备数据共3种类型。
			//1.设备通知
			if (yyv_id == nullptr) {
				handleNotify(jResp,doc);
			}
			//2.设备请求
			else if (yyv_params != nullptr) {
				handleDevRequest(jResp, doc);
			}
			//3.设备响应
			else if (yyv_result != nullptr || yyv_err != nullptr) {
				//存在响应等待handler
				bool handled = handleSyncResp(jResp, yyv_id, doc);

				//不存在响应等待handler
				if(!handled) {//请求响应超时后或者网络延迟等情况，或者发送了rpc请求，但是没有等待处理响应
					handleAsynResp(jResp, doc);
				}
			}
		}
	}
	catch (std::exception& e) {
		string errorType = e.what();
		string log = "tdsp device ,json parse error. " + errorType;

		string str;

		size_t len;
		auto s = yyjson_val_write(jResp,0,&len);
		if (s) {
			str = s;
			free(s);
		}

		LOG("[warn]" + log + ",Pkt: " + str);
	}
	return true;
}

bool ioDev_tdsp::getCurrentVal()
{
	return false;
}


bool ioDev_tdsp::sendData(unsigned char* pData, size_t iLen)
{
	if (m_ioMode == "translator" && m_translatorProto != "") {
		vector<unsigned char> devPkt;
		string tdspPkt = (char*)pData;
		translateToDevPkt(tdspPkt, devPkt);
		return ioDev::sendData(devPkt.data(), devPkt.size());
	}
	else {
		return ioDev::sendData(pData, iLen);
	}
}


bool ioDev_tdsp::handleNotify(yyjson_val* jNotify, yyjson_doc* doc) {
	yyjson_val* yyv_method = yyjson_obj_get(jNotify, "method");

	string method;
	if (yyv_method) {
		method = yyjson_get_str(yyv_method);
	}

	yyjson_val* yyv_params = yyjson_obj_get(jNotify, "params");
	
	if (method == "devRegister") {
		if (yyv_params != nullptr) {
			yyjson_val* yyv_info = yyjson_obj_get(yyv_params, "info");

			yyjson_val* yyv_softVer = yyjson_obj_get(yyv_info, "softVer");
			if (yyv_softVer != nullptr){
				m_softVer = yyjson_get_str(yyv_softVer);
			}

			yyjson_val* yyv_hardVer = yyjson_obj_get(yyv_info, "hardVer"); 
			if (yyv_hardVer != nullptr) {
				m_hardVer = yyjson_get_str(yyv_hardVer);
			}

			yyjson_val* yyv_mfrDate = yyjson_obj_get(yyv_info, "mfrDate");
			if (yyv_mfrDate != nullptr) {
				m_mfrDate = yyjson_get_str(yyv_mfrDate);
			}

			yyjson_val* yyv_imei = yyjson_obj_get(yyv_info, "IMEI");
			if (yyv_imei != nullptr) {
				m_IMEI = yyjson_get_str(yyv_imei);
			}

			yyjson_val* yyv_httpPort = yyjson_obj_get(yyv_params, "httpPort");
			if (yyv_httpPort != nullptr) {
				m_childTdsHttpPort = yyjson_get_int(yyv_httpPort);
			}

			yyjson_val* yyv_httpsPort = yyjson_obj_get(yyv_params, "httpsPort");
			if (yyv_httpsPort != nullptr) {
				m_childTdsHttpsPort = yyjson_get_int(yyv_httpsPort);
			}

			yyjson_val* yyv_devSubType = yyjson_obj_get(yyv_params, "devSubType");
			if (yyv_devSubType != nullptr) {
				m_devSubType = yyjson_get_str(yyv_devSubType);
			}
		}

		triggerCycleAcq();
	}
	else if (method == "input" || method == "acq") {
		handle_AcqOrInput(yyv_params,doc);
	}
	else if (method == "onDataUpdate") {
		//位号增加上该子服务绑定的位号。
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			string sParams;

			size_t len;
			char* p = yyjson_val_write(yyv_params, 0, &len);
			if (p) {
				sParams = p;
				free(p);
			}

			json jParams = json::parse(sParams);
			if (jParams.is_array()) {
				for (auto& de : jParams) {
					de["rootTag"] = m_strTagBind;
				}
			}
			else if (jParams.is_object()) {
				jParams["rootTag"] = m_strTagBind;
			}

			string sp = jParams.dump();
			tds->callAsyn("input", sp);
		}
	}
	else if (method == "onServerUpgradeStatusChange") {
		yyjson_val* yyv_sus = yyjson_obj_get(yyv_params, "serverUpgradeStatus");
		if (yyv_sus) {
			m_childTdsUpgradeStatus = yyjson_get_str(yyv_sus);
		}
	}
	else if (method == "onAlarmUpdate") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			string sNotify;

			size_t len;
			char* p = yyjson_val_write(yyv_params, 0, &len);
			if (p) {
				sNotify = p;
				free(p);
			}

			json jParams = json::parse(sNotify);

			string sTag = jParams["tag"];
			sTag = TAG::addRoot(sTag, m_strTagBind);
			jParams["tag"] = sTag;

			string sDbPath;
			if (jParams.contains("dbPath")) {
				sDbPath = jParams["dbPath"];
			}

			RPC_RESP resp;
			almServer* pAlmSrv = NULL;

			if (sDbPath.find("alarms") != string::npos) {
				pAlmSrv = &almSrv;
			}
			else {
				pAlmSrv = &almSrv;
			}

			pAlmSrv->rpc_updateStatus(jParams, resp);
		}
	}
	else if (method == "onAlarmAdd") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			string sNotify;

			size_t len;
			char* p = yyjson_val_write(yyv_params, 0, &len);
			if (p) {
				sNotify = p;
				free(p);
			}

			json jParams = json::parse(sNotify);
			string sTag = jParams["tag"];

			string::size_type pos_s = sTag.find("(");
			if (pos_s != string::npos) {
				string::size_type pos_e = sTag.find(")");

				if (pos_e != string::npos) {
					string sIp = sTag.substr(0, pos_s);
					sTag = sTag.substr(pos_s + 1, pos_e - (pos_s + 1));
					sTag = TAG::addRoot(sTag, m_strTagBind);

					sTag = str::format("%s(%s)", sIp.c_str(), sTag.c_str());
				}
				else{
					sTag = TAG::addRoot(sTag, m_strTagBind);
				}
			}
			else{
				sTag = TAG::addRoot(sTag, m_strTagBind);
			}
			
			jParams["tag"] = sTag;
			string s = jParams.dump();
			yyjson_doc* doc = yyjson_read(s.c_str(), s.size(),0);
			yyjson_val* yyv_params = yyjson_doc_get_root(doc);
			RPC_RESP resp;
			almSrv.rpc_addAlarm(yyv_params, resp, false);
			yyjson_doc_free(doc);
		}
	}
	else if (method == "onObjOnline" || method == "objOnline") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			yyjson_val* yyv_tag = yyjson_obj_get(yyv_params, "tag");
			if (yyv_tag && m_strTagBind != "") {
				string tagChild = yyjson_get_str(yyv_tag);
				string tag = m_strTagBind + "." + tagChild;

				string sParams = str::format("{\"tag\":\"%s\"}", tag.c_str());
				tds->callAsyn("onObjOnline", sParams);
			}
		}
	}
	else if (method == "onObjOffline" || method == "objOffline") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			yyjson_val* yyv_tag = yyjson_obj_get(yyv_params, "tag");
			if (yyv_tag && m_strTagBind != "") {
				string tagChild = yyjson_get_str(yyv_tag);
				string tag = m_strTagBind + "." + tagChild;

				string sParams = str::format("{\"tag\":\"%s\"}", tag.c_str());
				tds->callAsyn("onObjOffline", sParams);
			}
		}
	}

	return true;
}

int ioDev_tdsp::getRpcId()
{
	std::unique_lock<mutex> lock(m_csRPCId);
	int id = m_iRpcId;
	m_iRpcId++;
	if (m_iRpcId > 250)
	{
		m_iRpcId = 0;
	}
	return id;
}

bool ioDev_tdsp::isSingleTransaction() {
	if (ioSrv.m_tdspSingleTransaction) {
		return true;
	}

	if (m_translatorProto != "") { //使用适配器时，该协议不支持id，因此不允许多请求并发
		return true;
	}

	return false;
}


void ioDev_tdsp::call(string method, json params, json sessionParams, json& result, json& error,  bool sync)
{
	if (!m_bRunning) {
		error = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devStopped, "device stopped"));
		return;
	}

	if (m_bIsWaitingResp)
	{
		error = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devBusy, "device busy"));
		return;
	}


	//通过连接是否存在来判断是否离线
	// tcp类型直连设备支持
	// adaptor下设备不支持
	//rs485下的tdsp设备不进行离线判断。 通过 isViaAdaptor过滤
	//其他情况统一认为 通过tcp连接
	if (!checkOnlineByTcpConn()) {
		error = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devOffline, "device offline"));
		return;
	}

	if (pIOSession != nullptr) {
		pIOSession->lastMethodCalled = method;
	}

	if (isSingleTransaction()) {
		CommLock();
	}

	json req;
	req["jsonrpc"] = "2.0";
	req["method"] = method;
	req["params"] = params;
	int iId = getRpcId();
	if (sync) {
		req["id"] = iId;
	}

	req["clientId"] = "tds";
	string ioAddr = getIOAddrStr();

	if (m_devSubType == TDSP_SUB_TYPE::childTds) {
		req["childTds"] = ioAddr;
	}
	else {
		req["ioAddr"] = ioAddr;
	}

	if (sessionParams != nullptr) {
		req.merge_patch(sessionParams);
	}

	string strReq = req.dump() + "\n\n";

	if (m_charset == "gb2312" || ioSrv.m_bGb2312Tdsp)
	{
		strReq = charCodec::utf8_to_gb(strReq);
	}

	if (!sync)
	{
		sendStr(strReq);
		result = "\"ok,asyn sended\"";
		goto TRANSACTION_END;
	}
	else
	{
		if (method == "setDevConf") 
		{
			m_jSettingConf = params;
		}

		//设置指定id命令的同步等待信息。
		//[注意] 必须先设置等待信息，再发送请求。本机release模式下配合模拟器调试。
		// 有可能还没运行到设置等待信息,就收到了响应，导致响应找不到匹配的请求。
		TDSP_SYNC_INFO* tsi = nullptr;
		//string sReq = req.dump();
		//sReq = sReq.substr(0, 80);
		//LOG("[io设备同步请求]\n" + sReq);
		m_csSyncRPCInfo.lock();
		tsi = new TDSP_SYNC_INFO();
		m_mapSyncRPCInfo[iId] = tsi;
		m_csSyncRPCInfo.unlock();
		//发送请求
		sendStr(strReq);
		
		//等待请求
		TIME startTime = timeopt::now();
		bool bGetResp = tsi->respSignal.wait_for(tds->conf->iotimeoutTdsp);  

		//删除同步信息
		string sResp;
		m_csSyncRPCInfo.lock();
		sResp = tsi->strResp;
		delete tsi;
		m_mapSyncRPCInfo.erase(iId);
		m_csSyncRPCInfo.unlock();

		time_t timeCost = timeopt::CalcTimePassMilliSecond(startTime);
		doRespTimeStatis(timeCost);

		//处理响应
		if (bGetResp && sResp != "")
		{
			json resp = json::parse(sResp);
			//LOG("[io设备同步响应]\n" + resp.dump());
			if (resp["result"] != nullptr)
			{
				result = resp["result"];
				//如果是获取配置命令，将配置存入缓存
				if (method == "getDevConf" && result.is_object())
				{
					lock_conf_unique();
					for (auto& [key, value] : result.items()) {
						m_jConf[key] = value;
					}
					saveConfBuff();
					unlock_conf_unique();
				}
				//手工发起的请求也更新数据
				else if (method == "acq" || method == "getAlarmStatus")
				{
					yyjson_doc* resp_doc = yyjson_read(sResp.c_str(), sResp.length(),0);
					yyjson_val* yyv_resp = yyjson_doc_get_root(resp_doc);
					handleAsynResp(yyv_resp,resp_doc);
					yyjson_doc_free(resp_doc);
				}
				else if (method == "setDevConf") //设置成功，更新到当前配置信息缓存
				{
					lock_conf_unique();
					for (auto& [key, val] : m_jSettingConf.items())
					{
						m_jConf[key] = val;
					}
					saveConfBuff();
					unlock_conf_unique();
					json jParams;
					jParams["ioAddr"] = getIOAddrStr();
					rpcSrv.notify("devConfBuffUpdated", jParams);
				}
				//else if (method == "getDevInfo")
				//{
				//	DEV_CONF_LOCK_UNIQUE(this);
				//	m_jInfo = result;
				//	saveInfoBuff();
				//	
				//}
				else if (method == "output")
				{
					string sParams = params.dump();
					yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
					yyjson_val* yyv_params = yyjson_doc_get_root(doc);
					if (yyjson_is_obj(yyv_params))
					{
						size_t i, imax;
						yyjson_val* key, * val;
						yyjson_obj_foreach(yyv_params,i,imax,key,val)
						{
							string ioAddr = yyjson_get_str(key);
							ioChannel* pC = getChanByDevAddr(ioAddr);
							if (pC)
							{
								pC->input(val);
							}
						}
					}
					else if (params.is_array())
					{
						size_t i, imax;
						yyjson_val* de;
						yyjson_arr_foreach(yyv_params, i, imax,de)
						{
							yyjson_val* yyv_ioAddr = yyjson_obj_get(yyv_params, "ioAddr");
							string ioAddr = yyjson_get_str(yyv_ioAddr);
							ioChannel* pC = getChanByDevAddr(ioAddr);
							if (pC)
							{
								yyjson_val* yyv_val = yyjson_obj_get(yyv_params, "val");
								pC->input(yyv_val);
							}
						}
					}
					yyjson_doc_free(doc);
				}
				else if (method == "startServerUpgrade") {
					m_childTdsUpgradeStatus = "正在升级";
				}
			}
			else if (resp["error"] != nullptr)
			{
				error = resp["error"];
				if (method == "startServerUpgrade") {
					m_childTdsUpgradeStatus = "错误:" + error["message"].dump();
				}
			}
			else {
				error = "device response has no error and result field";
			}
			goto TRANSACTION_END;
		}
		else {
			error = json::parse(makeRPCError(RPC_ERROR_CODE::IO_reqTimeout, "request time out"));
			setOffline(false,"请求未响应:" + req.dump());
			m_lastFailReq = strReq;
			m_lastFailReqTime = startTime.toStr(true);
			goto TRANSACTION_END;
		}
	}

TRANSACTION_END:
	if (isSingleTransaction()) {
		CommUnlock();
	}
	return;
}

void upgradeProcessThread(ioDev_tdsp* pDev, string firmwareFileName) {
	UPGRADE_INFO& ui = pDev->m_upgradeInfo;
	ui.isUpgrading = true;
	ui.stopUpgradeSignal = false;
	int retryCount = 0;

	//发送启动升级命令包
	json jp;
	jp["fileLen"] = ui.fileLen;
	jp["fileCrc"] = ui.fileCrc;
	jp["pktNum"] = ui.pktNum;
	jp["pktLen"] = ui.pktLen;
	jp["version"] = ui.version;
	jp["devType"] = ui.devType;
	json rlt, err;
	ui.grogressInfo = "请求设备启动升级";
	pDev->call("startUpgrade", jp, nullptr, rlt, err);

	if (err != nullptr) {
		ui.statusInfo = "请求设备启动升级失败," + err.dump();
		goto END;
	}


	ui.grogressInfo = "上传升级文件";
	//上传固件分包
	for (int i = 0; i < ui.pktNum; i++) {
		if (ui.stopUpgradeSignal) {
			LOG("[设备升级]升级流程停止,收到停止升级流程请求,设备地址:%s", pDev->getIOAddrStr().c_str());
			break;
		}
			

		unsigned char* pPktData = ui.fileData + ui.currentPktNo * ui.pktLen;

		json params;
		params["no"] = ui.currentPktNo;
		int pktLen = ui.pktLen;
		if (i == ui.pktNum - 1)//最后一包
		{
			if (ui.fileLen % ui.pktLen) {
				pktLen = ui.fileLen % ui.pktLen;
			}
		}
		params["len"] = pktLen;
		params["enc"] = "base64";
		char out[10000] = { 0 };
		base64_encode(pPktData, pktLen, out);
		string base64Data = out;
		params["data"] = base64Data;
		unsigned short crc = common::N_CRC16(pPktData, pktLen);
		params["crc"] = crc;
		json rlt, err;
		pDev->call("uploadFirmware", params, nullptr, rlt, err);
		
		if(rlt!=nullptr)
		{
			LOG("[设备升级]uploadFirmware成功,设备地址:%s,当前包%d/总包数%d", pDev->getIOAddrStr().c_str(),ui.currentPktNo+1,ui.pktNum);
			ui.currentPktNo++;
		}
		else {
			retryCount++;
			if (retryCount > 5)
			{
				LOG("[设备升级]升级失败,通信重试次数超限,设备地址:%s", pDev->getIOAddrStr().c_str());
				break;
			}
		}
	}

	if (ui.currentPktNo == ui.pktNum) {
		ui.statusInfo = "升级文件上传成功";
	}

END:

	ui.grogressInfo = "";
	ui.isUpgrading = false;
	ui.stopUpgradeSignal = false;
}

bool ioDev_tdsp::startUpgradeProcess(string firmwareFileName)
{
	thread t(upgradeProcessThread, this, firmwareFileName);
	t.detach();
	return true;
}

bool ioDev_tdsp::rpc_startUpgrade(string firmwareFileName, int pktLen, RPC_RESP& rpcResp)
{
	bool ret = false;
	if (isCommBusy()) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device busy,another command is requesting");
	}
	else if (!m_upgradeInfo.loadFirmwareFile(firmwareFileName, pktLen)) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "can not load specified firmware");
	}
	else if (m_upgradeInfo.devType.find(m_strChanTemplate) == string::npos) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device type is " + m_strChanTemplate + ",but firmware device type is " + m_upgradeInfo.devType);
	}
	else {
		json jp;
		UPGRADE_INFO& ui = m_upgradeInfo;
		jp["fileLen"] = ui.fileLen;
		jp["fileCrc"] = ui.fileCrc;
		jp["pktNum"] = ui.pktNum;
		jp["pktLen"] = ui.pktLen;
		jp["version"] = ui.version;
		jp["devType"] = ui.devType;
		json rlt, err;
		call("startUpgrade", jp, nullptr, rlt, err);
		if (rlt != nullptr) {
			rpcResp.result = jp.dump();
			ret = true;
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "start upgrade fail:" + err.dump());
		}
	}
	return ret;
}

bool ioDev_tdsp::stopUpgrade()
{
	return false;
}

bool ioDev_tdsp::isConnected()
{
	if (pIOSession != nullptr && pIOSession->isConnected())
	{
		return true;
	}
	return false;
}

json ioDev_tdsp::getAddr()
{
	json j;
	j["id"] = m_devAddr;
	return j;
}


void acq_work_thread(ioDev_tdsp* pDev) {
	pDev->m_bAcqThreadRunning = true; 


	if (pDev->m_acqMode == "group") {
		vector<string> groups;
		str::split(groups, pDev->m_chanGroup, ",");
		for (int i = 0; i < groups.size(); i++) {
			string groupName = groups[i];
			json params;
			params["group"] = groupName;
			json rlt, err;
			pDev->call("acq", params, nullptr, rlt, err);

			if (rlt != nullptr) {
				for (auto& [key, val] : rlt.items()) {
					pDev->m_jAcq[key] = val;
				}
			}
			else if (err != nullptr) {

			}
			else
				assert(false);



			if (pDev->m_bRunning == false)
				break;
		}
	}
	else if (pDev->m_acqMode == "single") {
		for (int i = 0; i < pDev->m_channels.size(); i++) {
			ioChannel* pC = pDev->m_channels[i];

			json params;
			params["ioAddr"] = pC->getDevAddrStr();
			json rlt, err;
			pDev->call("acq", params, nullptr, rlt, err);

			if (rlt != nullptr) {
				for (auto& [key, val] : rlt.items()) {
					pDev->m_jAcq[key] = val;
				}
			}

			if (pDev->m_bRunning == false)
				break;
		}
	}
	else if (pDev->m_acqMode == "all") {
		{
			json params;
			params["ioAddr"] = "*";
			json jRlt, jErr; 
			pDev->call("acq", params, nullptr, jRlt, jErr);
		}

		if (pDev->m_acqAlarm)
		{
			json jRlt, jErr;
			pDev->call("getAlarmStatus", nullptr, nullptr, jRlt, jErr);
		}
	}
	else {
	}
	pDev->m_bAcqThreadRunning = false;
}


void ioDev_tdsp::DoAcq()
{
	timeopt::now(&m_stLastAcqTime);

	if (m_bAcqThreadRunning == false) {
		thread t(acq_work_thread,this);
		t.detach();
	}
}

//该模式用于适配器模式串口网关下的设备
void ioDev_tdsp::DoCycleTaskSync() {
	if (!m_bRunning)
		return;

	if (m_upgradeInfo.isUpgrading)
		return;

	if (!m_bEnableAcq)
		return;
	if (m_dispositionMode != DEV_DISPOSITION_MODE::managed)
		return;

	DoAcq();
}


void ioDev_tdsp::DoCycleTask()
{
	if (m_bEnableOfflineTimeout && m_offlineTimeout >0) {
		long long inactiveTime = timeopt::CalcTimePassMilliSecond(m_stLastActiveTime);
		if (inactiveTime > m_offlineTimeout) {
			string s = str::format("%dms未收到数据,超时时间%dms", inactiveTime, m_offlineTimeout);
			setOffline(false,s);
		}
	}


	if (!m_bRunning)
		return;

	if (m_upgradeInfo.isUpgrading)
		return;

	//适配器模式下的TDSP不获取设备信息，一般适配器不实现该功能。
	if (ioSrv.m_tdspOnlineReq && m_bOnline && !isViaAdaptor() && !m_onlineInfoQueried && timeopt::CalcTimePassMilliSecond(m_stOnlineTime) > 1000) {
		json jRlt, jErr;
		call("getDevInfo", nullptr, nullptr, jRlt, jErr, false);
		m_onlineInfoQueried = true;
	}


	//子服务启用心跳.心跳不受轮询开关控制
	if (m_devSubType == TDSP_SUB_TYPE::childTds && timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > 5) {
		json rlt, err;
		call("heartbeat", nullptr, nullptr, rlt, err, false);
		m_stLastHeartbeatTime = timeopt::now();
	}


	if (!m_bEnableAcq)
		return;
	if (m_dispositionMode != DEV_DISPOSITION_MODE::managed)
		return;

	/*if (tds->conf->enableDevCommReboot)
	{
		int iPass = timeopt::CalcTimePassSecond(m_stLastActiveTime);
		if (iPass > tds->conf->devCommRebootTime)
		{
			json params = json::object();
			json jRlt, jErr;
			call("rebootComm", params, jRlt, jErr, false);
			logger.logInternal("[ioDev]重启设备通讯模块，ioAddr=" + getIOAddrStr() + ",tag=" + m_strTagBind);
			timeopt::now(&m_stLastActiveTime);
		}
	}


	if (tds->conf->enableDevReboot)
	{
		if (timeopt::CalcTimePassSecond(m_stLastActiveTime) > tds->conf->devRebootTime)
		{
			json params = json::object();
			json jRlt, jErr;
			call("rebootDev", params, jRlt, jErr, false);
			logger.logInternal("[ioDev]重启设备，ioAddr=" + getIOAddrStr() + ",tag=" + m_strTagBind);
			timeopt::now(&m_stLastActiveTime);
		}
	}*/


	if (m_fAcqInterval == 0 || timeopt::CalcTimePassSecond(m_stLastAcqTime) < m_fAcqInterval)
		return;

	//用于仅进行心跳通信,但是不进行采集的场景
	if (m_acqMode != "none") {
		DoAcq();
	}
}

void ioDev_tdsp::onEvent_online()
{
	/*if (m_devSubType == TDSP_SUB_TYPE::childTds) {
		LOG("TDS子服务上线,%s,查询子服务对象树配置", pIOSession->getRemoteAddr().c_str());

		json jReq, jParam;
		jReq["method"] = "getObj";
		jParam["tag"] = "";
		jParam["getConf"] = true;
		jParam["getConfDetail"] = true;
		jParam["getMp"] = true;
		jParam["getChild"] = true;
		jParam["getStatus"] = true;
		jReq["params"] = jParam;
		jReq["id"] = getRpcId();
		string s = jReq.dump();
		s += "\n\n";
		sendStr(s);
	}*/

	if (m_devSubType == TDSP_SUB_TYPE::childTds) {
		/*
			等效于如下查询，首次返回树形全部实时值
			OBJ_QUERIER query;
			query.getConf = false;
			query.getStatus = true;
			query.getChild = true;
			query.getMp = true;
			prj.toJson(j, query);
		*/
		string s = "{\"method\":\"acq\",\"params\":{},\"id\":" + str::fromInt(getRpcId()) + ",\"info\":\"do acq when get online\"}\n\n";
		sendStr(s);
	}
}

void ioDev_tdsp::onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo) {
	if (connInfo->bEnable == false) {
		return;
	}

	IOLogRecv(pData, len,connInfo->getRemoteAddr(),connInfo->getLocalAddr(), getIOAddrStr());

	if (m_translatorProto != "") { 	//使用协议转换器模式的tcp通信，不允许出现粘包和断包
		string tdspPkt;
		translateToTdspPkt((char*)pData, len, tdspPkt);

		if (tdspPkt.size() != 0) {
			onRecvData((unsigned char*)tdspPkt.c_str(), tdspPkt.size());
		}
	}
	else {
		stream2pkt* pab = &m_pab;
		pab->PushStream(pData, len);

		while (pab->PopPkt(IsValidPkt_TDSP)) {
			if (pab->abandonData != "") {
				string remoteAddr = connInfo->getRemoteAddr();
				LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);

				pIOSession->abandonLen += pab->iAbandonLen;
			}

			pIOSession->iALProto = pab->m_protocolType;
			onRecvData(pab->pkt, pab->iPktLen);
		}
	}
}

//translator通信数据包格式
//设备地址，协议类型，待转换数据包
//001122334455,alishan-power,192.168.0.110:6554,
//
#define TRANSLATOR_RECV_BUFF_LEN 10 * 1024 * 1024
int g_translatorSock = 0;
struct sockaddr_in servaddr;
char g_translatorRecvBuffer[TRANSLATOR_RECV_BUFF_LEN] = { 0 };
mutex g_csTranlator;
bool doTranslate(char* p,size_t len,string& sRecv) {
	bool ret = false;
	g_csTranlator.lock();
	if (g_translatorSock == 0) {
		int sockfd;
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		#ifdef _WIN32
		SetHandleInformation((HANDLE)sockfd, HANDLE_FLAG_INHERIT, 0);
		#else
		fcntl(sockfd, F_SETFD, FD_CLOEXEC);
		#endif

		// 设置超时时间为500毫秒
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(61111);
		servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
		g_translatorSock = sockfd;
	}
		
	sendto(g_translatorSock, (char*)p, len, 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
	
	int l = sizeof(servaddr);
	int recvLen = recvfrom(g_translatorSock, g_translatorRecvBuffer, TRANSLATOR_RECV_BUFF_LEN, 0, (struct sockaddr*)&servaddr, &l);

	if (recvLen < 0) {
		LOG("[error][协议转换器]请求协议转换超时，未收到响应,检查协议转换器是否启动,请求:\n%s",p);
	}
	else{
		g_translatorRecvBuffer[recvLen] = 0;
		sRecv = g_translatorRecvBuffer;
		ret = true;
	}
	g_csTranlator.unlock();

	return ret;
}


void ioDev_tdsp::translateToDevPkt(string& tdspPkt, vector<unsigned char>& devPkt)
{
	string s = str::removeChar(tdspPkt,'\n');
	s = s + "\n" + m_translatorProto + "\n" + getIOAddrStr();
	string sRecv;
	if (!doTranslate(s.data(), s.length(), sRecv)) {
		return;
	}
	if (sRecv.size() == 0) {
		LOG("[error][协议转换器]TDSP数据包->设备数据包 错误，返回空，协议转换器请求:\n" + s);
		return;
	}

	vector<string> vecRecv;
	str::split(vecRecv, sRecv, "\n");
	devPkt = str::hexStrToBytes(vecRecv[0]);  //协议转换器可以只返回数据包，不返回协议类型和
}


void ioDev_tdsp::translateToTdspPkt(char* devPkt,int len, string& tdspPkt)
{
	string s = str::bytesToHexStr(devPkt,len) + "\n" + m_translatorProto + "\n" + getIOAddrStr();
	string sRecv;
	if (!doTranslate(s.data(), s.length(), sRecv)) {
		return;
	}
	if (sRecv.size() == 0) {
		LOG("[error][协议转换器]设备数据包->TDSP数据包 错误，返回空，协议转换器请求:\n" + s);
		return;
	}
	vector<string> vecRecv;
	str::split(vecRecv, sRecv, "\n");
	tdspPkt = vecRecv[0];
}


int UPGRADE_INFO::calcPktNum(int pl)
{
	int pn = fileLen / pl;
	if (fileLen % pl)
		pn++;
	return pn;
}

bool UPGRADE_INFO::loadFirmwareFile(string fn, int pl)
{
	if (fileData) delete fileData;
	fileLen = 0;
	fileName = fn;
	binPath = fs::appPath() + "/files/firmware/" + fileName;
	if (fs::readFile(binPath, fileData, fileLen)) {
		pktLen = pl;
		pktNum = calcPktNum(pktLen);
		currentPktNo = 0;
		fileCrc = common::N_CRC16(fileData, fileLen);
		vector<string> infoList;
		str::split(infoList, fileName, "_");
		if (infoList.size() < 2) {
			return false;
		}
		version = infoList[1];
		devType = infoList[0];
		return true;
	}
	return false;
}



