#pragma once
#include "ioDev.h"
#include "proto_rtu.hpp"

class MB_PDU_TRANSACTION {
public:
	TIME reqTime; //请求发出时间
	MB_PDU* req;
	MB_PDU* resp;
	string req_adu;
	string resp_adu;
	mutex m_csResp;
	semaphore m_respSignal;  //收到响应的信号
	bool m_bGetResp;

	void setReq(MB_PDU* pkt) { //req不用加锁，因为doTransaction函数不能并发调用
		if (pkt == nullptr) {
			req = nullptr;
			return;
		}

		if (req)delete req;
		req = new MB_PDU();
		*req = *pkt;
	}

	void setResp(MB_PDU* pkt,string& aduResp) { //请求线程 和 回调线程并发调用
		unique_lock<mutex> lock(m_csResp);
		m_bGetResp = true;
		if (resp)delete resp;
		resp = new MB_PDU();
		*resp = *pkt;
		resp_adu = aduResp;
		m_respSignal.notify();
	}

	bool getResp(MB_PDU& pkt) {
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
class ioDev_ModbusSlave : public ioDev
{
public:
	ioDev_ModbusSlave(void);
	~ioDev_ModbusSlave(void);

	//基本io设备管理
	void stop() override;
	bool loadConf(json& conf) override;

	//通信会话
	virtual bool doTransaction(MB_PDU& req, MB_PDU& resp,string& errorInfo);
	virtual bool sendADU(MB_PDU& req) = 0;
	MB_PDU_TRANSACTION m_transaction;

	//轮询方案处理
	void generateAcqCmd();
	vector<PDU_REQ_read> chanList2MultiAcqCmd(vector<ioChannel*>& list);
	bool continuousChanInput2MBRead(vector<ioChannel*>& list, PDU_REQ_read& reqCmd);
	vector<PDU_REQ_read> m_vecAcqCmd; //轮询命令列表

	void getIOTypeByMBDataType(ioChannel* p);

	//TDS接口
	void output(string chanAddr, json jVal, json& rlt,json& err, bool sync = true) override;
	void output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync = true) override;


	void handle_pduResp_read(PDU_REQ_read* req, PDU_RESP_read* resp);

	bool isCommBusy() override;
	bool isConnected() override;
	void DoCycleTask() override;
	void DoCycleTaskSync() override;
    unsigned char getFCode(string regType);

	json getChanValFromRegBuff(ioChannel* pC, size_t regOffsetOfData, char* pData, size_t len);
	void setChanValToRegBuff(ioChannel* pC,json jVal,char* pRegData);


	bool checkRespValication(MB_PDU& pduReq, unsigned char* pData, size_t iLen, string& errorInfo);

	bool handleResp(MB_RTU_PKT* resp);

	bool CommLock(int dwTimeoutMS = 0) override;
	void CommUnlock() override;

	//因为modbus设备先完成组包，确定地址，才能知道数据是属于当前设备的。因此字节流组包在ioDev对象外部完成。
	//设备不会收到该地址的包的分片，因此onRecvData无用，直接使用onRecvPkt
	virtual bool onRecvPkt(unsigned char* pData, size_t iLen) override { return false; };
	bool onRecvNotify(unsigned char* pData, size_t iLen);
	bool onRecvData(unsigned char* pData, size_t iLen) override;
	unsigned char funcName2funcCode(string name);
};
void mbBuff2Local(char* pMB, char* pLocal, string byteOrder, int size);

