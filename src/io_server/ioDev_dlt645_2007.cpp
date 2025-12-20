#include "pch.h"
#include "ioDev_dlt645_2007.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"


ioDev* createDev_dlt645_2007()
{
	return new ioDev_dlt645_2007();
}

class createReg_dlt645_2007 {
public:
	createReg_dlt645_2007();
};
createReg_dlt645_2007::createReg_dlt645_2007()
{
	mapDevCreateFunc["dlt645-2007"] = createDev_dlt645_2007;
	mapDevTypeLabel["dlt645-2007"] = "DLT645-2007电表";
}
createReg_dlt645_2007 regDlt645_2007;


ioDev_dlt645_2007::ioDev_dlt645_2007(void)
{
	m_devType = "dlt645-2007";
	m_bCycleAcqThreadRunning = false;
	m_level = "device";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_bEnableRecvPreamble = true;
}


ioDev_dlt645_2007::~ioDev_dlt645_2007(void)
{
}

void ioDev_dlt645_2007::stop()
{
	m_bRunning = false;  //设为false以后，应当不会有新的线程尝试占用通信锁
	m_transaction.m_respSignal.notify(); //中断所有等待中的会话

	//此处怎么设计找沈春种或者廖灰灰讨论一次。有没有基于信号量的方法
	while (m_bWorkingThreadRunning) {
		timeopt::sleepMilli(1);
	}
}



bool ioDev_dlt645_2007::loadConf(json& conf)
{
	//if (conf.contains("enableRecvPreamble")) {
	//	m_bEnableRecvPreamble = conf["enableRecvPreamble"].get<bool>();
	//}


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

bool ioDev_dlt645_2007::toJson(yyjson_mut_val* conf, yyjson_mut_doc* doc, DEV_QUERIER querier)
{
	//lock_conf_shared();

	//if (querier.getConf) {
	//	conf["enableRecvPreamble"] = m_bEnableRecvPreamble;
	//}

	//unlock_conf_shared();

	return ioDev::toJson(conf,doc,querier);
}

void ioDev_dlt645_2007::output(string chanAddr, json jVal, json& rlt,json& err, bool sync)
{
	ioChannel* pC = getChanByDevAddr(chanAddr);
	if (pC)
		output(pC, jVal, rlt,err, sync);
	else
		return ;
}

void ioDev_dlt645_2007::output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync)
{


}

bool ioDev_dlt645_2007::isCommBusy()
{
	assert(m_pParent);
	return m_pParent->isCommBusy();
}

bool ioDev_dlt645_2007::isConnected()
{
	return m_pParent->isConnected();
}

struct DATA_FMT {
	string fmt;
	int readValDigitCount;  //数字的个数
	double k;  //整数乘上k后，得到该数值

	DATA_FMT() {
		readValDigitCount = 0;
	}

	void init(string f) {
		fmt = f;
		string readValFmt;
		for (int i = 0; i < fmt.length(); i++) {
			char a = fmt[i];
			if (a == 'X' || a == '.') {
				readValFmt += a;
			}

			if (a == 'X') {
				readValDigitCount++;
			}
		}

		size_t dotPos = readValFmt.find(".");
		if (dotPos != string::npos) {
			int decimalDigits = readValDigitCount - dotPos;
			k = pow(0.1, decimalDigits);
		}
	}
};

json ioDev_dlt645_2007::parseReadData(unsigned char* pD, int len,string fmt) {
	string digits;
	DATA_FMT df;
	df.init(fmt);

	if (df.readValDigitCount <= 2 * len/*一个字节2个BCD码，因此乘2*/) { //模拟器测试时返回数据长度比实际数据长度大，忽略尾部的数据
		int validByteLen = df.readValDigitCount / 2;
		unsigned char* readVal = new unsigned char[validByteLen];
		for (int i = 0; i < validByteLen; i++) {
			readVal[i] = pD[i] - 0x33;
		}

		char szVal[50] = { 0 };
		d07_bcd2str(readVal, szVal, validByteLen);
		delete[] readVal;

		double fVal = atof(szVal);
		fVal *= df.k;

		return fVal;
	}

	return nullptr;
}

void cycleAcq_thread_dlt645(ioDev_dlt645_2007* pDev) {
	string devAddr;
	if (!pDev->m_jDevAddr["id"].is_string()) {
		goto TASK_THREAD_END;
	}
	devAddr  = pDev->m_jDevAddr["id"];
	if (devAddr.length() != 12) {
		goto TASK_THREAD_END;
	}

	//采样周期为0进入全速采集模式，不退出线程
	while (pDev->m_bRunning) {
		TIME acqTimeDev;
		acqTimeDev.setNow();
		for (int i = 0; i < pDev->m_channels.size(); i++)
		{
			ioChannel* pC = pDev->m_channels[i];

			unsigned char DI[4] = { 0 };

			if (!pC->m_jDevAddr.is_string())
				continue;
			string chanAddr = pC->m_jDevAddr.get<string>(); //此处的devAddr就是 DLT645中的数据标识，固定为4个字节
			if (chanAddr.length() != 8) {
				continue;
			}

			//界面上配置的顺序是 DI3 DI2 DI1 DI0
			//数据包中的顺序是 DI0 DI1 DI2 DI3
			DLT645_2007_PKT::setDI(chanAddr.c_str(), DI);


			string errorInfo;
			DLT645_2007_PKT req, resp;
			req.ctrlCode = D07_CTRL_READ_DATA;
			req.setAddr(devAddr);
			req.setDataRegion(DI, 4);
			req.pack();
			if (pDev->doTransaction(req, resp, errorInfo)) {
				if (errorInfo == "") {
					resp.unpack();
					json jVal = pDev->parseReadData(resp.d + 4 /*偏移4字节的数据标识*/, resp.dLen - 4, pC->m_fmt);

					if (jVal.is_null()) {
						string s = str::bytesToHexStr(resp.data, resp.len);
						LOG("[warn][DLT645-2007]读取数据解析回包失败,%s", resp.data, resp.len);
					}
					else {
						if (pDev->m_chanTimeSync) {
							pC->input(jVal,&acqTimeDev);
						}
						else {
							TIME acqTimeChan;
							acqTimeChan.setNow();
							pC->input(jVal, &acqTimeChan);
						}
					}
				}
			}
		}

		if (pDev->m_fAcqInterval == 0)
			continue;

		break;
	}


TASK_THREAD_END:
	pDev->m_bCycleAcqThreadRunning = false;
}


void ioDev_dlt645_2007::DoCycleTask()
{
	if (!m_bEnableAcq)return;
	if (!m_bRunning)return;
	if (!isConnected())return;
	if (isCommBusy())return;

	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning)
		{
			// 任务触发线程， 执行线程， 标记位  3者的逻辑关系
			//由任务触发线程将标志位置为true,可以保证线程一定不会被触发产生2个。线程退出标志位才置为false
			//不要将 标志位置为true 放在线程里面执行。这样逻辑上触发线程可能触发2个线程出来。如果执行线程还没有置位，触发线程已经判断为false
			//这样就会产生2个执行线程实例
			m_bCycleAcqThreadRunning = true;
			thread t(cycleAcq_thread_dlt645, this);
			t.detach();
			timeopt::now(&m_stLastAcqTime);
		}
	}
}

void ioDev_dlt645_2007::DoCycleTaskSync() {
	if (!m_bEnableAcq)return;
	if (!m_bRunning)return;
	if (!isConnected())return;

	m_bCycleAcqThreadRunning = true;
	cycleAcq_thread_dlt645(this);
	timeopt::now(&m_stLastAcqTime);
}

//只允许只有一个 transaction 进行
//transaction开始时清空 resp缓存，如果没有收到notify,resp缓存一定是空的
//修改设备配置时，可能会通过stop终止transaction,此时虽然收到notify,resp缓存也是空的
//该函数必须先退出，再销毁设备对象，否则可能出现两种异常
//1.该线程一直卡在m_transaction.m_respSignal.wait_for处不退出。 一直占用commLock
//2.m_transaction.m_respSignal.wait_for此处等待返回，但是后面的 commUnlock已经不起作用，导致依然占用commLock
bool ioDev_dlt645_2007::doTransaction(DLT645_2007_PKT& pduReq, DLT645_2007_PKT& pduResp, string& errorInfo)
{
	unsigned char* pSend = nullptr;
	if (m_bRunning == false)
		return false;
	unsigned char preamble[4] = { 0xFE,0xFE,0xFE,0xFE };


	CommLock();
	TIME startTime = timeopt::now();
	bool ret = false;
	if (m_bRunning == false)
		goto TRANSACTION_END;


	m_bIsWaitingResp = true;
	m_transaction.init();
	m_transaction.setReq(&pduReq);

	m_transaction.m_respSignal.reset();

	
	pSend = new unsigned char[4 + pduReq.len];
	memcpy(pSend, preamble, 4);
	memcpy(pSend + 4, pduReq.data, pduReq.len);
	sendData(pSend, 4+ pduReq.len);
	delete pSend;

	if (m_transaction.m_respSignal.wait_for(tds->conf->iotimeoutDLT645))
	{
		if (m_transaction.getResp(pduResp)) {
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
	m_bIsWaitingResp = false;  // waiting resp 的置位必须在 commLock和unlock的内部。如果放在方便，这里unlock之后会和另外一个线程同时操作该变量
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



bool ioDev_dlt645_2007::handleResp(DLT645_2007_PKT* pkt)
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

bool ioDev_dlt645_2007::CommLock(int dwTimeoutMS)
{
	return m_pParent->CommLock(dwTimeoutMS);
}

void ioDev_dlt645_2007::CommUnlock()
{
	m_pParent->CommUnlock();
}

bool ioDev_dlt645_2007::onRecvPkt(unsigned char* pData, size_t iLen)
{
	DLT645_2007_PKT respPkt;
	respPkt.unpack(pData, iLen);

	if (respPkt.getAddrString() != m_devAddr) return false;

	setOnline();

	if (m_transaction.req) {
		if (respPkt.ctrlCode == m_transaction.req->ctrlCode | 0x80) {
			if (m_transaction.req->ctrlCode == D07_CTRL_READ_DATA) {
				unsigned char* reqDI = m_transaction.req->getDIBuff();
				unsigned char* respDI = respPkt.getDIBuff();
				//必须判断读取的数据标志位的符合性,有时串口返回慢，会导致下一个di的请求收到上一个di的响应
				if (memcmp(reqDI, respDI, 4) == 0) {
					m_transaction.setResp(&respPkt);
				}
				else {
					m_unhandledRecvPktCount++;
				}
			}
			else
				m_unhandledRecvPktCount++; //暂时不支持其他命令
		}
		else {
			m_unhandledRecvPktCount++;
		}
	}
	else {
		m_unhandledRecvPktCount++;
	}


	return true;
}

//串口通信时，需要进行字节流组包
bool ioDev_dlt645_2007::onRecvData(unsigned char* pData, size_t iLen)
{
	stream2pkt* pab = &m_pab;
	pab->PushStream(pData, iLen);

	while (pab->PopPkt(isValidPkt_DLT645_2007))
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



void ioDev_dlt645_2007::generateAcqCmd()
{
	
}


