#include "pch.h"
#include "ioDev_ModbusSlave.h"
#include "commSrv.h"
#include "db.h"
#include "ioGW_LocalSerial.h"
#include "mp.h"
#include "ioDev_mqttBroker.h"
#include "ioChan.h"
#include "logger.h"
#include "common.hpp"





ioDev_ModbusSlave::ioDev_ModbusSlave(void)
{
	m_devType = IO_DEV_TYPE::DEV::modbus_rtu_slave;
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_level = IO_DEV_LEVEL::device;
	m_addrMode = DEV_ADDR_MODE::deviceID;
	m_pCurrentIOCmd = nullptr;
}


ioDev_ModbusSlave::~ioDev_ModbusSlave(void)
{
}

void ioDev_ModbusSlave::stop()
{
	std::unique_lock<recursive_timed_mutex> lock(m_csCommLock);
	setCurrentIOCmd(nullptr);
	m_bRunning = false;
}

void ioDev_ModbusSlave::getIOTypeByMBDataType(ioChannel* p)
{
	if (p->m_regType == MODBUS_REG_TYPE::coil || p->m_regType == MODBUS_REG_TYPE::holdingRegister)
	{
		p->m_ioType = CHAN_IO_TYPE::IO;
		p->m_ioTypeLabel = getIOTypeLabel(p->m_ioType);
	}
	else if (p->m_regType == MODBUS_REG_TYPE::discreteInput || p->m_regType == MODBUS_REG_TYPE::inputRegister)
	{
		p->m_ioType = CHAN_IO_TYPE::I;
		p->m_ioTypeLabel = getIOTypeLabel(p->m_ioType);
	}
}

bool ioDev_ModbusSlave::loadConf(json& conf)
{
	stop();
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

bool ioDev_ModbusSlave::output(string chanAddr, json jVal, json& jResp, bool sync)
{
	ioChannel* pC = getChanByDevAddr(chanAddr);
	if (pC)
		return output(pC, jVal, jResp, sync);
	else
		return false;
}

bool ioDev_ModbusSlave::output(ioChannel* pC, json jVal, json& jResp, bool sync)
{
	if (pC->m_regType == MODBUS_REG_TYPE::holdingRegister)
	{
		MB_IO_CMD* p = new MB_IO_CMD();
		p->devAddr = atoi(getDevAddrStr().c_str());
		p->fCode = MODBUS_FUNCTION_CODE::writeSingleRegister;
		p->ioChannels.push_back(pC);
		p->jOuputVal = jVal;

		unique_lock<mutex> lock(m_csIOTask);
		m_vecIOTask.push_back(p);
		return true;
	}
	else if (pC->m_regType == MODBUS_REG_TYPE::coil)
	{
		MB_IO_CMD* p = new MB_IO_CMD();
		p->devAddr = atoi(getDevAddrStr().c_str());
		p->fCode = MODBUS_FUNCTION_CODE::writeSingleCoil;
		p->offset = pC->m_regOffset;
		p->ioChannels.push_back(pC);
		p->jOuputVal = jVal;

		unique_lock<mutex> lock(m_csIOTask);
		m_vecIOTask.push_back(p);
		return true;
	}
	return false;
}

bool ioDev_ModbusSlave::isCommBusy()
{
	assert(m_pParent);
	return m_pParent->isCommBusy();
}

void ioDev_ModbusSlave::sendIOCmd(MB_IO_CMD* cmd)
{
	cmd->pack();
	char* p = (char*)cmd->data;
	setCurrentIOCmd(cmd);
	sendData((char*)cmd->data, cmd->len);
}



void ioDev_ModbusSlave::DoCycleTask()
{
	std::unique_lock<recursive_timed_mutex> lock(m_csCommLock);
	if (!m_bRunning)return;

	//检查是否有通信超时
	if (m_pCurrentIOCmd)
	{
		if (timeopt::CalcTimePassSecond(m_pCurrentIOCmd->stLastAcq) > 5)
		{
			setCurrentIOCmd(nullptr);
			m_bOnline = false;
		}
	}

	if (isCommBusy())return;

	//周期时间到，将周期采集命令放入IO队列
	if (m_vecIOTask.size() == 0 && m_bEnableAcq)
	{
		if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
		{
			for (int i = 0; i < m_vecAcqCmd.size(); i++)
			{
				MB_IO_CMD& ac = m_vecAcqCmd[i];
				MB_IO_CMD* p = new MB_IO_CMD();
				*p = ac;
				unique_lock<mutex> lock(m_csIOTask);
				m_vecIOTask.push_back(p);
			}
			GetLocalTime(&m_stLastAcqTime);
		}
	}

	//执行IO通信任务
	if (m_vecIOTask.size() > 0)
	{
		MB_IO_CMD* p = m_vecIOTask[0];
		m_vecIOTask.erase(m_vecIOTask.begin());
		sendIOCmd(p);
	}
}

bool ioDev_ModbusSlave::RequestAndWaitResponse(PKT_DATA& req,PKT_DATA& resp)
{
	m_recvBuff.Init();//删除缓存
	sendData(req.data, req.len);

	if (m_recvSignal.wait_for(3000))
	{
		std::unique_lock<mutex> lock(m_csRecvBuff);
		if (m_recvBuff.PopPkt(APP_LAYER_PROTO::MODBUS_RTU))
		{
			if (m_recvBuff.pkt[0] == atoi(m_devAddr.c_str()))
			{
				resp.setData(m_recvBuff.pkt, m_recvBuff.iPktLen);
				return true;
			}
			return false;
		}
		return false;
	}
	return false;
}

bool ioDev_ModbusSlave::sendData(char* pData, int iLen)
{
	statisOnSend((char*)pData, iLen, getIOAddrStr());
	if (m_pParent)
	{
		return m_pParent->sendData(pData, iLen);
	}
	return false;
}

/*
测试发送：
f7 03 41 9c 02 00 3d c4
测试回包:
f7 03 04 FF 00 FF 00 00 00
*/

void ioDev_ModbusSlave::SendAcqRTData()
{
	RTU_REQ_read req;
	memset(&req,0,sizeof(req));
	req.eqp_addr = atoi(m_devAddr.c_str());
	req.fun_code = MODBUS_FUNCTION_CODE::readInputRegisters;
	req.reg_num_L = 0x02;
	WORD crc = common::N_CRC16((unsigned char*)&req,sizeof(req) -2); 
	req.crc_H = HIBYTE(crc);
	req.crc_L = LOBYTE(crc);

	sendData((char*)&req,sizeof(req));
}

unsigned char ioDev_ModbusSlave::getFCode(string regType)
{
	if (regType == MODBUS_REG_TYPE::inputRegister)
		return MODBUS_FUNCTION_CODE::readInputRegisters;
	else if (regType == MODBUS_REG_TYPE::coil)
		return MODBUS_FUNCTION_CODE::readCoils;
	else if (regType == MODBUS_REG_TYPE::discreteInput)
		return MODBUS_FUNCTION_CODE::readDiscreteInputs;
	else if (regType == MODBUS_REG_TYPE::holdingRegister)
		return MODBUS_FUNCTION_CODE::readHoldingRegisters;
}

json ioDev_ModbusSlave::getChanDataFromBuff(ioChannel* pC,int regOffsetOfData, char* pData,int len)
{
	int regOffsetResp = pC->m_regOffset - regOffsetOfData/*pData中第一个数据的偏移地址*/;
	json jVal;
	if (pC->m_regType == MODBUS_REG_TYPE::holdingRegister || pC->m_regType == MODBUS_REG_TYPE::inputRegister)
	{
		char* pChanData = pData + regOffsetResp * 2;
		string storageFmt = pC->m_storageFmt;
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
		else if (storageFmt == STORAGE_FMT::Int32_AB_CD)
		{
			int mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[0];
			pBuff[2] = pChanData[1];
			pBuff[1] = pChanData[2];
			pBuff[0] = pChanData[3];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Int32_CD_AB)
		{
			int mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[2];
			pBuff[2] = pChanData[3];
			pBuff[1] = pChanData[0];
			pBuff[0] = pChanData[1];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Int32_BA_DC)
		{
			int mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[1];
			pBuff[2] = pChanData[0];
			pBuff[1] = pChanData[3];
			pBuff[0] = pChanData[2];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Int32_DC_BA)
		{
			int mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[3];
			pBuff[2] = pChanData[2];
			pBuff[1] = pChanData[1];
			pBuff[0] = pChanData[0];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Float_AB_CD)
		{
			float mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[0];
			pBuff[2] = pChanData[1];
			pBuff[1] = pChanData[2];
			pBuff[0] = pChanData[3];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Float_CD_AB)
		{
			float mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[2];
			pBuff[2] = pChanData[3];
			pBuff[1] = pChanData[0];
			pBuff[0] = pChanData[1];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Float_BA_DC)
		{
			float mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[1];
			pBuff[2] = pChanData[0];
			pBuff[1] = pChanData[3];
			pBuff[0] = pChanData[2];
			jVal = mbVal;
		}
		else if (storageFmt == STORAGE_FMT::Float_DC_BA)
		{
			float mbVal;
			char* pBuff = (char*)&mbVal;
			pBuff[3] = pChanData[3];
			pBuff[2] = pChanData[2];
			pBuff[1] = pChanData[1];
			pBuff[0] = pChanData[0];
			jVal = mbVal;
		}
	}
	else
	{
		char dataByte = pData[regOffsetResp / 8];
		int idx = regOffsetResp % 8;
		char mask = 1 << idx;
		bool val = (dataByte & mask) > 0;
		jVal = val;
	}
	return jVal;
}

json ioDev_ModbusSlave::acqModbusReg(string regType,string regAddr,string storageFmt, int regNum)
{
	if (regType == "" || regAddr == "" || storageFmt == "")
		return nullptr;

	json jRet;
	RTU_REQ_read req;
	memset(&req, 0, sizeof(req));
	req.eqp_addr = atoi(m_devAddr.c_str());
	req.fun_code = getFCode(regType);
unsigned short usRegAddr = atoi(regAddr.c_str());
req.start_reg_addr_L = LOBYTE(usRegAddr);
req.start_reg_addr_H = HIBYTE(usRegAddr);
req.reg_num_L = storageSize(storageFmt) / 2;
WORD crc = common::N_CRC16((unsigned char*)&req, sizeof(req) - 2);
req.crc_H = HIBYTE(crc);
req.crc_L = LOBYTE(crc);

PKT_DATA reqPkt((char*)&req, sizeof(req));
PKT_DATA respPkt;
if (RequestAndWaitResponse(reqPkt, respPkt))
{
	int retSize = respPkt.data[2];
	if (retSize != storageSize(storageFmt))
	{
		LOG("[ModbusRTU]请求的数据长度和实际返回的长度不一致");
		return nullptr;
	}

	if (storageFmt == STORAGE_FMT::UInt16)
	{
		unsigned short mbVal;
		memcpy(&mbVal, respPkt.data + 3, retSize);
		common::endianSwap((char*)&mbVal, 2);
		jRet = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Int16)
	{
		short mbVal;
		memcpy(&mbVal, respPkt.data + 3, retSize);
		common::endianSwap((char*)&mbVal, 2);
		jRet = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::UInt32)
	{
		unsigned int mbVal;
		memcpy(&mbVal, respPkt.data + 3, retSize);
		common::endianSwap((char*)&mbVal, 4);
		jRet = mbVal;
	}

	return jRet;
}
return nullptr;
}




bool ioDev_ModbusSlave::OnRecvData(char* pData, int iLen)
{
	statisOnRecv((char*)pData, iLen, getIOAddrStr());
	std::unique_lock<mutex> lock(m_csRecvBuff);
	m_recvBuff.PushStream(pData, iLen);
	m_recvSignal.notify();
	return true;
}

bool ioDev_ModbusSlave::checkRespValication(char* pData, int iLen, string& errorInfo)
{
	if (m_pCurrentIOCmd == nullptr)
		return false;

	unsigned char FCode = pData[1];
	unsigned int byteCount = pData[2];
	if (m_pCurrentIOCmd->fCode != FCode)
	{
		errorInfo = str::format("功能码不一致,请求功能码:%d,响应功能码:%d", m_pCurrentIOCmd->fCode, FCode);
		return false;
	}
	else
	{
		if (m_pCurrentIOCmd->fCode == MODBUS_FUNCTION_CODE::readHoldingRegisters ||
			m_pCurrentIOCmd->fCode == MODBUS_FUNCTION_CODE::readInputRegisters)
		{
			RTU_REQ_read* pReq = (RTU_REQ_read*)m_pCurrentIOCmd->data;
			if (pReq->getRegNum() * 2 != byteCount)
			{
				errorInfo = str::format("寄存器个数不一致,请求个数:%d,响应个数:%d", pReq->getRegNum(), byteCount / 2);
				return false;
			}
		}
		else if (m_pCurrentIOCmd->fCode == MODBUS_FUNCTION_CODE::readCoils ||
			m_pCurrentIOCmd->fCode == MODBUS_FUNCTION_CODE::readDiscreteInputs)
		{
			RTU_REQ_read* pReq = (RTU_REQ_read*)m_pCurrentIOCmd->data;
			if (byteCount * 8 >= pReq->getRegNum() &&
				byteCount * 8 - pReq->getRegNum() < 8)
			{

			}
			else
			{
				errorInfo = str::format("线圈个数不一致,请求个数:%d,响应字节数:%d", pReq->getRegNum(), byteCount);
				return false;
			}
		}
		else if (m_pCurrentIOCmd->fCode == MODBUS_FUNCTION_CODE::writeSingleCoil)
		{
			if (iLen == m_pCurrentIOCmd->len && memcmp(pData, m_pCurrentIOCmd->data, iLen) == 0)
			{}
			else
			{
				string req = str::bytesToHexStr(m_pCurrentIOCmd->data, m_pCurrentIOCmd->len);
				string resp = str::bytesToHexStr(pData, iLen);
				errorInfo = str::format("写线圈回包错误,请求包:%s,响应包:%s",req.c_str(),resp.c_str());
				return false;
			}
		}
	}

	return true;
}

bool ioDev_ModbusSlave::onRecvPkt(char* pData, int iLen)
{
	m_bOnline = true;
	std::unique_lock<recursive_timed_mutex> lock(m_csCommLock); //锁住m_pCurrentIOCmd
	if (!m_bRunning)return false;

	if (!m_bIsWaitingResp)
		return false;

	unsigned char FCode = pData[1];
	unsigned int byteCount = pData[2];
	char* pRegData = pData + 3;
	string errorInfo;
	string req = str::bytesToHexStr(m_pCurrentIOCmd->data, m_pCurrentIOCmd->len);
	string resp = str::bytesToHexStr(pData, iLen);
	unsigned char FCodeResp = 0x80 | m_pCurrentIOCmd->fCode;
	if (FCode == FCodeResp)
	{
		errorInfo = getExpCodeDesc(pData[2]);
		LOG("[warn][Modbus通讯]返回失败,错误信息:%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), req.c_str(), resp.c_str());
	}
	else if (!checkRespValication(pData, iLen, errorInfo))
	{
		LOG("[warn][Modbus通讯]请求与响应包不匹配,%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), req.c_str(), resp.c_str());
	}
	else
	{
		if (FCode == MODBUS_FUNCTION_CODE::readCoils ||
			FCode == MODBUS_FUNCTION_CODE::readDiscreteInputs ||
			FCode == MODBUS_FUNCTION_CODE::readHoldingRegisters ||
			FCode == MODBUS_FUNCTION_CODE::readInputRegisters)
		{
			for (int i = 0; i < m_pCurrentIOCmd->ioChannels.size(); i++)
			{
				ioChannel* pC = m_pCurrentIOCmd->ioChannels[i];
				json jVal = getChanDataFromBuff(pC, m_pCurrentIOCmd->offset, pRegData, iLen);
				pC->input(jVal);
			}
		}
		else if (FCode == MODBUS_FUNCTION_CODE::writeSingleCoil)
		{
			ioChannel* pC = m_pCurrentIOCmd->ioChannels[0];
			bool bVal = false;
			unsigned char cVal = pData[4];
			if (cVal == 0xFF) 
				bVal = true;
			json jVal = bVal;
			pC->input(jVal);
		}
	}

	setCurrentIOCmd(nullptr);
	return false;
}

unsigned char ioDev_ModbusSlave::funcName2funcCode(string name)
{
	if (name == MODBUS_REG_TYPE::coil)
	{
		return MODBUS_FUNCTION_CODE::readCoils;
	}
	else if (name == MODBUS_REG_TYPE::discreteInput)
	{
		return MODBUS_FUNCTION_CODE::readDiscreteInputs;
	}
	else if (name == MODBUS_REG_TYPE::holdingRegister)
	{
		return MODBUS_FUNCTION_CODE::readHoldingRegisters;
	}
	else if (name == MODBUS_REG_TYPE::inputRegister)
	{
		return MODBUS_FUNCTION_CODE::readInputRegisters;
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
	vector<MB_IO_CMD> acqCmd = chanList2MultiAcqCmd(f1List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f2List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f3List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f4List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
}

vector<MB_IO_CMD> ioDev_ModbusSlave::chanList2MultiAcqCmd(vector<ioChannel*>& list)
{
	vector<MB_IO_CMD> vecAcqCmd;
	vector<ioChannel*> batchAcqList;
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
			if (nextOffset - Offset > 100)
			{
				addCmd = true;
			}
		}


		//生成读取指令，清空批量获取的通道队列
		if (addCmd)
		{
			MB_IO_CMD ac = chanList2IOCmd(batchAcqList);
			vecAcqCmd.push_back(ac);
			batchAcqList.clear();
		}
	}
	return vecAcqCmd;
}

MB_IO_CMD ioDev_ModbusSlave::chanList2IOCmd(vector<ioChannel*>& list)
{
	ioChannel* lastChan = list[list.size() - 1];
	ioChannel* firstChan = list[0];
	unsigned char fCode = getFCode(firstChan->m_regType);
	unsigned short startRegOffset = list[0]->m_regOffset;
	//最后1个通道和第一个通道的偏移差
	unsigned short regNum = lastChan->m_regOffset - firstChan->m_regOffset;

	MB_IO_CMD ioCmd;

	if (fCode == MODBUS_FUNCTION_CODE::readHoldingRegisters || fCode == MODBUS_FUNCTION_CODE::readInputRegisters)
	{
		int lastChanSize = storageSize(lastChan->m_storageFmt); //最后1个通道字节数
		int lastChanRegNum = lastChanSize / 2;//最后1个通道寄存器数
		regNum += lastChanRegNum;
	}
	else
	{
		regNum += 1;
	}

	ioCmd.devAddr = atoi(getDevAddrStr().c_str());
	ioCmd.fCode = fCode;
	ioCmd.offset = startRegOffset;
	ioCmd.count = regNum;
	ioCmd.ioChannels = list;
	return ioCmd;
}

void ioDev_ModbusSlave::setCurrentIOCmd(MB_IO_CMD* p)
{
	if (m_pCurrentIOCmd)
		delete m_pCurrentIOCmd;
	m_pCurrentIOCmd = p;

	if (p)
	{
		m_bIsWaitingResp = true;
		GetLocalTime(&m_stLastReqSendTime);
		m_pCurrentIOCmd->stLastAcq = m_stLastAcqTime;
	}
	else
	{
		m_bIsWaitingResp = false;
	}
}

void ioDev_ModbusSlave::checkAcqReqTimeout()
{
	
}

bool MB_IO_CMD::pack()
{
	if (fCode == MODBUS_FUNCTION_CODE::readHoldingRegisters || 
		fCode == MODBUS_FUNCTION_CODE::readInputRegisters ||
		fCode == MODBUS_FUNCTION_CODE::readCoils ||
		fCode == MODBUS_FUNCTION_CODE::readDiscreteInputs)
	{
		RTU_REQ_read& req = *(new RTU_REQ_read());
		memset(&req, 0, sizeof(req));
		req.eqp_addr = devAddr;
		req.fun_code = fCode;
		req.start_reg_addr_L = LOBYTE(offset);
		req.start_reg_addr_H = HIBYTE(offset);
		req.reg_num_L = LOBYTE(count);
		req.reg_num_H = HIBYTE(count);
		WORD crc = common::N_CRC16((unsigned char*)&req, sizeof(req) - 2);
		req.crc_H = HIBYTE(crc);
		req.crc_L = LOBYTE(crc);
		data = (char*)&req;
		len = sizeof(req);
		return 1;
	}
	else if (fCode == MODBUS_FUNCTION_CODE::writeSingleCoil)
	{
		bool bVal = jOuputVal.get<bool>();
		RTU_REQ_writeSingleCoil& req = *(new RTU_REQ_writeSingleCoil());
		req.eqp_addr = devAddr;
		req.fun_code = MODBUS_FUNCTION_CODE::writeSingleCoil;
		req.addr_L = LOBYTE(offset);
		req.addr_H = HIBYTE(offset);
		req.val_L = 0;
		req.val_H = bVal ? 0xFF : 0;;
		WORD crc = common::N_CRC16((unsigned char*)&req, sizeof(req) - 2);
		req.crc_H = HIBYTE(crc);
		req.crc_L = LOBYTE(crc);
		data = (char*)&req;
		len = sizeof(req);
		return 1;
	}
	else if (fCode == MODBUS_FUNCTION_CODE::writeSingleRegister)
	{
		unsigned short usVal = jOuputVal.get<unsigned short>();
		RTU_REQ_writeSingleCoil& req = *(new RTU_REQ_writeSingleCoil());
		req.eqp_addr = devAddr;
		req.fun_code = MODBUS_FUNCTION_CODE::writeSingleRegister;
		req.addr_L = LOBYTE(offset);
		req.addr_H = HIBYTE(offset);
		req.val_L = LOBYTE(usVal);
		req.val_H = HIBYTE(usVal);
		WORD crc = common::N_CRC16((unsigned char*)&req, sizeof(req) - 2);
		req.crc_H = HIBYTE(crc);
		req.crc_L = LOBYTE(crc);
		data = (char*)&req;
		len = sizeof(req);
		return 1;
	}

	return 0;
}
