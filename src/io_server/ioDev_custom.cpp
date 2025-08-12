#include "pch.h"
#include "json.hpp"
#include "ioDev_custom.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "scriptEngine.h"
#include "scriptManager.h"
#include "scriptFunc.h"
#include "webSrv.h"



namespace ns_ioDev_custom {
	ioDev* createDev()
	{
		return new ioDev_custom();
	}
	class createReg{
	public:
		createReg() {
			mapDevCreateFunc["custom-device"] = createDev;
			mapDevTypeLabel["custom-device"] = "自定义设备";
		};
	};
	createReg reg;
}




ioDev_custom::ioDev_custom()
{
	m_devType = "custom-device";
	m_devTypeLabel = "自定义设备";
	m_level = "devcie";
}

ioDev_custom::~ioDev_custom()
{
	stop();
}

bool ioDev_custom::toJson(json& conf, DEV_QUERIER querier)
{
	ioDev::toJson(conf, querier);

	if (querier.getStatus) {
		json jRunInfo = json::object();
		if (m_cycleTaskScript != "") {
			yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_val* yyRuninfo = yyjson_mut_obj(doc);
			m_lastRunInfo_cycleAcq.toJson(doc, yyRuninfo);
			size_t len;
			char* p = yyjson_mut_val_write(yyRuninfo, 0, &len);
			if (p) {
				json j = json::parse(p);
				free(p);
				jRunInfo["cycleAcq"] = j;
			}
		}
		if (m_outputScript != "") {
			yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_val* yyRuninfo = yyjson_mut_obj(doc);
			m_lastRunInfo_output.toJson(doc, yyRuninfo);
			size_t len;
			char* p = yyjson_mut_val_write(yyRuninfo, 0, &len);
			if (p) {
				json j = json::parse(p);
				free(p);
				jRunInfo["output"] = j;
			}
		}
		if (m_onRecvScript != "") {
			yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_val* yyRuninfo = yyjson_mut_obj(doc);
			m_lastRunInfo_onRecv.toJson(doc, yyRuninfo);
			size_t len;
			char* p = yyjson_mut_val_write(yyRuninfo, 0, &len);
			if (p) {
				json j = json::parse(p);
				free(p);
				jRunInfo["onRecv"] = j;
			}
		}
	}

	return true;
}


bool ioDev_custom::doTransaction(vector<uint8_t> req, vector<uint8_t>& resp)
{
	if (m_bRunning == false)
		return false;


	while (1) {
		if (CommLock(1)) {
			break;
		}

		if (m_bRunning == false)
			return false;
	}

	TIME startTime = timeopt::now();
	bool ret = false;
	if (m_bRunning == false)
		goto TRANSACTION_END;


	m_bIsWaitingResp = true;
	m_transaction.init();
	m_transaction.setReq(req);

	m_transaction.m_respSignal.reset();

	if (!sendData(req.data(),req.size())) {
		goto TRANSACTION_END;
	}

	if (m_transaction.m_respSignal.wait_for(tds->conf->iotimeoutModbusRtu))
	{
		if (m_transaction.getResp(resp)) {	
			ret = true;
		}
		else {
			ret = false;
		}
	}
	else
	{
		setOffline();
	}

TRANSACTION_END:
	m_bIsWaitingResp = false;
	CommUnlock();

	if (ret) {
		time_t timeCost = timeopt::CalcTimePassMilliSecond(startTime);
		doRespTimeStatis(timeCost);
		m_transactionSuccessCount++;
		if (m_pParent) {
			m_pParent->m_transactionSuccessCount++;
			m_pParent->doRespTimeStatis(timeCost);
		}
	}
	else {
		m_transactionFailCount++;
		if (m_pParent) {
			m_pParent->m_transactionFailCount++;
		}
	}

	return ret;
}

void ioDev_custom::DoAcq()
{

}

void thread_do_http_heartbeat(ioDev_custom* pDev) {
	pDev->doHttpHeartbeat();
}

static void mg_connect_fn(struct mg_connection* connect, int ev, void* ev_data) {
	mg_http_data* data = (mg_http_data*)connect->fn_data;
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

void ioDev_custom::doHttpHeartbeat() {
	int nPort = m_jDevAddr["port"].get<int>();

	string ip = m_jDevAddr["ip"];
	string port = str::fromInt(nPort);
	string path = "";
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path.c_str(), ip.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	if (data.status == 200) {
		setOnline();
	}
	else {
		setOffline();
	}
}

void acq_thread_customDev(ioDev_custom* pDev) {
#ifdef ENABLE_QJS
	if (pDev->m_cycleTaskScript != "") {
		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		se.m_ioDevThis = pDev;

		SCRIPT_INFO si;
		scriptManager.getScript(pDev->m_cycleTaskScript, si);
		if (si.enable) {
			se.runScript(si.script, "", pDev->m_lastRunInfo_cycleAcq);

			if (se.m_sError != "") {
				LOG("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", pDev->m_cycleTaskScript.c_str(), se.m_sError.c_str(), pDev->getIOAddrStr().c_str());
			}
		}
	}

	pDev->m_bAcqThreadRunning = false;
#endif
}

void ioDev_custom::DoCycleTask()
{
	//if (timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > m_heartBeatInterval) {
	//	m_stLastHeartbeatTime = timeopt::now();
	//	if (m_bEnableHttpHeartbeat) {
	//		thread t(thread_do_http_heartbeat, this);
	//		t.detach();
	//	}
	//}

	if (!m_bEnableAcq)
		return;

	if (m_fAcqInterval == 0 || timeopt::CalcTimePassSecond(m_stLastAcqTime) < m_fAcqInterval)
		return;

	if (m_bAcqThreadRunning)
		return;

	m_stLastAcqTime.setNow();
	m_bAcqThreadRunning = true;
	thread t(acq_thread_customDev, this);
	t.detach();
}

void ioDev_custom::onEvent_online()
{
	ioDev::onEvent_online();
}

bool ioDev_custom::onRecvData(unsigned char* pData, size_t iLen)
{
	setOnline();

	if (m_onRecvScript != "") {
#ifdef _DEBUG
		LOG("执行脚本: " + m_onRecvScript);
#endif

		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		json jRecvData = json::array();
		for (int i = 0; i < iLen; i++) {
			int v = pData[i];
			jRecvData.push_back(v);
		}

		se.m_globalObj["RecvData"] = jRecvData;
		se.m_ioDevThis = this;

		SCRIPT_INFO si;
		scriptManager.getScript(m_onRecvScript, si);
		se.runScript(si.script, "",m_lastRunInfo_onRecv);

		if (se.m_sError != "") {
			string s = str::format("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", si.name.c_str(), se.m_sError.c_str(), getIOAddrStr().c_str());
			LOG(s);
		}
		else {
			
		}
	}

	vector<uint8_t> pkt;
	pkt.resize(iLen);
	memcpy(pkt.data(), pData, iLen);
	m_transaction.setResp(pkt);
	return false;
}

void ioDev_custom::output(string chanAddr, json jVal, json& rlt, json& err, bool sync)
{
	if (m_outputScript != "") {
		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		json jOutput;
		jOutput["val"] = jVal;
		jOutput["chan"] = chanAddr;
		se.m_globalObj["Output"] = jOutput;
		se.m_ioDevThis = this;

		LOG("[warn]执行自定义output脚本,val=%s,chan=%s,脚本=%s,设备=%s", jVal.dump().c_str(), chanAddr.c_str(), m_outputScript.c_str(), getIOAddrStr().c_str());

		SCRIPT_INFO si;
		scriptManager.getScript(m_outputScript, si);

		if (si.enable) {
			se.runScript(si.script, "", m_lastRunInfo_output);

			if (se.m_sError != "") {
				string s = str::format("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", m_outputScript.c_str(), se.m_sError.c_str(), getIOAddrStr().c_str());
				LOG(s);
				err = s;
			}
			else {
				if (se.m_scriptRet.is_object()) {
					if (se.m_scriptRet["result"] != nullptr) {
						rlt = "ok";

						ioChannel* pC = getChanByDevAddr(chanAddr);
						if (pC)
						{
							pC->input(jVal);
						}
					}
					else if (se.m_scriptRet["error"] != nullptr) {
						err = se.m_scriptRet["error"];
					}
					else {
						err = "未知错误,控制输出脚本未返回有效的错误信息";
					}
				}
				else {
					err = "控制输出脚本的返回信息必须是一个对象";
				}
			}
		}
	}
	else {
		err = "自定义设备未设置控制输出脚本";
	}
}

void ioDev_custom::output(ioChannel* pC, json jVal, json& rlt, json& err, bool sync) {
	string addr = pC->getDevAddrStr();
	output(addr, jVal, rlt, err, sync);
}

void ioDev_custom::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	ioDev::statusChange_tcpClt(connInfo, bIsConn);
}

bool ioDev_custom::handleDevRpcCall(json& jReq, RPC_RESP& rpcResp) {
	if (m_tdspScript != "") {
		ScriptEngine se;

#ifdef TDS
		se.m_initTdsFunc = initTdsFunc;
#endif

		se.m_globalObj["Req"] = jReq;
		se.m_ioDevThis = this;

		LOG("[warn]执行自定义Tdsp请求脚本,Req=%s,脚本=%s,设备=%s", jReq.dump().c_str(), m_tdspScript.c_str(), getIOAddrStr().c_str());

		SCRIPT_INFO si;
		scriptManager.getScript(m_tdspScript, si);
		se.runScript(si.script, "",si.lastRunInfo);
		scriptManager.setRunInfo(si.name, si.lastRunInfo);

		if (se.m_sError != "") {
			string s = str::format("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", m_tdspScript.c_str(), se.m_sError.c_str(), getIOAddrStr().c_str());
			LOG(s);
			rpcResp.error = s;
		}
		else {
			if (se.m_scriptRet.is_object()) {
				if (se.m_scriptRet["result"] != nullptr) {
					rpcResp.result = RPC_OK;
				}
				else if (se.m_scriptRet["error"] != nullptr) {
					rpcResp.error = se.m_scriptRet["error"].dump();
				}
				else {
					json j = "未知错误,控制输出脚本未返回有效的错误信息";
					rpcResp.error = j.dump();
				}
			}
			else {
				json j = "控制输出脚本的返回信息必须是一个对象";
				rpcResp.error = j.dump();
			}
		}
	}
	else {
		json j = "method not implemented";
		rpcResp.error = j.dump();
	}
	return true;
}




