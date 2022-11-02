#include "pch.h"
#include "masterDs.h"
#include "logger.h"
#include "prj.h"
#include "mp.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "tdsConf.h"
#include "tds.h"
#include "users/userMng.h"

MasterDs* pMasterDs = nullptr;

void MasterDs::statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		m_mutexChildTdsList.lock();
		m_vecChildTds[pTcpSess] = p;
		m_mutexChildTdsList.unlock();


		json jReq, jParam;
		jReq["method"] = "getObj";
		jParam["tag"] = "";
		jParam["getConf"] = true;
		jParam["getConfDetail"] = true;
		jParam["getMp"] = true;
		jParam["getChild"] = true;
		jParam["getStatus"] = true;
		jReq["params"] = jParam;
		jReq["id"] = m_rpcId;
		m_rpcId++;

		LOG("TDS子服务上线,%s:%d,查询子服务对象树配置", pTcpSess->remoteIP.c_str(), pTcpSess->remotePort);

		string s = jReq.dump();
		s += "\n\n";
		pTcpSess->send(s.data(), s.length());
	}
	else
	{
		m_mutexChildTdsList.lock();
		std::shared_ptr<TDS_SESSION> p = m_vecChildTds[pTcpSess];
		m_vecChildTds.erase(pTcpSess);
		m_mutexChildTdsList.unlock();
		//更新该session状态。等待其他零散指针引用销毁后自动删除
		p->onTcpDisconnect();
	}
}

void MasterDs::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	m_mutexChildTdsList.lock();
	std::shared_ptr<TDS_SESSION> ioSession = m_vecChildTds[pTcpSess];
	assert(ioSession != nullptr);
	m_mutexChildTdsList.unlock();
	OnRecvData((unsigned char*)pData, iLen, ioSession);
}

void MasterDs::OnRecvData(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> childSession)
{
	stream2pkt& tlBuf = childSession->m_tlBuf;
	tlBuf.PushStream((unsigned char*)pData, iLen);
	while (tlBuf.PopPkt(IsValidPkt_TDSP,false))
	{
		string s = str::fromBuff((char*)tlBuf.pkt, tlBuf.iPktLen);
		try {
			json pkt = json::parse(s);
			onRecvPkt(pkt, childSession);
		}
		catch (exception& e) {

		}
	}

}

bool MasterDs::handleAsynResp(json resp, std::shared_ptr<TDS_SESSION> childSession) {
	string method = resp["method"];

	if (method == "getObj") {
		json rlt = resp["result"];
		if (rlt.contains("lastModify") && rlt.contains("parentTag")) {
			//获取参数
			string parentTag = rlt["parentTag"];
			string tag = rlt["name"];
			tag = TAG::addRoot(tag, parentTag);
			childSession->m_childTdsTag = tag;
			string strLastModify = rlt["lastModify"];

			LOG("[主从服务]获取到子服务对象树配置,子服务位号:%s", tag.c_str());

			//如果上次修改时间和本地保存的一致，忽略
			//根据修改时间自动同步机制取消，统一改为手动设置
			OBJ* p = prj.queryObj(tag);
			//if (p) {
			//	string localLastModify = p->m_strLastModify;
			//	if (strLastModify == localLastModify){
			//		return;
			//	}
			//}

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
		}
		else {
			json rlt = resp["result"];
			project prjTmp;
			prjTmp.loadConf(rlt);
			prjTmp.m_rootTag = childSession->m_childTdsTag; //使得prjTmp	返回的tag都加上rootTag
			shared_lock<shared_mutex> lock(prj.m_csPrj);
			OBJ* pMO = prj.queryObj(childSession->m_childTdsTag);
			pMO->loadStatus(&prjTmp, true);
		}
	}
	//同步实时值
	else if (method == "getMp") {
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		OBJ* pMO = prj.queryObj(childSession->m_childTdsTag);
		json rlt = resp["result"];
		pMO->loadStatus(rlt);
	}

	return true;
}

bool MasterDs::handleNotify(json jResp, std::shared_ptr<TDS_SESSION> childSession) {
	return true;

}

void MasterDs::onRecvPkt(json& jResp, std::shared_ptr<TDS_SESSION> childSession)
{
	std::unique_lock<mutex> lock(m_csSyncRPCInfo);
	try {
		if (jResp["id"] == nullptr) //主动上送命令
		{
			handleNotify(jResp,childSession);
		}
		else
		{
			int id = jResp["id"].get<int>();
			if (m_mapSyncRPCInfo.find(id) != m_mapSyncRPCInfo.end())
			{
				RPC_SYNC_INFO* p = m_mapSyncRPCInfo[id];
				p->jResp = jResp;
				p->respSignal.notify();
			}
			else
			{
				handleAsynResp(jResp,childSession);
			}
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		string log = "tdsp device ,json parse error. " + errorType;
	}



	
}


bool MasterDs::doChildTdsTransaction(string childTdsTag,json& req, RPC_RESP& rpcResp, bool sync)
{
	return true;
}

bool MasterDs::needLog(string method) {
	if (method == "keepStream")
		return false;

	return true;
}

bool MasterDs::callChildTds(string childTds, string method, json params, json& rlt, json& err, bool sync)
{
	//找到childSession
	m_mutexChildTdsList.lock();
	std::shared_ptr<TDS_SESSION> ioSession = getSessionByTag(childTds);
	m_mutexChildTdsList.unlock();
	if (ioSession == nullptr) {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devOffline, "子服务离线"));
		return true;
	}


	json req;
	req["method"] = method;
	req["params"] = params;
	int iId = m_rpcId++;
	req["id"] = iId;
	string strReq = req.dump() + "\n\n";

	if(needLog(method))
		LOG("[子服务请求]\r\n" + strReq);



	//设置指定id命令的同步等待信息。
	//[注意] 必须先设置等待信息，再发送请求。本机release模式下配合模拟器调试。
	// 有可能还没运行到设置等待信息,就收到了响应，导致响应找不到匹配的请求。
	RPC_SYNC_INFO* tsi = nullptr;
	m_csSyncRPCInfo.lock();
	tsi = new RPC_SYNC_INFO();
	m_mapSyncRPCInfo[iId] = tsi;
	m_csSyncRPCInfo.unlock();
	//发送请求
	ioSession->sendStr(strReq);
	//等待请求
	bool bGetResp = tsi->respSignal.wait_for(3000);
	//删除同步信息
	m_csSyncRPCInfo.lock();
	json resp = tsi->jResp;
	delete tsi;
	m_mapSyncRPCInfo.erase(iId);
	m_csSyncRPCInfo.unlock();
	//处理响应
	if (bGetResp)
	{
		if (resp["result"] != nullptr) {
			rlt = resp["result"];
			if (needLog(method))
				LOG("[子服务响应]\r\n" + rlt.dump());
		}
		else if (resp["error"] != nullptr)
		{
			err = resp["error"];
			if (needLog(method))
				LOG("[子服务响应]\r\n" + err.dump());
		}
		else
		{
			LOG("[error][TDSP]TDSP响应数据包缺少result或者error字段");
		}
		return true;
	}
	else
	{
		err = json::parse(makeRPCError(RPC_ERROR_CODE::IO_reqTimeout, "子服务响应超时"));
	}

	return false;
}


//请求转发
bool MasterDs::rpc_childTdsDispatch(json& req, RPC_RESP& rpcResp, bool sync)
{
	string childTds = req["childTds"].get<string>();
	req.erase("childTds");
	json id = req["id"];

	json rlt, err;
	callChildTds(childTds,req["method"],req["params"],rlt,err,sync);

	if (rlt != nullptr)
		rpcResp.result = rlt.dump();
	if (err != nullptr)
		rpcResp.error = err.dump();
	return true;
}




void thread_masterDsWorkProc(MasterDs* p) {
	p->workingProc();
}

bool MasterDs::run()
{
	m_masterTdsPort = tds->conf->getInt("masterSrvPort", 700);
	if (m_masterTdsPort != 0) {
		m_tcpSrv = new tcpSrv();
		m_tcpSrv->run(this, m_masterTdsPort);
		thread t(thread_masterDsWorkProc, this);
		t.detach();
		LOG("[中心服务] 端口:%d,TDS子服务数据汇聚", m_masterTdsPort);
	}
	return false;
}

void MasterDs::stop()
{

}



void MasterDs::workingProc()
{
	int masterDataSyncInterval = tds->conf->getInt("masterDataSyncInterval", 2000);
	LOG("[主服务]数据同步周期,%d", masterDataSyncInterval);
	while (1) {
		Sleep(masterDataSyncInterval);
		json jReq,jParam;
		jReq["method"] = "getObj";
		jParam["tag"] = "";
		jParam["getConf"] = false;
		jParam["getStatus"] = true;
		jParam["getMp"] = true;
		jParam["getChild"] = true;
		jReq["params"] = jParam;
		jReq["id"] = m_rpcId;
		m_rpcId++;
		string s = jReq.dump();
		s += "\n\n";
		m_tcpSrv->SendData(s.data(), s.length());
	}
}

MasterDs::MasterDs()
{
	m_rpcId = 0;
}

MasterDs::~MasterDs()
{
}

std::shared_ptr<TDS_SESSION> MasterDs::getSessionByTag(string tag)
{
	for (auto& i : m_vecChildTds) {
		if (i.second->m_childTdsTag == tag)
			return i.second;
	}
	return nullptr;
}

string MasterDs::getChildTdsIP(string childTdsTag)
{
	m_mutexChildTdsList.lock();
	std::shared_ptr<TDS_SESSION> ioSession = getSessionByTag(childTdsTag);
	m_mutexChildTdsList.unlock();
	if (ioSession == nullptr) {
		return "";
	}
	return ioSession->ip;
}
