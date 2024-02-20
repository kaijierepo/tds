#pragma once
#include "ioDev.h"
#include "proto_bacnet.hpp"
#include "udpSrv.h"

class BACNET_TRANSACTION {
public:
	TIME reqTime; //请求发出时间
	BACNET_PKT* req;
	BACNET_PKT* resp;
	mutex m_csResp;
	semaphore m_respSignal;  //收到响应的信号
	bool m_bGetResp;
	int m_transactionId;

	BACNET_TRANSACTION() {
		req = nullptr;
		resp = nullptr;
		m_transactionId = -1;
	}

	~BACNET_TRANSACTION() {
		if (req != nullptr)
			delete req;
		if (resp != nullptr)
			delete resp;
	}

	void setReq(BACNET_PKT* pkt) {
		if (req)delete req;
		req = new BACNET_PKT();
		*req = *pkt;

		if (pkt->transactionId >= 0) {
			m_transactionId = pkt->transactionId;
		}
	}

	void setResp(BACNET_PKT* pkt) {
		unique_lock<mutex> lock(m_csResp);
		m_bGetResp = true;
		if (resp)delete resp;
		resp = new BACNET_PKT();
		*resp = *pkt;
		m_respSignal.notify();
	}

	bool getResp(BACNET_PKT& pkt) {
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
class ioDev_bacnet : public ioDev
{
public:
	ioDev_bacnet(void);
	~ioDev_bacnet(void); 

	unsigned char sessionHandle[4];

	//基本io设备管理
	bool run() override;
	void stop() override;
	bool loadConf(json& conf) override;

	//通信会话
	virtual bool doTransaction(BACNET_PKT& req, BACNET_PKT& resp,string& errorInfo);
	BACNET_TRANSACTION m_transaction;

	//轮询方案处理
	bool m_bCycleAcqThreadRunning;

	unsigned char m_invokeId;

	//TDS接口
	void output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync = true) override;

	bool isCommBusy() override;
	bool isConnected() override;
	bool disconnect() override;
	void DoCycleTask();

	bool isEipSessionOpen();

	bool CommLock(int dwTimeoutMS = 0) override;
	void CommUnlock() override;


	void onEvent_online() override;
	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, string strIP, int port) override;

	//后续再改进为并发机制，怕有的设备硬件本身不并发，做成并发意义不大
	//map<int, BACNET_TRANSACTION*> m_mapCurrentTransaction;
	//mutex m_csCurrentTransaction;
};


class ioDev_searcher_bacnet : public IUdpServerCallBack
{
public:
	ioDev_searcher_bacnet();
	~ioDev_searcher_bacnet();

	void search();

	void OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, string strIP, int port) override;

	udpServer udpClt;
};