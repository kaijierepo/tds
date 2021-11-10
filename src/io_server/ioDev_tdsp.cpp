#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_tdsp.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "commSrv.h"

using namespace httplib;


ioDev_tdsp::ioDev_tdsp()
{
	m_devType = TDS::IO_DEV_TYPE::DEV::iq60_gateway;
	m_devTypeLabel = IO_DEV_TYPE_LABEL.at(m_devType);
	m_parentDevType = "tds";
	m_channelType = "io-point";
	m_channelTypeLabel = "IO点";
	m_level = "device";
	m_iRpcId = 0;
}

ioDev_tdsp::~ioDev_tdsp()
{
}

void ioDev_tdsp::stop()
{
	m_bRunning = false;
	for (auto& i : m_mapSyncRPCInfo)
	{
		i.second->respSignal.notify();
	}
}

bool ioDev_tdsp::onRecvPkt(json jResp)
{
	std::unique_lock<mutex> lock(m_csSyncRPCInfo);
	try {
		if (jResp["id"] == nullptr) //主动上送命令
		{

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


bool ioDev_tdsp::sendData(char* pData, int iLen)
{
	unique_lock<mutex> lock(m_csIOSession);
	if (pIOSession)
	{
		pIOSession->send(pData, iLen);
		if (m_bEnableIoLog)
			statisOnSend((char*)pData, iLen, getIOAddrStr());
	}
	else
		return false;
	return true;
}

bool ioDev_tdsp::handleNotify(json& jNotify)
{
	


	return true;
}


bool ioDev_tdsp::call(string method, json params, json& result, json& error)
{
	json req;
	req["jsonrpc"] = "2.0";
	req["method"] = method;
	req["params"] = params;
	int iId = m_iRpcId;
	req["id"] = iId;
	m_iRpcId++;
	string strReq = req.dump() + "\n\n";
	sendStr(strReq);
	json resp;

	m_csSyncRPCInfo.lock();
	TDSP_SYNC_INFO* tsi = new TDSP_SYNC_INFO();
	m_mapSyncRPCInfo[iId] = tsi;
	m_csSyncRPCInfo.unlock();

	bool bGetResp = tsi->respSignal.wait_for(3000);
	
	
	m_csSyncRPCInfo.lock();
	resp = tsi->jResp;
	delete tsi;
	m_mapSyncRPCInfo.erase(iId);
	m_csSyncRPCInfo.unlock();

	if (!m_bRunning)
		return false;

	if (bGetResp)
	{
		if (resp["result"] != nullptr)
		{
			result = resp["result"];
		}
		if (resp["error"] != nullptr)
		{
			error = resp["error"];
		}
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

void ioDev_tdsp::DoCycleTask()
{
	json jChans = json::array();
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioChannel* pC = (ioChannel*) m_vecChild[i];
		jChans.push_back(pC->m_devAddr);
	}
	json params;
	params["ioAddr"] = jChans;
	json jRlt, jErr;
	call("acq", params, jRlt, jErr);

	if (jRlt != nullptr)
	{
		for (int i = 0; i < jRlt.size(); i++)
		{
			json jDE = jRlt[i];
			ioChannel* pC = getChan(jDE["ioAddr"].get<string>());
			if (pC)
				pC->input(jDE["val"]);
		}
	}
}
