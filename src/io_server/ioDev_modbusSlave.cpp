#include "pch.h"
#include "ioDev_modbusSlave.h"
#include "ioGW_localSerial.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"



ioDev_ModbusSlave::ioDev_ModbusSlave(void)
{

}


ioDev_ModbusSlave::~ioDev_ModbusSlave(void)
{
}

void ioDev_ModbusSlave::stop()
{
	//三个外部调用线程. 三个入口都加入 m_bRunning判断
	//接收线程调用 onRecvData
	//数据输出 output
	//采集 doCycleTask
	m_bRunning = false;  //设为false以后，应当不会有新的线程尝试占用该对象

	while (m_bCycleAcqThreadRunning || m_bRecvProcessing || m_bOutputting) {
		m_transaction.m_respSignal.notify(); //中断所有等待中的调用
		timeopt::sleepMilli(1);
	}
}

void ioDev_ModbusSlave::getIOTypeByMBDataType(ioChannel* p)
{
	//这里增加一个特殊的机制,用于广达的开合闸功能
	//如果选择了自定义输出类型,则认为他是仅输出.
	if (p->m_bCustomOutputType)
	{
		p->m_ioType = CHAN_IO_TYPE::O;
	}
	else if (p->m_regType == MODBUS_REG_TYPE::coil || p->m_regType == MODBUS_REG_TYPE::holdingRegister)
	{
		p->m_ioType = CHAN_IO_TYPE::IO;
	}
	else if (p->m_regType == MODBUS_REG_TYPE::discreteInput || p->m_regType == MODBUS_REG_TYPE::inputRegister)
	{
		p->m_ioType = CHAN_IO_TYPE::I;
	}
}

bool ioDev_ModbusSlave::loadConf(json& conf)
{
	if (ioDev::loadConf(conf))
	{
		for (int i = 0; i < m_channels.size(); i++)
		{
			ioChannel* p = m_channels[i];
			getIOTypeByMBDataType(p);
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

void ioDev_ModbusSlave::output(string chanAddr, json jVal, json& rlt,json& err, bool sync)
{
	m_bOutputting = true;
	FLAG_GUARD g(&m_bOutputting);

	if (m_bRunning == false) {
		err = "device stopped";
		return;
	}

	ioChannel* pC = getChanByDevAddr(chanAddr);
	if (pC)
		output(pC, jVal, rlt,err, sync);
}

void ioDev_ModbusSlave::output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync)
{
	m_bOutputting = true;
	FLAG_GUARD g(&m_bOutputting);

	if (m_bRunning == false) {
		err = "device stopped";
		return;
	}

	if (m_devType == DEV_TYPE_modbus_tcp_slave && pIOSession == nullptr) {
		err = makeRPCError(RPC_ERROR_CODE::IO_devOffline, "modbus tcp slave device offline,tcp disconnected ,ioAddr=" + getIOAddrStr());
		return;
	}

	MB_PDU pduReq;
	if (pC->m_regType == MODBUS_REG_TYPE::holdingRegister)
	{	
		if (pC->m_fmt == STORAGE_FMT::Bit16No)
		{
			if (jVal.is_boolean())
			{
				unsigned short mbVal;
				bool val = jVal.get<bool>();
				if (val)
				{
					unsigned short mask = 1 << pC->m_regBitIndex;
					unsigned short outputVal = pC->holdingRegVal | mask;
					mbVal = outputVal;
				}
				else
				{
					unsigned short a = pC->holdingRegVal; // 初始值
					unsigned short mask = ~(1 << pC->m_regBitIndex);
					a = a & mask;
					mbVal = a;
				}

				if (pC->m_bCustomOutputType && pC->m_sCustomOutputType == "writeMultipleRegister")
				{
					PDU_REQ_writeMultiReg* pCmd = new PDU_REQ_writeMultiReg();
					pCmd->func_code = MB_FUNC_CODE::writeMultipleRegister;
					pCmd->setStartReg(pC->m_regOffset);
					pCmd->setRegNum(1);
					pCmd->byte_count = 2;
					json tempVal = mbVal;
					setChanValToRegBuff(pC, tempVal, pCmd->reg_data);

					pduReq.setData(pCmd, pCmd->getSize());
				}
				else
				{
					PDU_REQ_writeSingleReg* pCmd = new PDU_REQ_writeSingleReg();
					pCmd->func_code = MB_FUNC_CODE::writeSingleRegister;
					pCmd->setOffset(pC->m_regOffset);
					pCmd->setVal(mbVal);

					pduReq.setData(pCmd, sizeof(PDU_REQ_writeSingleReg));
				}
			}
			else
			{
				string errorInfo = "[error]输出失败, 当前为保持寄存器Bit16No类型, 但输出值不是布尔量";
				err = makeRPCError(RPC_ERROR_CODE::MO_outputFail, errorInfo);
				return;
			}
		}
		else
		{
			int regDataLen = storageSize(pC->m_fmt);
			int regCount = regDataLen / 2;
			if (regCount == 1) //
			{
				if (pC->m_bCustomOutputType && pC->m_sCustomOutputType == "writeMultipleRegister")
				{
					PDU_REQ_writeMultiReg* pCmd = new PDU_REQ_writeMultiReg();
					pCmd->func_code = MB_FUNC_CODE::writeMultipleRegister;
					pCmd->setStartReg(pC->m_regOffset);
					pCmd->setRegNum(regCount);
					pCmd->byte_count = regDataLen;
					setChanValToRegBuff(pC, jVal, pCmd->reg_data);

					pduReq.setData(pCmd, pCmd->getSize());
				}
				else
				{
					PDU_REQ_writeSingleReg* pCmd = new PDU_REQ_writeSingleReg();
					pCmd->func_code = MB_FUNC_CODE::writeSingleRegister;
					pCmd->setOffset(pC->m_regOffset);
					if (pC->m_fmt == STORAGE_FMT::UInt16)
					{
						unsigned short usVal = jVal.get<unsigned short>();
						pCmd->setVal(usVal);
					}
					else if (pC->m_fmt == STORAGE_FMT::Int16)
					{
						short usVal = jVal.get<short>();
						pCmd->setVal(usVal);
					}

					pduReq.setData(pCmd, sizeof(PDU_REQ_writeSingleReg));
				}
			}
			else
			{
				PDU_REQ_writeMultiReg* pCmd = new PDU_REQ_writeMultiReg();
				pCmd->func_code = MB_FUNC_CODE::writeMultipleRegister;
				pCmd->setStartReg(pC->m_regOffset);
				pCmd->setRegNum(regCount);
				pCmd->byte_count = regDataLen;
				setChanValToRegBuff(pC, jVal, pCmd->reg_data);

				pduReq.setData(pCmd, pCmd->getSize());
			}
		}
	}
	else if (pC->m_regType == MODBUS_REG_TYPE::coil)
	{
		PDU_REQ_writeSingleCoil* pCmd = new PDU_REQ_writeSingleCoil();
		pCmd->func_code = MB_FUNC_CODE::writeSingleCoil;
		pCmd->setOffset(pC->m_regOffset);
		pCmd->setVal(jVal.get<bool>());

		pduReq.setData(pCmd, sizeof(PDU_REQ_writeSingleCoil));
	}
	else
	{
		string errorInfo = str::format("[error]输出失败,modbus通道寄存器类型错误,当前类型:%s,通道地址:%s", pC->m_regType.c_str(), pC->getIOAddrStr().c_str());
		err = makeRPCError(RPC_ERROR_CODE::MO_outputFail, errorInfo);
		return;
	}

	MB_PDU resp;
	string errorInfo;
	bool ret = doTransaction(pduReq, resp,errorInfo);
	if (ret)
	{
		rlt = "\"ok\"";
		pC->input(jVal);
		return;
	}
	else
	{
		err = makeRPCError(RPC_ERROR_CODE::MO_outputFail, errorInfo);
		return;
	}
}

void ioDev_ModbusSlave::handle_pduResp_read(PDU_REQ_read* req,PDU_RESP_read* resp)
{
	lock_conf_shared();
	PDU_RESP_read* pPduResp = resp;
	PDU_REQ_read* pPduReq = req;

	int readOffset = 0;
	vector<string> vecDevAddr;
	vector<json> vecVal;
	for (auto i : m_mapDataChannel)
	{
		ioChannel* pC = i.second;
		
		//如果是仅输出,不进行读取.
		//如果未绑定对应位置,不进行读取.
		if (pC->m_ioType == "o" || pC->m_strTagBind == "") continue;

		unsigned char FCode = pPduReq->func_code;
		json jVal = nullptr;
		//根据当前读取的modbusReg缓存类型，将值设置到对应的通道类型
		if (FCode == MB_FUNC_CODE::readCoils && pC->m_regType == MODBUS_REG_TYPE::coil)
		{
			jVal = getChanValFromRegBuff(pC, pPduReq->getStartReg(), pPduResp->reg_data, pPduResp->byte_count);
		}
		else if (FCode == MB_FUNC_CODE::readDiscreteInputs && pC->m_regType == MODBUS_REG_TYPE::discreteInput)
		{
			jVal = getChanValFromRegBuff(pC, pPduReq->getStartReg(), pPduResp->reg_data, pPduResp->byte_count);
		}
		else if (FCode == MB_FUNC_CODE::readHoldingRegisters && pC->m_regType == MODBUS_REG_TYPE::holdingRegister)
		{
			jVal = getChanValFromRegBuff(pC, pPduReq->getStartReg(), pPduResp->reg_data, pPduResp->byte_count);
		}
		else if (FCode == MB_FUNC_CODE::readInputRegisters && pC->m_regType == MODBUS_REG_TYPE::inputRegister)
		{
			jVal = getChanValFromRegBuff(pC, pPduReq->getStartReg(), pPduResp->reg_data, pPduResp->byte_count);
		}

		if (jVal != nullptr) {
			vecDevAddr.push_back(i.first);
			vecVal.push_back(jVal);
		}
	}

	if (vecDevAddr.size() > 0) {
		input(vecDevAddr, vecVal);
	}
	unlock_conf_shared();
}

bool ioDev_ModbusSlave::isCommBusy()
{
	assert(m_pParent);
	return m_pParent->isCommBusy();
}

bool ioDev_ModbusSlave::isConnected()
{
	return m_pParent->isConnected();
}


void cycleAcq_thread_mbSlave(ioDev_ModbusSlave* pDev) {
	setThreadName("modbus transaction thread");
	FLAG_GUARD g(&pDev->m_bCycleAcqThreadRunning);

	//采样周期为0进入全速采集模式，不退出线程
	while (pDev->m_bRunning) {
		for (int i = 0; i < pDev->m_vecAcqCmd.size(); i++)
		{
			PDU_REQ_read& req = pDev->m_vecAcqCmd[i];
			string errorInfo;
			string addr = pDev->m_devAddr;
			MB_PDU pduReq, pduResp;
			pduReq.setData((unsigned char*)&req, sizeof(req));
			if (pDev->doTransaction(pduReq, pduResp, errorInfo))
			{
				unsigned char FCode = pduResp.getFuncCode();
				PDU_RESP_read* pPduRespRead = (PDU_RESP_read*)pduResp.data;
				if (FCode == MB_FUNC_CODE::readCoils)
				{
					pDev->handle_pduResp_read(&req, pPduRespRead);
				}
				else if (FCode == MB_FUNC_CODE::readDiscreteInputs)
				{
					pDev->handle_pduResp_read(&req, pPduRespRead);
				}
				else if (FCode == MB_FUNC_CODE::readHoldingRegisters)
				{
					pDev->handle_pduResp_read(&req, pPduRespRead);
				}
				else if (FCode == MB_FUNC_CODE::readInputRegisters)
				{
					pDev->handle_pduResp_read(&req, pPduRespRead);
				}
			}
			else {
				//如果第一条命令采集失败，可能设备离线，后续命令不再采集。加快同一条485上的其他设备的采集频率
				break;
			}
		}

		if (pDev->m_fAcqInterval == 0)
			continue;
		
		break;
	}
}



void ioDev_ModbusSlave::DoCycleTaskSync() {
	if (!m_bEnableAcq)return;
	if (!m_bRunning)return;
	if (!isConnected())return;

	cycleAcq_thread_mbSlave(this);
}

void ioDev_ModbusSlave::DoCycleTask()
{
	if (!m_bEnableAcq)return;
	if (!m_bRunning)return;
	if (!isConnected())return;
	if (isCommBusy())return;

	if (m_pParent != nullptr && m_pParent != &ioSrv) {
		if (m_pParent->isBusBusy()) {
			return;
		}
	}

	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning)
		{
			m_bCycleAcqThreadRunning = true;
			thread t(cycleAcq_thread_mbSlave, this);
			t.detach();
			timeopt::now(&m_stLastAcqTime);
		}
	}
}

//只允许只有一个 transaction 进行
//transaction开始时清空 resp缓存，如果没有收到notify,resp缓存一定是空的
//修改设备配置时，可能会通过stop终止transaction,此时虽然收到notify,resp缓存也是空的
//该函数必须先退出，再销毁设备对象，否则可能出现两种异常
//1.该线程一直卡在m_transaction.m_respSignal.wait_for处不退出。 一直占用commLock
//2.m_transaction.m_respSignal.wait_for此处等待返回，但是后面的 commUnlock已经不起作用，导致依然占用commLock
bool ioDev_ModbusSlave::doTransaction(MB_PDU& pduReq, MB_PDU& pduResp, string& errorInfo)
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
	m_transaction.setReq(&pduReq);

	m_transaction.m_respSignal.reset();

	if (!sendADU(pduReq)) {
		goto TRANSACTION_END;
	}

	if (m_transaction.m_respSignal.wait_for(tds->conf->iotimeoutModbusRtu))
	{
		if (m_transaction.getResp(pduResp)) {
			string strReq = m_transaction.req_adu;
			string strResp = m_transaction.resp_adu;

			unsigned char FCode = pduResp.data[0];
			unsigned char FCodeErrResp = 0x80 | m_transaction.req->getFuncCode();
			if (FCode == FCodeErrResp)
			{
				unsigned char errCode = pduResp.data[1];
				errorInfo = getExpCodeDesc(errCode);
				errorInfo = str::format("设备返回异常:%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), strReq.c_str(), strResp.c_str());
				
				if (errCode == MODBUS_EXP_CODE::illegalDataAddress) {
					errorInfo += ",检查是否读写不存在的寄存器，读写寄存器偏移是否越界";
				}
				LOG("[warn][Modbus通讯]" + errorInfo);
			}
			else if (!checkRespValication(pduReq,pduResp.data, pduResp.len, errorInfo))
			{
				errorInfo = str::format("请求与响应包不匹配,%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), strReq.c_str(), strResp.c_str());
				LOG("[warn][Modbus通讯]" + errorInfo);
			}
			else
			{
				ret = true;
			}
		}
		else {
			LOG("[warn][Modbus通讯]中断transaction,请求包:" + m_transaction.req_adu);
		}
	}
	else
	{
		m_transaction.setReq(nullptr);
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


unsigned char ioDev_ModbusSlave::getFCode(string regType)
{
	if (regType == MODBUS_REG_TYPE::inputRegister)
		return MB_FUNC_CODE::readInputRegisters;
	else if (regType == MODBUS_REG_TYPE::coil)
		return MB_FUNC_CODE::readCoils;
	else if (regType == MODBUS_REG_TYPE::discreteInput)
		return MB_FUNC_CODE::readDiscreteInputs;
	else if (regType == MODBUS_REG_TYPE::holdingRegister)
		return MB_FUNC_CODE::readHoldingRegisters;
	else {
		T_ASSERT(false);
		return 0;
	}
}

unsigned long power(int base, int times)
{
	int i;
	unsigned long rslt = 1;
	for (i = 0; i < times; i++)
		rslt *= base;
	return rslt;
}

unsigned long  BCDtoDec(const unsigned char* bcd, int length)
{
	int i, tmp;
	unsigned long dec = 0;
	for (i = 0; i < length; i++)
	{
		tmp = ((bcd[i] >> 4) & 0x0F) * 10 + (bcd[i] & 0x0F);
		dec += tmp * power(100, length - 1 - i);
	}
	return dec;
}


//mb寄存器转本地数据结构
void mbBuff2Local(char* pMB, char* pLocal, string byteOrder,int size) {
	if (size == 4) {
		if (byteOrder == MB_BYTE_ORDER::bigEndian) {
			pLocal[3] = pMB[0];
			pLocal[2] = pMB[1];
			pLocal[1] = pMB[2];
			pLocal[0] = pMB[3];
		}
		else if (byteOrder == MB_BYTE_ORDER::littleEndian) {
			pLocal[3] = pMB[2];
			pLocal[2] = pMB[3];
			pLocal[1] = pMB[0];
			pLocal[0] = pMB[1];
		}
		else if (byteOrder == MB_BYTE_ORDER::bigEndianByteSwap) {
			pLocal[3] = pMB[1];
			pLocal[2] = pMB[0];
			pLocal[1] = pMB[3];
			pLocal[0] = pMB[2];
		}
		else if (byteOrder == MB_BYTE_ORDER::littleEndianByteSwap) {
			pLocal[3] = pMB[3];
			pLocal[2] = pMB[2];
			pLocal[1] = pMB[1];
			pLocal[0] = pMB[0];
		}
	}
	else if (size == 2)
	{
		if (byteOrder == MB_BYTE_ORDER::bigEndian) {
			pLocal[0] = pMB[1];
			pLocal[1] = pMB[0];
		}
		else if (byteOrder == MB_BYTE_ORDER::littleEndian) {
			pLocal[0] = pMB[0];
			pLocal[1] = pMB[1];
		}
	}
}



void Local2MbBuff(char* pMB, char* pLocal, string byteOrder, int size) {
	if (size == 4) {
		if (byteOrder == MB_BYTE_ORDER::bigEndian) {
			pMB[0] = pLocal[3];
			pMB[1] = pLocal[2];
			pMB[2] = pLocal[1];
			pMB[3] = pLocal[0];
		}
		else if (byteOrder == MB_BYTE_ORDER::littleEndian) {
			pMB[2] = pLocal[3];
			pMB[3] = pLocal[2];
			pMB[0] = pLocal[1];
			pMB[1] = pLocal[0];
		}
		else if (byteOrder == MB_BYTE_ORDER::bigEndianByteSwap) {
			pMB[1] = pLocal[3];
			pMB[0] = pLocal[2];
			pMB[3] = pLocal[1];
			pMB[2] = pLocal[0];
		}
		else if (byteOrder == MB_BYTE_ORDER::littleEndianByteSwap) {
			pMB[3] = pLocal[3];
			pMB[2] = pLocal[2];
			pMB[1] = pLocal[1];
			pMB[0] = pLocal[0];
		}
	}
}

json ioDev_ModbusSlave::getChanValFromRegBuff(ioChannel* pC,size_t regOffsetStart, char* regData, size_t len)
{
	json jVal;
	if (pC->m_regType == MODBUS_REG_TYPE::holdingRegister || pC->m_regType == MODBUS_REG_TYPE::inputRegister)
	{
		//检查通道的寄存器地址是否在 回包的寄存器范围内，如果不在，返回nullptr
		size_t regOffsetEnd = regOffsetStart + len / 2;
		if (pC->m_regOffset < regOffsetStart) //当前通道不在回包中的数据范围中
			return nullptr;
		if (pC->m_regOffset > regOffsetEnd)
			return nullptr;
		size_t regOffsetResp = pC->m_regOffset - regOffsetStart;

		char* pChanData = regData + regOffsetResp * 2;
		string storageFmt = pC->m_fmt;
		string byteOrder = pC->m_byteOrder;
		int bit16Index = pC->m_regBitIndex;
		if (storageFmt == STORAGE_FMT::UInt16)
		{
			unsigned short mbVal;
			memcpy(&mbVal, pChanData, 2);
			common::endianSwap((char*)&mbVal, 2);
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Int16)
		{
			short mbVal;
			memcpy(&mbVal, pChanData, 2);
			common::endianSwap((char*)&mbVal, 2);
			jVal = mbVal;
		}
		//AB CD 表示的是在返回的buff中的排序规则
		else if (storageFmt == STORAGE_FMT::Int32)
		{
			int mbVal;
			char* pBuff = (char*)&mbVal;
			mbBuff2Local(pChanData, pBuff, byteOrder, 4);
			jVal = mbVal;
		}
		//32位无符号整型
		else if (storageFmt == STORAGE_FMT::UInt32)
		{
			unsigned int mbVal;
			char* pBuff = (char*)&mbVal;
			mbBuff2Local(pChanData, pBuff, byteOrder, 4);
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Float)
		{
			float mbVal;
			char* pBuff = (char*)&mbVal;
			mbBuff2Local(pChanData, pBuff, byteOrder, 4);
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::BCD16)
		{
			unsigned long mbVal = BCDtoDec((const unsigned char*)pChanData, 2);
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::BCD32) {
			unsigned long mbVal = BCDtoDec((const unsigned char*)pChanData, 4);
			jVal = mbVal;
		}
		else if (storageFmt== STORAGE_FMT::Bit16No)
		{
			unsigned short mbVal;
			char* pBuff = (char*)&mbVal;
			mbBuff2Local(pChanData, pBuff, byteOrder, 2);
			pC->holdingRegVal = mbVal;

			bool temp = ((mbVal >> bit16Index) & 1) != 0;
			jVal = temp;
		}
	}
	else if(pC->m_regType == MODBUS_REG_TYPE::discreteInput || pC->m_regType == MODBUS_REG_TYPE::coil)
	{
		//检查通道的寄存器地址是否在 回包的寄存器范围内，如果不在，返回nullptr
		size_t regOffsetEnd = regOffsetStart + len * 8;
		if (pC->m_regOffset < regOffsetStart) //当前通道不在回包中的数据范围中
			return nullptr;
		if (pC->m_regOffset > regOffsetEnd)
			return nullptr;
		size_t regOffsetResp = pC->m_regOffset - regOffsetStart;

		char dataByte = regData[regOffsetResp / 8];
		size_t idx = regOffsetResp % 8;
		char mask = 1 << idx;
		bool val = (dataByte & mask) > 0;
		jVal = val;
	}
	return jVal;
}

void ioDev_ModbusSlave::setChanValToRegBuff(ioChannel* pC, json jVal, char* pRegData)
{
	if (pC->m_regType == MODBUS_REG_TYPE::holdingRegister)
	{
		string storageFmt = pC->m_fmt;
		string byteOrder = pC->m_byteOrder;
		if (storageFmt == STORAGE_FMT::UInt16)
		{
			unsigned short mbVal = jVal.get<unsigned short>();
			common::endianSwap((char*)&mbVal, 2);
			memcpy(pRegData ,&mbVal, 2);
		}
		else if (storageFmt == STORAGE_FMT::Int16)
		{
			short mbVal = jVal.get<short>();
			common::endianSwap((char*)&mbVal, 2);
			memcpy(pRegData ,&mbVal, 2);
		}
		//AB CD 表示的是在返回的buff中的排序规则
		else if (storageFmt == STORAGE_FMT::Int32)
		{
			int mbVal = jVal.get<int>();
			char* pBuff = (char*)&mbVal;
			Local2MbBuff(pRegData, pBuff, byteOrder, 4);
		}
		else if (storageFmt == STORAGE_FMT::UInt32)
		{
			unsigned int mbVal = jVal.get<unsigned int>();
			char* pBuff = (char*)&mbVal;
			Local2MbBuff(pRegData, pBuff, byteOrder, 4);
		}
		else if (storageFmt == STORAGE_FMT::Float)
		{
			float mbVal = jVal.get<float>();
			char* pBuff = (char*)&mbVal;
			Local2MbBuff(pRegData, pBuff, byteOrder, 4);
		}
		else if (storageFmt == STORAGE_FMT::Bit16No)
		{
			unsigned short mbVal = jVal.get<unsigned short>();
			common::endianSwap((char*)&mbVal, 2);
			memcpy(pRegData, &mbVal, 2);
		}
	}
	else if (pC->m_regType == MODBUS_REG_TYPE::coil)
	{

	}
}


bool ioDev_ModbusSlave::checkRespValication(MB_PDU& pduReq, unsigned char* pData, size_t iLen, string& errorInfo)
{
	unsigned char FCode = pData[0];
	unsigned int byteCount = pData[1];
	if (pduReq.getFuncCode() != FCode)
	{
		errorInfo = str::format("功能码不一致,请求功能码:%d,响应功能码:%d", pduReq.getFuncCode(), FCode);
		return false;
	}
	else
	{
		if (pduReq.getFuncCode() == MB_FUNC_CODE::readHoldingRegisters ||
			pduReq.getFuncCode() == MB_FUNC_CODE::readInputRegisters)
		{
			PDU_REQ_read* pCmdReadReq = (PDU_REQ_read*)pduReq.data;
			if (pCmdReadReq->getRegNum() * 2 != byteCount)
			{
				errorInfo = str::format("寄存器个数不一致,请求个数:%d,响应个数:%d", pCmdReadReq->getRegNum(), byteCount / 2);
				return false;
			}
		}
		else if (pduReq.getFuncCode() == MB_FUNC_CODE::readCoils ||
			pduReq.getFuncCode() == MB_FUNC_CODE::readDiscreteInputs)
		{
			PDU_REQ_read* pCmdReadReq = (PDU_REQ_read*)pduReq.data;
			if (byteCount * 8 >= pCmdReadReq->getRegNum() &&
				byteCount * 8 - pCmdReadReq->getRegNum() < 8)
			{

			}
			else
			{
				errorInfo = str::format("线圈个数不一致,请求个数:%d,响应字节数:%d", pCmdReadReq->getRegNum(), byteCount);
				return false;
			}
		}
		else if (pduReq.getFuncCode() == MB_FUNC_CODE::writeSingleCoil)
		{
			if (iLen == pduReq.len && memcmp(pData, pduReq.data, iLen) == 0)
			{}
			else
			{
				string req = str::bytesToHexStr(pduReq.data, pduReq.len);
				string resp = str::bytesToHexStr(pData, iLen);
				errorInfo = str::format("写线圈回包错误,请求包:%s,响应包:%s",req.c_str(),resp.c_str());
				return false;
			}
		}
	}

	return true;
}

bool ioDev_ModbusSlave::handleResp(MB_RTU_PKT* pkt)
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

bool ioDev_ModbusSlave::CommLock(int dwTimeoutMS)
{
	return m_pParent->CommLock(dwTimeoutMS);
}

void ioDev_ModbusSlave::CommUnlock()
{
	m_pParent->CommUnlock();
}

bool ioDev_ModbusSlave::onRecvNotify(unsigned char* pData, size_t iLen)
{
	lock_conf_shared();

	vector<string> vecDevAddr;
	vector<json> vecVal;
	for (auto i : m_mapDataChannel)
	{
		ioChannel* pC = i.second;

		//如果是仅输出,不进行读取.
		//如果未绑定对应位置,不进行读取.
		if (pC->m_ioType == "o" || pC->m_strTagBind == "") continue;

		//按通道解析协议，必须连续
		unsigned char* pRegData = pData + 3;
		//遍历通道 01 03 04 02 FB 00 E2 0A 33 
		unsigned char FCode = pData[1];
		if (FCode != MB_FUNC_CODE::readHoldingRegisters) {
			continue;
		}

		json jVal = nullptr;
		//根据当前读取的modbusReg缓存类型，将值设置到对应的通道类型
		jVal = getChanValFromRegBuff(pC, 0, (char*)pRegData, pData[2]);
		
		if (jVal != nullptr) {
			vecDevAddr.push_back(i.first);
			vecVal.push_back(jVal);
		}
	}

	if (vecDevAddr.size() > 0) {
		input(vecDevAddr, vecVal);
	}
	unlock_conf_shared();


	return true;
}

//串口通信时，需要进行字节流组包
bool ioDev_ModbusSlave::onRecvData(unsigned char* pData, size_t iLen)
{
	m_bRecvProcessing = true;
	FLAG_GUARD g(&m_bRecvProcessing);

	if (m_bRunning == false)
		return false;

	stream2pkt* pab = &m_pab;
	pab->PushStream(pData, iLen);

	while (pab->PopPkt(IsValidPkt_ModbusRTU))
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

unsigned char ioDev_ModbusSlave::funcName2funcCode(string name)
{
	if (name == MODBUS_REG_TYPE::coil)
	{
		return MB_FUNC_CODE::readCoils;
	}
	else if (name == MODBUS_REG_TYPE::discreteInput)
	{
		return MB_FUNC_CODE::readDiscreteInputs;
	}
	else if (name == MODBUS_REG_TYPE::holdingRegister)
	{
		return MB_FUNC_CODE::readHoldingRegisters;
	}
	else if (name == MODBUS_REG_TYPE::inputRegister)
	{
		return MB_FUNC_CODE::readInputRegisters;
	}
	return 0;
}

bool sortChan(ioChannel* a, ioChannel* b) {
	if (a->m_regOffset < b->m_regOffset)
		return true;
	return false;
}

void ioDev_ModbusSlave::generateAcqCmd()
{
	m_vecAcqCmd.clear();

	vector<ioChannel*> f1List;
	vector<ioChannel*> f2List;
	vector<ioChannel*> f3List;
	vector<ioChannel*> f4List;
	//分类排序所有通道
	for (int i = 0; i < m_channels.size(); i++)
	{
		ioChannel* c = m_channels[i];
		if (c->m_regType == MODBUS_REG_TYPE::coil)
		{
			f1List.push_back(c);	
		}
		else if (c->m_regType == MODBUS_REG_TYPE::discreteInput)
		{
			f2List.push_back(c);
		}
		else if (c->m_regType == MODBUS_REG_TYPE::holdingRegister)
		{
			f3List.push_back(c);
		}
		else if (c->m_regType == MODBUS_REG_TYPE::inputRegister)
		{
			f4List.push_back(c);
		}
	}

	//按照寄存器偏移排序
	std::sort(f1List.begin(), f1List.end(), sortChan);
	std::sort(f2List.begin(), f2List.end(), sortChan);
	std::sort(f3List.begin(), f3List.begin() + f3List.size(), sortChan);
	std::sort(f4List.begin(), f4List.end(), sortChan);

	//间隔不超过100个寄存器的通道，使用同1条命令读取
	vector<PDU_REQ_read> acqCmd = chanList2MultiAcqCmd(f1List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f2List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f3List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f4List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
}


//根据设备的通道配置，生成轮询方案。如果通道不是连续的，根据算法可以生成多个轮询命令
vector<PDU_REQ_read> ioDev_ModbusSlave::chanList2MultiAcqCmd(vector<ioChannel*>& list)
{
	vector<PDU_REQ_read> vecAcqCmd;
	vector<ioChannel*> batchAcqList;  //一段连续的使用单次通信获取的寄存器数据
	for (int i = 0; i < list.size(); i++)
	{
		ioChannel* c = list[i];
		batchAcqList.push_back(c);

		//是否生成1条批量读取指令
		bool addCmd = false;
		if (i == list.size() - 1)//最后1条
		{
			addCmd = true;
		}
		else //不是最后一条，对比和后面一条是否间隔100个寄存器
		{
			unsigned short nextOffset = list[i + 1]->m_regOffset;
			unsigned short Offset = list[i]->m_regOffset;
			if (nextOffset - Offset > 50)
			{
				addCmd = true;
			}
		}


		//生成读取指令，清空批量获取的通道队列
		if (addCmd)
		{
			PDU_REQ_read pduReq;
			continuousChanInput2MBRead(batchAcqList, pduReq);
			vecAcqCmd.push_back(pduReq);
			batchAcqList.clear();
		}
	}
	return vecAcqCmd;
}

//根据一段地址连续的设备通道配置，生成一个读取请求命令
bool ioDev_ModbusSlave::continuousChanInput2MBRead(vector<ioChannel*>& list, PDU_REQ_read& reqCmd)
{
	ioChannel* lastChan = list[list.size() - 1];
	ioChannel* firstChan = list[0];
	reqCmd.func_code = getFCode(firstChan->m_regType);
	unsigned short startRegOffset = list[0]->m_regOffset;
	//最后1个通道和第一个通道的偏移差
	unsigned short regNum = lastChan->m_regOffset - firstChan->m_regOffset;

	//读寄存器
	if (reqCmd.func_code == MB_FUNC_CODE::readHoldingRegisters || reqCmd.func_code == MB_FUNC_CODE::readInputRegisters)
	{
		int lastChanSize = storageSize(lastChan->m_fmt); //最后1个通道字节数
		int lastChanRegNum = lastChanSize / 2;//最后1个通道寄存器数
		regNum += lastChanRegNum;
	}
	//读开关量
	else
	{
		regNum += 1;
	}

	reqCmd.setRegNum(regNum);
	reqCmd.setStartReg(startRegOffset);

	return true;
}

bool ioDev_ModbusSlave::sendADU(MB_PDU& req)
{
	return false;
}
