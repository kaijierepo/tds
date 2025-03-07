#pragma once
#include "ioDev.h"
#include "proto_dlt645_2007.hpp"

class DLT645_2007_TRANSACTION {
public:
	TIME reqTime; //请求发出时间
	DLT645_2007_PKT* req;
	DLT645_2007_PKT* resp;
	mutex m_csResp;
	semaphore m_respSignal;  //收到响应的信号
	bool m_bGetResp;

	void setReq(DLT645_2007_PKT* pkt) {
		if (req)delete req;
		req = new DLT645_2007_PKT();
		*req = *pkt;
	}

	void setResp(DLT645_2007_PKT* pkt) {
		unique_lock<mutex> lock(m_csResp);
		m_bGetResp = true;
		if (resp)delete resp;
		resp = new DLT645_2007_PKT();
		*resp = *pkt;
		m_respSignal.notify();
	}

	bool getResp(DLT645_2007_PKT& pkt) {
		unique_lock<mutex> lock(m_csResp);
		if (resp)
		{
			pkt = *resp;
			return true;
		}
		else
		{
			return false;
		}
	}

	bool isTimeout()
	{
		if (timeopt::CalcTimePassSecond(reqTime) > 5)
			return true;
		return false;
	}

	void init() {
		m_bGetResp = false;
		if (req)delete req;
		if (resp) delete resp;
		req = nullptr; resp = nullptr;
	};
};

//对于读寄存器命令，modbus需要存储会话信息，知道目前读的起始寄存器是多少
class ioDev_dlt645_2007 : public ioDev
{
public:
	ioDev_dlt645_2007(void);
	~ioDev_dlt645_2007(void);

	//基本io设备管理
	void stop() override;
	bool loadConf(json& conf) override;
	bool toJson(json& conf, DEV_QUERIER querier) override;

	//通信会话
	virtual bool doTransaction(DLT645_2007_PKT& req, DLT645_2007_PKT& resp,string& errorInfo);
	DLT645_2007_TRANSACTION m_transaction;

	//轮询方案处理
	void generateAcqCmd();
	bool m_bCycleAcqThreadRunning;

	//TDS接口
	void output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync = true) override;

	bool isCommBusy() override;
	bool isConnected() override;
	json parseReadData(unsigned char* pD, int len, string fmt);
	void DoCycleTask();
	void DoCycleTaskSync() override;

	bool handleResp(DLT645_2007_PKT* resp);

	bool CommLock(int dwTimeoutMS = 0) override;
	void CommUnlock() override;

	//因为modbus设备先完成组包，确定地址，才能知道数据是属于当前设备的。因此字节流组包在ioDev对象外部完成。
	//设备不会收到该地址的包的分片，因此onRecvData无用，直接使用onRecvPkt
	bool onRecvPkt(unsigned char* pData, size_t iLen) override;
	bool onRecvData(unsigned char* pData, size_t iLen) override;
};


