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
#include "alarm_server/as.h"

using namespace httplib;


ioDev_tdsp::ioDev_tdsp()
{
	m_devType = TDS::IO_DEV_TYPE::DEV::tdsp_device;
	m_devTypeLabel = getDevTypeLabel(m_devType);
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

bool ioDev_tdsp::handleAsynResp(json jResp)
{
	json rlt = jResp["result"];
	string method = jResp["method"].get<string>();
	if (rlt == nullptr)
	{
		LOG("TDSP设备,没有返回result字段");
		return false;
	}

	if (method == "acq")
	{
		for (int i = 0; i < rlt.size(); i++)
		{
			json jDE = rlt[i];
			ioChannel* pC = getChanByDevAddr(jDE["ioAddr"].get<string>());
			if (pC)
				pC->input(jDE["val"]);
		}

		m_csThis.lock();
		m_jAcq = rlt;
		m_csThis.unlock();

		GetLocalTime(&m_stLastChanDataTime);
	}
	else if (method == "getAlarmStatus")
	{
		json querier;
		querier["tag"] = m_strTagBind;
		querier["isRecover"] = false;
		vector<ALARM_INFO*> statusList = almSrv.tableCurrent.query(querier);

		//当前有的报警，新的里面没有的，消除
		for (int i = 0; i < statusList.size(); i++)
		{
			ALARM_INFO* pai  = statusList[i];

			bool bIsAlarm = false;
			for (int j = 0; j < rlt.size(); j++)
			{
				json jAlm = rlt[j];
				if (jAlm["type"].get<string>() == pai->type)
				{
					bIsAlarm = true;
				}
			}
	
			if (!bIsAlarm)
			{
				ALARM_INFO aiStatus;
				aiStatus.tag = m_strTagBind;
				aiStatus.type = pai->type;
				aiStatus.level = ALARM_LEVEL::normal;
				almSrv.Update(aiStatus);
			}
		}


		//原来没有现在有的，产生报警
		for (int j = 0; j < rlt.size(); j++)
		{
			json jAlm = rlt[j];
			ALARM_INFO aiStatus;
			aiStatus.tag = m_strTagBind;
			aiStatus.type = jAlm["type"].get<string>();
			aiStatus.level = ALARM_LEVEL::alarm;
			almSrv.Update(aiStatus);
		}


		m_csThis.lock();
		m_jAlarmStatus = rlt;
		m_csThis.unlock();

		GetLocalTime(&m_stLastAlarmStatusTime);
	}
	else if (method == "getDevConf")
	{
		LOG("[warn]收到异步getDevConf");
	}
}

bool ioDev_tdsp::onRecvPkt(json jResp)
{
	std::unique_lock<mutex> lock(m_csSyncRPCInfo);
	GetLocalTime(&m_stLastActiveTime);
	try {
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
	catch (std::exception& e)
	{
		string errorType = e.what();
		string log = "tdsp device ,json parse error. " + errorType;
	}

	if (m_bOnline == false)
	{
		m_bOnline = true;
		logger.logInternal("[ioDev]设备上线，ioAddr=" + getIOAddrStr());
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
			else if (jInfo["hardVer"] != nullptr)
			{
				m_softVer = jInfo["softVer"].get<string>();
			}
			else if (jInfo["mfrDate"] != nullptr)
			{
				m_mfrDate = jInfo["mfrDate"].get<string>();
			}
			else if (jInfo["IMEI"] != nullptr)
			{
				m_IMEI = jInfo["IMEI"].get<string>();
			}
		}
		

		triggerCycleAcq();
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


bool ioDev_tdsp::call(string method, json params, json& result, json& error, bool sync)
{
	if (!m_bRunning)
		return false;

	json req;
	req["jsonrpc"] = "2.0";
	req["method"] = method;
	req["params"] = params;
	int iId = getRpcId();
	req["id"] = iId;
	req["clientId"] = "tds";
	req["ioAddr"] = getIOAddrStr();
	string strReq = req.dump() + "\n\n";

	if (!sync)
	{
		return sendStr(strReq);
	}
	else
	{
		//设置指定id命令的同步等待信息。
		//[注意] 必须先设置等待信息，再发送请求。本机release模式下配合模拟器调试。
		// 有可能还没运行到设置等待信息,就收到了响应，导致响应找不到匹配的请求。
		TDSP_SYNC_INFO* tsi = nullptr;
		LOG("[io设备同步请求]\n" + req.dump());
		m_csSyncRPCInfo.lock();
		tsi = new TDSP_SYNC_INFO();
		m_mapSyncRPCInfo[iId] = tsi;
		m_csSyncRPCInfo.unlock();
		//发送请求
		sendStr(strReq);
		//等待请求
		bool bGetResp = tsi->respSignal.wait_for(tds->conf->iotimeoutTdsp);
		//删除同步信息
		m_csSyncRPCInfo.lock();
		json resp = tsi->jResp;
		delete tsi;
		m_mapSyncRPCInfo.erase(iId);
		m_csSyncRPCInfo.unlock();
		//处理响应
		if (bGetResp)
		{
			LOG("[io设备同步响应]\n" + resp.dump());
			if (resp["result"] != nullptr)
			{
				result = resp["result"];
				//如果是获取配置命令，将配置存入缓存
				if (method == "getDevConf" && result.is_object())
				{
					m_csThis.lock();
					for (auto& [key, value] : result.items()) {
						m_jConf[key] = value;
					}
					saveConfBuff();
					m_csThis.unlock();
				}
				//手工发起的请求也更新数据
				else if (method == "acq" || method == "getAlarmStatus")
				{
					handleAsynResp(resp);
				}
			}
			if (resp["error"] != nullptr)
			{
				error = resp["error"];
			}
			return true;
		}
		LOG("[io设备同步请求]请求超时,超时时间" + str::fromInt(tds->conf->iotimeoutTdsp) + "毫秒");
		return false;
	}
}

json ioDev_tdsp::getAddr()
{
	json j;
	j["id"] = m_devAddr;
	return j;
}

void ioDev_tdsp::DoAcq()
{
	GetLocalTime(&m_stLastAcqTime);

	{
		json params;
		params["ioAddr"] = "*";
		json jRlt, jErr;
		call("acq", params, jRlt, jErr, false);
	}

	{
		json jRlt, jErr;
		call("getAlarmStatus", nullptr, jRlt, jErr, false);
	}
}

void ioDev_tdsp::DoCycleTask()
{
	if (!m_bEnableAcq)
		return;
	if (m_dispositionMode != DEV_DISPOSITION_MODE::managed)
		return;

	if (tds->conf->enableDevCommReboot)
	{
		int iPass = timeopt::CalcTimePassSecond(m_stLastActiveTime);
		if (iPass > tds->conf->devCommRebootTime)
		{
			json params = json::object();
			json jRlt, jErr;
			call("rebootComm", params, jRlt, jErr, false);
			logger.logInternal("[ioDev]重启设备通讯模块，ioAddr=" + getIOAddrStr() + ",tag=" + m_strTagBind);
			GetLocalTime(&m_stLastActiveTime);
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
			GetLocalTime(&m_stLastActiveTime);
		}
	}


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
