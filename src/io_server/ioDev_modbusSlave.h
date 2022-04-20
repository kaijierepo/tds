#pragma once
#include "ioDev.h"
#include "proto/proto_rtu.hpp"

class MB_IO_CMD : public PKT_DATA{
public:
	SYSTEMTIME stLastAcq;
	vector<ioChannel*> ioChannels; //该采集命令数据所对应的io通道
	unsigned char devAddr;
	unsigned char fCode;
	unsigned short offset;  //读写的寄存器或者线圈偏移
	unsigned short count;   //读写的寄存器或者线圈数量
	json jOuputVal;

	MB_IO_CMD() {
		timeopt::setAsTimeOrg(stLastAcq);
	}
	bool pack() override;
};

class ioDev_ModbusSlave : public ioDev
{
public:
	ioDev_ModbusSlave(void);
	~ioDev_ModbusSlave(void);

	void stop() override;

	void getIOTypeByMBDataType(ioChannel* p);

	bool loadConf(json& conf) override;
	bool output(string chanAddr, json jVal, json& jResp, bool sync = false) override;
	bool output(ioChannel* pC, json jVal, json& jResp, bool sync = false) override;

	bool isCommBusy() override;
	void sendIOCmd(MB_IO_CMD* cmd);
	void DoCycleTask();
	void SendAcqRTData();
    unsigned char getFCode(string regType);
	json getChanDataFromBuff(ioChannel* pC, int regOffsetOfData, char* pData, int len);
    json acqModbusReg(string regType, string regAddr, string storageFmt = STORAGE_FMT::UInt16, int regNum = 1);
	bool RequestAndWaitResponse(PKT_DATA& req,PKT_DATA& resp);
	bool sendData(char* pData,int iLen) override;
	bool OnRecvData(char* pData,int iLen) override;
	bool checkRespValication(char* pData, int iLen, string& errorInfo);
	bool onRecvPkt(char* pData, int iLen) override;
	unsigned char funcName2funcCode(string name);
	void generateAcqCmd();
	vector<MB_IO_CMD> chanList2MultiAcqCmd(vector<ioChannel*>& list);
	MB_IO_CMD chanList2IOCmd(vector<ioChannel*>& list);
	vector<MB_IO_CMD> m_vecAcqCmd; //周期采集命令模板
	vector<MB_IO_CMD*> m_vecIOTask;
	mutex m_csIOTask;
	void setCurrentIOCmd(MB_IO_CMD* p);
	void checkAcqReqTimeout() override;
	MB_IO_CMD* m_pCurrentIOCmd;
	stream2pkt m_recvBuff;
	mutex m_csRecvBuff;
	semaphore m_recvSignal;
};

