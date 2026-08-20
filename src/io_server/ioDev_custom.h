#ifndef TDS_IO_SERVER_IODEV_CUSTOM_H
#define TDS_IO_SERVER_IODEV_CUSTOM_H

#include "ioDev.h"
#include "tcpClt.h"
#include "tdsSession.h"
#include "json.hpp"
#include "scriptEngine.h"


class CUSTOM_PDU_TRANSACTION {
public:
	TIME reqTime; //请求发出时间
	vector<uint8_t> req;
	vector<uint8_t> resp;
	string req_adu;
	string resp_adu;
	mutex m_csResp;
	semaphore m_respSignal;  //收到响应的信号
	bool m_bGetResp;

	void setReq(vector<uint8_t> pkt) { //req不用加锁，因为doTransaction函数不能并发调用
		req = pkt;
	}

	void setResp(vector<uint8_t> pkt) { //请求线程 和 回调线程并发调用
		unique_lock<mutex> lock(m_csResp);
		m_bGetResp = true;
		resp = pkt;
		m_respSignal.notify();
	}

	bool getResp(vector<uint8_t>& pkt) {
		unique_lock<mutex> lock(m_csResp);
		pkt = resp;
		return true;
	}

	bool isTimeout()
	{
		if (timeopt::CalcTimePassSecond(reqTime) > 5)
			return true;
		return false;
	}

	void init() {
		m_bGetResp = false;
		req.clear();
		resp.clear();
	};
};

class ioDev_custom : public ioDev
{
public:
	ioDev_custom();
	~ioDev_custom();

	bool toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, DEV_QUERIER querier) override;

	CUSTOM_PDU_TRANSACTION m_transaction;
	bool handleDevRpcCall(json& jReq, RPC_RESP& rpcResp) override;
	bool doTransaction(vector<uint8_t> req, vector<uint8_t>& resp);
	void DoAcq();
	void doHttpHeartbeat();
	void DoCycleTask() override;
	void onEvent_online() override;
	bool onRecvDataNotify(unsigned char* pData, size_t iLen) override;
	bool onRecvData(unsigned char* pData, size_t iLen) override;
	void output(string chanAddr, json jVal, json& rlt, json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt, json& err, bool sync = true) override;
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;

	SCRIPT_RUN_INFO m_lastRunInfo_cycleAcq;
	SCRIPT_RUN_INFO m_lastRunInfo_output;
	SCRIPT_RUN_INFO m_lastRunInfo_onRecv;
};
#endif /* TDS_IO_SERVER_IODEV_CUSTOM_H */
