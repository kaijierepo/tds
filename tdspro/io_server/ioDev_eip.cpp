#include "pch.h"
#include "ioDev_eip.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"
#include "webSrv.h"


ioDev* createDev_eip()
{
	return new ioDev_eip();
}

class createReg_eip {
public:
	createReg_eip();
};
createReg_eip::createReg_eip()
{
	mapDevCreateFunc["ethernet-ip"] = createDev_eip;
	mapDevTypeLabel["ethernet-ip"] = "EtherNet/IP";
}
createReg_eip regEip;


ioDev_eip::ioDev_eip(void)
{
	m_devType = "ethernet-ip";
	m_bCycleAcqThreadRunning = false;
	m_level = "device";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	memset(sessionHandle, 0, 4);
}


ioDev_eip::~ioDev_eip(void)
{
}

bool ioDev_eip::run()
{
	bool ret = ioDev::run();
	if (m_tcpClt) {
		m_tcpClt->m_pCallBackUser = this;
	}
	return ret;
}

void ioDev_eip::stop()
{
	m_bRunning = false;  //设为false以后，应当不会有新的线程尝试占用通信锁
	m_transaction.m_respSignal.notify(); //中断所有等待中的会话

	if(m_tcpClt)
		m_tcpClt->stop();

	//此处怎么设计找沈春种或者廖灰灰讨论一次。有没有基于信号量的方法
	while (m_bWorkingThreadRunning) {
		timeopt::sleepMilli(1);
	}
}



bool ioDev_eip::loadConf(json& conf)
{
	if (ioDev::loadConf(conf))
	{
		for (int i = 0; i < m_channels.size(); i++)
		{
			ioChannel* p = m_channels[i];

		}
		generateAcqCmd();
		run();
		return true;
	}
	else
	{
		run();
		return false;
	}
}

void ioDev_eip::output(string chanAddr, json jVal, json& rlt,json& err, bool sync)
{
	ioChannel* pC = getChanByDevAddr(chanAddr);
	if (pC)
		output(pC, jVal, rlt,err, sync);
	else
		return ;
}

void ioDev_eip::output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync)
{
	string chanAddr = pC->getDevAddrStr();

	string errorInfo;
	EIP_PKT req, resp;
	if (m_devSubType == DEV_SUB_TYPE::ethernetIP::control_logix) {
		unsigned char slot = 0;
		if (m_jDevAddr.contains("slot")) {
			slot = m_jDevAddr["slot"].get<int>();
		}
		//req.pack_LOG_writeTag(pC->getDevAddrStr(),sessionHandle, slot);
	}
	else if (m_devSubType == DEV_SUB_TYPE::ethernetIP::micro800) {
		TAG_VAL tagVal;
		unsigned short tagType;
		if (pC->m_valType == "BOOL") {
			tagVal.BOOL_val = jVal.get<bool>() ? 0x01 : 0x00;
			tagType = LOGIX_TAG_VAL_TYPE::Bool;
		}
		else if (pC->m_valType == "SINT") {

		}


		req.pack_Micro800_writeTag(pC->getDevAddrStr(), sessionHandle, tagType, tagVal);
	}

	if (doTransaction(req, resp, errorInfo)) {
		if (errorInfo == "") {
			CIP_Response cipResp;
			resp.unpack_Micro800_writeTag(cipResp);
			bool validResp = false;
			if (cipResp.general_status == 0) {
				validResp = true;
			}
			else {
				errorInfo = getWriteTagServiceErrorCodeDesc(cipResp.general_status,cipResp.additional_status).c_str();
				LOG("[warn]EIP设备writeTag设备返回失败,%s", errorInfo);
			}
		}
	}
	else {
		errorInfo = "timeout";
	}

	if (errorInfo!="") {
		err = errorInfo;
	}
	else {
		rlt = "ok";
	}
}

bool ioDev_eip::isCommBusy()
{
	assert(m_pParent);
	return m_pParent->isCommBusy();
}

bool ioDev_eip::isConnected()
{
	if (m_tcpClt) {
		return m_tcpClt->IsConnect();
	}
	return false;
}

void cycleAcq_thread_eip(ioDev_eip* pDev) {
	//采样周期为0进入全速采集模式，不退出线程
	while (pDev->m_bRunning) {
		for (int i = 0; i < pDev->m_channels.size(); i++)
		{
			ioChannel* pC = pDev->m_channels[i];

		
			string errorInfo;
			EIP_PKT req, resp;
			if (pDev->m_devSubType == DEV_SUB_TYPE::ethernetIP::control_logix) {
				unsigned char slot = 0;
				if (pDev->m_jDevAddr.contains("slot")) {
					slot = pDev->m_jDevAddr["slot"].get<int>();
				}
				req.pack_ControlLogix_readTag(pC->getDevAddrStr(),pDev->sessionHandle,slot);
				//string s = str::bytesToHexStr(req.data, req.len);
				//LOG("[EIP]" + s);
			}
			else if (pDev->m_devSubType == DEV_SUB_TYPE::ethernetIP::micro800) {
				req.pack_Micro800_readTag(pC->getDevAddrStr(), pDev->sessionHandle);
			}

			if (pDev->doTransaction(req, resp, errorInfo)) {
				if (errorInfo == "") {
					CIP_Response cipResp;
					resp.unpack_Micro800_readTag(cipResp);
					bool validResp = false;
					if (cipResp.general_status == 0) {
						validResp = true;
					}
					else {
						LOG("[warn]EIP设备readTag设备返回失败,%s", getReadTagServiceErrorCodeDesc(cipResp.general_status).c_str());
					}

					//resp tag命令的resp部分
					if (validResp) {
						unsigned char* pData = cipResp.response_data.data();
						unsigned short tagValType = *(unsigned short*)(pData);
						unsigned char* pTagVal = pData + 2;
						if (tagValType == LOGIX_TAG_VAL_TYPE::Bool) {
							unsigned char val = *(unsigned char*)(pTagVal);
							if (val == 0xff) {
								pC->input(true);
							}
							else if (val == 0x01) {
								pC->input(true);
							}
							else {
								pC->input(false);
							}
						}
						else if (tagValType == LOGIX_TAG_VAL_TYPE::Real) {
							float val = *(float*)(pTagVal);
							double dbVal = (double)val;
							pC->input(dbVal);
						}
						else if (tagValType == LOGIX_TAG_VAL_TYPE::SInt) {
							int val = *(unsigned char*)(pTagVal);
							pC->input(val);
						}
						else if (tagValType == LOGIX_TAG_VAL_TYPE::SInt) {
							int val = *(unsigned short*)(pTagVal);
							pC->input(val);
						}
						else if (tagValType == LOGIX_TAG_VAL_TYPE::DInt) {
							int val = *(int*)(pTagVal);
							pC->input(val);
						}
					}	
				}
			}
			else {
				errorInfo = "timeout";
			}
		}

		if (pDev->m_fAcqInterval == 0)
			continue;

		break;
	}


TASK_THREAD_END:
	pDev->m_bCycleAcqThreadRunning = false;
}


void ioDev_eip::DoCycleTask()
{
	if (!m_bEnableAcq)return;
	if (!m_bRunning)return;
	if (!isConnected())return;
	if (isCommBusy())return;
	if (!isEipSessionOpen())return;

	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning)
		{
			// 任务触发线程， 执行线程， 标记位  3者的逻辑关系
			//由任务触发线程将标志位置为true,可以保证线程一定不会被触发产生2个。线程退出标志位才置为false
			//不要将 标志位置为true 放在线程里面执行。这样逻辑上触发线程可能触发2个线程出来。如果执行线程还没有置位，触发线程已经判断为false
			//这样就会产生2个执行线程实例
			m_bCycleAcqThreadRunning = true;
			thread t(cycleAcq_thread_eip, this);
			t.detach();
			timeopt::now(&m_stLastAcqTime);
		}
	}
}

bool ioDev_eip::isEipSessionOpen()
{
	unsigned char nullSessionHandle[4] = { 0 };
	if (memcmp(nullSessionHandle, sessionHandle, 4) == 0) {
		return false;
	}
	return true;
}

//只允许只有一个 transaction 进行
//transaction开始时清空 resp缓存，如果没有收到notify,resp缓存一定是空的
//修改设备配置时，可能会通过stop终止transaction,此时虽然收到notify,resp缓存也是空的
//该函数必须先退出，再销毁设备对象，否则可能出现两种异常
//1.该线程一直卡在m_transaction.m_respSignal.wait_for处不退出。 一直占用commLock
//2.m_transaction.m_respSignal.wait_for此处等待返回，但是后面的 commUnlock已经不起作用，导致依然占用commLock
bool ioDev_eip::doTransaction(EIP_PKT& pduReq, EIP_PKT& pduResp, string& errorInfo)
{
	if (m_bRunning == false)
		return false;


	CommLock();
	TIME startTime = timeopt::now();
	bool ret = false;
	if (m_bRunning == false)
		goto TRANSACTION_END;


	m_bIsWaitingResp = true;
	m_transaction.init();
	m_transaction.setReq(&pduReq);

	m_transaction.m_respSignal.reset();


	sendData(pduReq.data, pduReq.len);

	if (m_transaction.m_respSignal.wait_for(2000))
	{
		if (m_transaction.getResp(pduResp)) {
			//if(pduResp)


			ret = true;
		}
		else {
			string s = str::bytesToHexStr(pduReq.data, pduReq.len);
			LOG("[warn][Modbus通讯]中断transaction,请求包:" + s);
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



bool ioDev_eip::handleResp(EIP_PKT* pkt)
{
	//string errorInfo;
	//string req = str::bytesToHexStr(m_transaction.req->data, m_transaction.req->len);
	//string resp = str::bytesToHexStr(pkt->data, pkt->len);
	//if (!checkRespValication(pkt->data, pkt->len, errorInfo))
	//{
	//	LOG("[warn][Modbus通讯]请求与响应包不匹配,%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), req.c_str(), resp.c_str());
	//}
	//else
	//{
	//	unsigned char FCode = pkt->data[1];
	//	unsigned char FCodeErrResp = 0x80 | m_transaction.req->getFuncCode();
	//	if (FCode == FCodeErrResp)
	//	{
	//		errorInfo = getExpCodeDesc(pkt->data[2]);
	//		LOG("[warn][Modbus通讯]返回失败,错误信息:%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), req.c_str(), resp.c_str());
	//	}
	//	else
	//	{
	//		if (FCode == MB_FUNC_CODE::readCoils ||
	//			FCode == MB_FUNC_CODE::readDiscreteInputs ||
	//			FCode == MB_FUNC_CODE::readHoldingRegisters ||
	//			FCode == MB_FUNC_CODE::readInputRegisters)
	//		{
	//			//handle_pduResp_read(pkt->data, pkt->len);
	//		}
	//		else if (FCode == MB_FUNC_CODE::writeSingleCoil)
	//		{
	//			//ioChannel* pC = m_pCurrentIOCmd->ioChannels[0];
	//			bool bVal = false;
	//			unsigned char cVal = pkt->data[4];
	//			if (cVal == 0xFF)
	//				bVal = true;
	//			json jVal = bVal;
	//			//pC->input(jVal);
	//		}
	//	}
	//}

	return false;
}

bool ioDev_eip::CommLock(int dwTimeoutMS)
{
	return m_pParent->CommLock(dwTimeoutMS);
}

void ioDev_eip::CommUnlock()
{
	m_pParent->CommUnlock();
}

bool ioDev_eip::onRecvPkt(unsigned char* pData, size_t iLen)
{
	setOnline();
	EIP_PKT respPkt;
	respPkt.setData((unsigned char*)pData, iLen);

	if (m_bIsWaitingResp) {
		m_transaction.setResp(&respPkt);
	}
	else {
		EIP_PKT resp;
		resp.unpack(pData, iLen);

		if (resp.header.command == EIP_CMD_REGISTER_SESSION) {
			memcpy(sessionHandle, resp.header.session_handle, 4);
			string s = str::bytesToHexStr(sessionHandle, 4);
			LOG("[EtherNet/IP]设备连接成功%s,sessionHandle=%s", getIOAddrStr().c_str(), s.c_str());
		}
	}

	return true;
}


size_t isValidPkt_EIP(unsigned char* p, size_t iLen)
{
	if (iLen < 24)
		return 0;

	EIP_PKT_HEADER* pHeader = (EIP_PKT_HEADER*)p;

	if (iLen < pHeader->length + 24) {
		return 0;
	}
	
	return 24 + pHeader->length;
}

bool ioDev_eip::onRecvData(unsigned char* pData, size_t iLen)
{
	stream2pkt* pab = &m_pab;
	pab->PushStream(pData, iLen);

	while (pab->PopPkt(isValidPkt_EIP))
	{
		if (pab->abandonData != "")
		{
			string remoteAddr = getDevAddrStr();
			LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
			m_abandonLen += pab->iAbandonLen;
		}
		onRecvPkt(pab->pkt, pab->iPktLen);
	}
	return false;
}

void ioDev_eip::onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo)
{
	IOLogRecv(pData, len, connInfo->getRemoteAddr(),"host");
	onRecvData(pData, len);
}

void ioDev_eip::onEvent_online()
{
	EIP_PKT req;
	req.header.command = EIP_CMD_REGISTER_SESSION;
	req.header.length = 4;
	unsigned char cmdData[4] = {1,0,0,0};

	req.pushData(&req.header,sizeof(req.header));
	req.pushData(cmdData, 4);

	sendData(req.data, req.len);
}



void ioDev_eip::generateAcqCmd()
{
	
}


