#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_custom.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "base64.h"
#include "mp.h"
#include "rpcHandler.h"
#include "scriptEngine.h"
#include "scriptManager.h"


using namespace httplib;

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


void ioDev_custom::DoAcq()
{

}

void thread_do_http_heartbeat(ioDev_custom* pDev) {
	pDev->doHttpHeartbeat();
}

void ioDev_custom::doHttpHeartbeat()
{
	string ip = m_jDevAddr["ip"];
	int port = m_jDevAddr["port"].get<int>();
	string addr = "http://" + ip + ":" + str::fromInt(port);
	httplib::Client cli(addr);
	auto res = cli.Get(m_httpHeartbeatUrl);

	if (res != nullptr) {
		setOnline();
	}
	else {
		setOffline();
	}
}

void acq_thread_customDev(ioDev_custom* pDev) {

	if (pDev->m_cycleTaskScript != "") {
		ScriptEngine se;
		se.m_initGlobalFunc = initGlobalFunc;
		se.m_initIODevFunc = initIODevFunc;
		se.m_ioDevThis = pDev;

		SCRIPT_INFO si;
		scriptManager.getScript(pDev->m_cycleTaskScript, si);
		se.runScript(si.script,"");

		if (se.m_sError != "") {
			LOG("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", pDev->m_cycleTaskScript.c_str(), se.m_sError.c_str(), pDev->getIOAddrStr().c_str());
		}
	}

	pDev->m_bAcqThreadRunning = false;
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
	thread t(acq_thread_customDev, this);
	t.detach();
}

void ioDev_custom::onEvent_online()
{
	
}

void ioDev_custom::output(string chanAddr, json jVal, json& rlt, json& err, bool sync)
{
	if (m_outputScript != "") {
		ScriptEngine se;
		se.m_initGlobalFunc = initGlobalFunc;
		se.m_initIODevFunc = initIODevFunc;
		json jOutput;
		jOutput["val"] = jVal;
		jOutput["chan"] = chanAddr;
		se.m_globalObj["output"] = jOutput;
		se.m_ioDevThis = this;

		LOG("[warn]执行自定义output脚本,val=%s,chan=%s,脚本=%s,设备=%s", jVal.dump().c_str(), chanAddr.c_str(), m_outputScript.c_str(), getIOAddrStr().c_str());

		SCRIPT_INFO si;
		scriptManager.getScript(m_outputScript, si);
		se.runScript(si.script, "");

		if (se.m_sError != "") {
			string s = str::format("[warn]脚本执行错误，脚本=%s,错误=%s,设备=%s", m_outputScript.c_str(), se.m_sError.c_str(), getIOAddrStr().c_str());
			LOG(s);
			err = s;
		}
		else {
			if (se.m_scriptRet.is_object()) {
				if (se.m_scriptRet["result"] != nullptr) {
					rlt = "ok";
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
}




