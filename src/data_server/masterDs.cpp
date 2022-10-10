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
		jParam["getConf"] = true;
		jParam["getConfDetail"] = true;
		jParam["getMp"] = true;
		jParam["getChild"] = true;
		jParam["getStatus"] = true;
		jReq["params"] = jParam;
		jReq["id"] = m_rpcId;
		m_rpcId++;

		LOG("TDS子服务上线,%s:%d", pTcpSess->remoteIP.c_str(), pTcpSess->remotePort);

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
		onRecvPkt(s, childSession);
	}

}

void MasterDs::onRecvPkt(string pkt, std::shared_ptr<TDS_SESSION> childSession)
{
	json resp = json::parse(pkt);
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

			//如果上次修改时间和本地保存的一致，忽略
			MO* p = prj.GetMOByTag(tag);
			if (p) {
				string localLastModify = p->m_strLastModify;
				if (strLastModify == localLastModify){
					return;
				}
			}

			//将最新子服务配置保存到本地
			unique_lock<shared_mutex> lock(prj.m_csPrj);
			if (!p) {
				p = prj.createObjBranchByTag(tag);
			}
			p->loadConf(rlt);
			p->m_bChildTds = true;
			p->m_bOnline = true;

			prj.saveConfFile();
		}
		else {
			json rlt = resp["result"];
			project prjTmp;
			prjTmp.loadConf(rlt);
			prjTmp.m_rootTag = childSession->m_childTdsTag; //使得prjTmp	返回的tag都加上rootTag
			shared_lock<shared_mutex> lock(prj.m_csPrj);
			MO* pMO = prj.GetMOByTag(childSession->m_childTdsTag);
			pMO->loadStatus(&prjTmp);
		}
	}
	//同步实时值
	else if(method == "getMp"){
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		MO* pMO = prj.GetMOByTag(childSession->m_childTdsTag);
		json rlt = resp["result"];
		pMO->loadStatus(rlt);
	}
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
	while (1) {
		Sleep(1000);
		json jReq,jParam;
		jReq["method"] = "getObj";
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
