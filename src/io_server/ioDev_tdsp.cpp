#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_tdsp.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "rpcHandler.h"
#include "as.h"

using namespace httplib;

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
}

ioDev_tdsp::~ioDev_tdsp()
{
	stop();
}

void ioDev_tdsp::syncDataToBindTag()
{
	if (m_devSubType == TDSP_SUB_TYPE::childTds) {
		if (m_jAcq != nullptr && m_strTagBind != "") {
			json params;
			params = m_jAcq;
			params["rootTag"] = m_strTagBind;
			tds->callAsyn("input", params);
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

void ioDev_tdsp::handleAlarmStatusData(json& alarmStatus)
{
	json querier;
	querier["tag"] = m_strTagBind;
	querier["isRecover"] = false;

	//当前有的报警，新的里面没有的，消除
	for (auto& almCur : m_jAlarmStatus)
	{
		bool bIsAlarm = false;
		for (auto& almNew : alarmStatus)
		{
			if (almNew["type"] == almCur["type"])
			{
				bIsAlarm = true;
			}
		}

		if (!bIsAlarm)
		{
			json jStatus;
			jStatus["tag"] = m_strTagBind;
			jStatus["type"] = almCur["type"];
			jStatus["level"] = "alarm";
			tds->callAsyn("updateAlarm", jStatus);
		}
	}


	//原来没有现在有的，产生报警
	for (auto& almNew : alarmStatus)
	{
		json jStatus;
		jStatus["tag"] = m_strTagBind;
		jStatus["type"] = almNew["type"];
		jStatus["level"] = "alarm";
		tds->callAsyn("updateAlarm", jStatus);
	}
	
	lock_conf_unique();
	m_jAlarmStatus = alarmStatus;
	unlock_conf_unique();

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

ioChannel* ioDev_tdsp::createChan(json& jVal, string addr) {
	ioChannel* pC = new ioChannel();
	pC->m_valType = getValTypeByJsonVal(jVal);
	pC->m_ioType = getIOTypeByChanAddr(addr);
	pC->m_dispositionMode = DEV_DISPOSITION_MODE::spare;
	pC->m_jDevAddr = addr;
	pC->m_devAddr = pC->getDevAddrStr();
	return pC;
}

bool ioDev_tdsp::handle_AcqOrInput(json chanData) {
	//缓存数据，但不一定是所有通道。后续优化
	lock_conf_unique();
	m_jAcq = chanData;
	unlock_conf_unique();
	timeopt::now(&m_stLastChanDataTime);


	//更新数据到通道。并更新到绑定对象
	//列表输入
	if (chanData.is_array())
	{
		lock_conf_unique();
		for (int i = 0; i < chanData.size(); i++)
		{
			json jDE = chanData[i];
			//格式为 [{"ioAddr":"voltage","val":25.1},{"ioAddr":"current","val":35.1}]
			//通道地址通过ioAddr来指定
			if (jDE.is_object()) {
				string addr = jDE["addr"].get<string>();
				ioChannel* pC = getChanByDevAddr(addr);
				json jVal = jDE["val"];

				if (pC == nullptr)
				{
					pC = createChan(jVal, addr);
					addChannel(pC);
				}

				if (pC)
					pC->input(jVal);
			}
			//格式为 [1.3,1.2,2,3,true] ,数组序号就是通道号
			else if (jDE.is_boolean() || jDE.is_number()) {
				string addr = str::format("%d", i);
				ioChannel* pC = getChanByDevAddr(addr);
				json jVal = jDE;
				if (pC == nullptr)
				{
					pC = createChan(jVal, addr);
					addChannel(pC);
				}

				if (pC)
					pC->input(jVal);
			}
		}
		unlock_conf_unique();
	}
	//树形输入.设备本身也通过 树状位号模式来管理通道，直接转发到数据服务
	//子服务首次连接发送 acq命令，子服务 首次返回树形全部实时值
	else if (chanData.contains("name")) {
		syncDataToBindTag();
	}
	//通道key,val模式输入
	else {
		lock_conf_unique();
		for (auto& [chanAddr, val] : chanData.items()) {
			
			ioChannel* pC = getChanByDevAddr(chanAddr);

			if (pC == nullptr)
			{
				pC = createChan(val, chanAddr);
				addChannel(pC);
			}

			if (pC)
				pC->input(val);
		}
		unlock_conf_unique();
	}
	return true;
}

bool ioDev_tdsp::handleAsynResp(json jResp)
{
	json rlt = jResp["result"];
	json err = jResp["error"];
	string method = jResp["method"].get<string>();
	if (err != nullptr)
	{
		LOG("[warn]TDSP设备,返回error\r\n" + jResp.dump());
		return false;
	}
	
	if (rlt == nullptr) {
		LOG("[warn]TDSP设备,错误的tdsp响应数据包,没有返回result或error");
		return false;
	}

	bool handled = true;
	if (method == "acq")
	{
		handle_AcqOrInput(rlt);
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
		string sOld = m_jInfo.dump();
		string sNew = rlt.dump();
		lock_conf_unique();
		m_jInfo = rlt;
		saveInfoBuff();
		unlock_conf_unique();

		if ( (sOld != sNew) && m_jInfo.contains("deviceType")) {
			string devType = m_jInfo["deviceType"];
			json tplData = ioSrv.getDevTemplate(devType);
			if (tplData != nullptr) {
				m_strChanTemplate = tplData["name"];
				json conf;
				conf["channels"] = tplData["channels"];
				loadConf(conf);
				json jDev;
				DEV_QUERIER query;
				toJson(jDev, query);
				rpcSrv.notify("devModified",jDev);
			}
		}

		if (m_strTagBind == "" && m_jInfo.contains("tagBind")) {
			m_strTagBind = m_jInfo["tagBind"];
		}
	}
	//else if (method == "getObj") {
	//	if (rlt.contains("parentTag")) { //响应当中包含了配置
	//		//获取参数
	//		string parentTag = rlt["parentTag"];
	//		string tag = rlt["name"];
	//		tag = TAG::addRoot(tag, parentTag);
	//		m_childTdsTag = tag;

	//		LOG("[主从服务]获取到子服务对象树配置,子服务位号:%s", tag.c_str());

	//		//如果上次修改时间和本地保存的一致，忽略
	//		//根据修改时间自动同步机制取消，统一改为手动设置
	//		OBJ* p = prj.queryObj(tag);


	//		//将最新子服务配置保存到本地
	//		unique_lock<shared_mutex> lock(prj.m_csPrj);
	//		if (!p) {
	//			LOG("[主从服务]主服务中未包含子服务对象,创建子服务对象树并保存到主服务");
	//			p = prj.createObjBranchByTag(tag);
	//			p->loadConf(rlt);
	//			p->m_bChildTds = true;
	//			p->m_bOnline = true;
	//			prj.saveConfFile();
	//		}
	//		else {
	//			p->m_bChildTds = true;
	//			p->m_bOnline = true;
	//		}

	//		if (p) {
	//			project prjTmp;
	//			prjTmp.loadConf(rlt);
	//			prjTmp.m_rootTag = m_childTdsTag; //使得prjTmp	返回的tag都加上rootTag
	//			TIME stNow;
	//			timeopt::now(&stNow);
	//			//此处不再保存到数据库，第3个参数需要重构掉
	//			prjTmp.m_bOnline = true;//根节点就是子服务，当前在线
	//			p->loadStatus(&prjTmp, &stNow, false);
	//		}
	//	}
	//	else { //响应当中仅包含实时数据,周期轮询得到的响应
	//		prj.loadObjTreeStatus(rlt, m_childTdsTag);
	//	}
	//}
	else {
		handled = false;
	}

	return handled;
}

bool ioDev_tdsp::onRecvPkt(json jResp)
{
	setOnline();
	std::unique_lock<mutex> lock(m_csSyncRPCInfo);
	timeopt::now(&m_stLastActiveTime);
	try {
		if (ioSrv.m_tdspSingleTransaction) {
			string method = jResp["method"];
			if (method == "input") {
				handleNotify(jResp);
			}
			else {
				auto iter = m_mapSyncRPCInfo.begin();
				if (iter != m_mapSyncRPCInfo.end())
				{
					TDSP_SYNC_INFO* p = iter->second;
					p->jResp = jResp;
					p->respSignal.notify();
				}
				else
				{
					handleAsynResp(jResp);
				}
			}
		}
		else {

			if (jResp["id"] == nullptr) //主动上送命令
			{
				handleNotify(jResp);
			}
			else
			{
				int id = jResp["id"].get<int>();
				if (m_mapSyncRPCInfo.find(id) != m_mapSyncRPCInfo.end())
				{
					TDSP_SYNC_INFO* p = m_mapSyncRPCInfo[id];
					p->jResp = jResp;
					p->respSignal.notify();
				}
				else
				{
					handleAsynResp(jResp);
				}
			}
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		string log = "tdsp device ,json parse error. " + errorType;
	}
	return true;
}				

bool ioDev_tdsp::getCurrentVal()
{
	return false;
}


bool ioDev_tdsp::sendData(unsigned char* pData, size_t iLen)
{
	return ioDev::sendData(pData, iLen);
}

bool ioDev_tdsp::handleNotify(json& jNotify)
{
	string method = jNotify["method"].get<string>();
	json jParams = jNotify["params"];
	
	if (method == "devRegister")
	{
		if (jParams != nullptr)
		{
			json jInfo = jParams["info"];
			if (jInfo["softVer"] != nullptr)
			{
				m_softVer = jInfo["softVer"].get<string>();
			}
			if (jInfo["hardVer"] != nullptr)
			{
				m_hardVer = jInfo["softVer"].get<string>();
			}
			if (jInfo["mfrDate"] != nullptr)
			{
				m_mfrDate = jInfo["mfrDate"].get<string>();
			}
			if (jInfo["IMEI"] != nullptr)
			{
				m_IMEI = jInfo["IMEI"].get<string>();
			}

			if (jParams["httpPort"] != nullptr)
			{
				m_childTdsHttpPort = jParams["httpPort"].get<int>();
			}
			if (jParams["httpsPort"] != nullptr)
			{
				m_childTdsHttpsPort = jParams["httpsPort"].get<int>();
			}
		}

		triggerCycleAcq();
	}
	else if (method == "input" || method == "acq")
	{
		handle_AcqOrInput(jParams);
	}
	else if (method == "notifyAlarmStatus")
	{
		handleAlarmStatusData(jParams);
	}
	else if (method == "statusUpdate") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			jParams["rootTag"] = m_strTagBind;
			RPC_RESP resp;
			RPC_SESSION session;
			rpcSrv.rpc_input(jParams, resp, session, FALSE);
		}
	}
	else if (method == "onUpdateAlarmStatus")
	{
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			string sTag = jParams["tag"];
			sTag = TAG::addRoot(sTag, m_strTagBind);
			jParams["tag"] = sTag;
			string sDbPath;
			if (jParams.contains("dbPath"))
			{
				sDbPath = jParams["dbPath"];
			}

			RPC_RESP resp;

			almServer* pAlmSrv = nullptr;

			if (sDbPath.find("alarms2") != string::npos)
			{
				pAlmSrv = &almSrv2;
			}
			else
			{
				pAlmSrv = &almSrv;
			}

			pAlmSrv->rpc_updateStatus(jParams, resp);
		}
	}
	else if (method == "alarmAdd")
	{
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			string sTag = jParams["tag"];
			sTag = TAG::addRoot(sTag, m_strTagBind);
			jParams["tag"] = sTag;
			string sDbPath;
			if (jParams.contains("dbPath"))
			{
				sDbPath = jParams["dbPath"];
			}

			RPC_RESP resp;

			almServer* pAlmSrv = nullptr;

			if (sDbPath.find("alarms2") != string::npos)
			{
				pAlmSrv = &almSrv2;
			}
			else
			{
				pAlmSrv = &almSrv;
			}

			pAlmSrv->rpc_addAlarm(jParams, resp, FALSE);
		}
	}
	else if (method == "objOnline") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			if (jParams.contains("tag") && m_strTagBind!= "") {
				string tagChild = jParams["tag"];
				string tag = m_strTagBind + "." + tagChild;
				jParams["tag"] = tag;
				tds->callAsyn("objOnline", jParams);
			}
		}
	}
	else if (method == "objOffline") {
		if (m_devSubType == TDSP_SUB_TYPE::childTds) {
			if (jParams.contains("tag") && m_strTagBind != "") {
				string tagChild = jParams["tag"];
				string tag = m_strTagBind + "." + tagChild;
				jParams["tag"] = tag;
				tds->callAsyn("objOffline", jParams);
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
	if (viaTcpConn()) {
		if (pIOSession == nullptr) {
			error = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devOffline, "device offline"));
			return;
		}
	}

	if (pIOSession != nullptr) {
		pIOSession->lastMethodCalled = method;
	}

	if (ioSrv.m_tdspSingleTransaction) {
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

	if (m_charset == "gb2312")
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
		m_csSyncRPCInfo.lock();
		json resp = tsi->jResp;
		delete tsi;
		m_mapSyncRPCInfo.erase(iId);
		m_csSyncRPCInfo.unlock();

		time_t timeCost = timeopt::CalcTimePassMilliSecond(startTime);
		doRespTimeStatis(timeCost);

		//处理响应
		if (bGetResp) 
		{
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
					handleAsynResp(resp);
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
				else if (method == "getDevInfo")
				{
					lock_conf_unique();
					m_jInfo = result;
					saveInfoBuff();
					unlock_conf_unique();
				}
				else if (method == "output")
				{
					if (params.is_object())
					{
						for (auto& i : params.items())
						{
							string ioAddr = i.key();
							ioChannel* pC = getChanByDevAddr(ioAddr);
							if (pC)
							{
								pC->input(i.value());
							}
						}
					}
					else if (params.is_array())
					{
						for (auto& i : params)
						{
							string ioAddr = i["ioAddr"].get<string>();
							ioChannel* pC = getChanByDevAddr(ioAddr);
							if (pC)
							{
								pC->input(i["val"]);
							}
						}
					}
				}
			}
			else if (resp["error"] != nullptr)
			{
				error = resp["error"];
			}
			else {
				error = "device response has no error and result field";
			}
			goto TRANSACTION_END;
		}
		else {
			error = json::parse(makeRPCError(RPC_ERROR_CODE::IO_reqTimeout, "request time out"));
			setOffline();
			goto TRANSACTION_END;
		}
	}

TRANSACTION_END:
	if (ioSrv.m_tdspSingleTransaction) {
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



void ioDev_tdsp::DoCycleTask()
{
	if (m_bEnableOfflineTimeout && m_offlineTimeout >0) {
		long long inactiveTime = timeopt::CalcTimePassMilliSecond(m_stLastActiveTime);
		if (inactiveTime > m_offlineTimeout) {
			setOffline();
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




	//如果从来没有收到过采集数据，加快采集频率
	if (m_jAcq == nullptr)
	{
		if (timeopt::CalcTimePassSecond(m_stLastAcqTime) < 15)
			return;
		else
			DoAcq();
	}
	else
	{
		if (m_fAcqInterval == 0 || timeopt::CalcTimePassSecond(m_stLastAcqTime) < m_fAcqInterval)
			return;
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
		json jReq, jParam;
		jReq["method"] = "acq";
		jReq["params"] = jParam;
		jReq["id"] = getRpcId();
		string s = jReq.dump();
		s += "\n\n";
		sendStr(s);
	}
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

