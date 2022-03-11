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
	m_pCurrentAcqCmd = nullptr;
}


ioDev_ModbusSlave::~ioDev_ModbusSlave(void)
{
}

bool ioDev_ModbusSlave::loadConf(json& conf)
{
	if (!ioDev::loadConf(conf))
		return false;
	generateAcqCmd();
	return true;
}



void ioDev_ModbusSlave::DoCycleTask()
{
	if (isAcqing())return;

	for (int i = 0; i < m_vecAcqCmd.size(); i++)
	{
		ACQ_CMD& ac = m_vecAcqCmd[i];
		if (timeopt::CalcTimePassSecond(ac.stLastAcq) > m_fAcqInterval)
		{
			//选择一条需要采集的命令发送请求，进入请求状态，表示设备正忙，设备正忙时不会发起新的请求
			m_bIsAcqing = true;
			m_pCurrentAcqCmd = &ac;
			RTU_REQ req;
			memset(&req, 0, sizeof(req));
			req.eqp_addr = atoi(getDevAddrStr().c_str());
			req.fun_code = ac.fCode;
			req.start_reg_addr_L = LOBYTE(ac.startRegOffset);
			req.start_reg_addr_H = HIBYTE(ac.startRegOffset);
			req.reg_num_L = LOBYTE(ac.regNum);
			req.reg_num_H = HIBYTE(ac.regNum);
			WORD crc = common::N_CRC16((unsigned char*)&req, sizeof(req) - 2);
			req.crc_H = HIBYTE(crc);
			req.crc_L = LOBYTE(crc);

			m_currentReq = req;
			sendData((char*)&req, sizeof(req));
			GetLocalTime(&m_stLastReqSendTime);
			return;
		}
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
	RTU_REQ req;
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

json ioDev_ModbusSlave::getChanDataFromBuff(ioChannel* pC, char* pData,int len)
{
	string storageFmt = pC->m_storageFmt;
	json jVal;
	if (storageFmt == STORAGE_FMT::UInt16)
	{
		unsigned short mbVal;
		memcpy(&mbVal, pData, 2);
		common::endianSwap((char*)&mbVal, 2);
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Int16)
	{
		short mbVal;
		memcpy(&mbVal, pData, 2);
		common::endianSwap((char*)&mbVal, 2);
		jVal = mbVal;
	}
	//AB CD 表示的是在返回的buff中的排序规则
	else if (storageFmt == STORAGE_FMT::Int32_AB_CD)
	{
		int mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[0];
		pBuff[2] = pData[1];
		pBuff[1] = pData[2];
		pBuff[0] = pData[3];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Int32_CD_AB)
	{
		int mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[2];  
		pBuff[2] = pData[3];
		pBuff[1] = pData[0];
		pBuff[0] = pData[1];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Int32_BA_DC)
	{
		int mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[1];
		pBuff[2] = pData[0];
		pBuff[1] = pData[3];
		pBuff[0] = pData[2];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Int32_DC_BA)
	{
		int mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[3];
		pBuff[2] = pData[2];
		pBuff[1] = pData[1];
		pBuff[0] = pData[0];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Float_AB_CD)
	{
		float mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[0];
		pBuff[2] = pData[1];
		pBuff[1] = pData[2];
		pBuff[0] = pData[3];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Float_CD_AB)
	{
		float mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[2];
		pBuff[2] = pData[3];
		pBuff[1] = pData[0];
		pBuff[0] = pData[1];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Float_BA_DC)
	{
		float mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[1];
		pBuff[2] = pData[0];
		pBuff[1] = pData[3];
		pBuff[0] = pData[2];
		jVal = mbVal;
	}
	else if (storageFmt == STORAGE_FMT::Float_DC_BA)
	{
		float mbVal;
		char* pBuff = (char*)&mbVal;
		pBuff[3] = pData[3];
		pBuff[2] = pData[2];
		pBuff[1] = pData[1];
		pBuff[0] = pData[0];
		jVal = mbVal;
	}

	return jVal;
}

json ioDev_ModbusSlave::acqModbusReg(string regType,string regAddr,string storageFmt, int regNum)
{
	if (regType == "" || regAddr == "" || storageFmt == "")
		return nullptr;

	json jRet;
	RTU_REQ req;
	memset(&req, 0, sizeof(req));
	req.eqp_addr = atoi(m_devAddr.c_str());
	req.fun_code = getFCode(regType);
	unsigned short usRegAddr = atoi(regAddr.c_str());
	req.start_reg_addr_L = LOBYTE(usRegAddr);
	req.start_reg_addr_H = HIBYTE(usRegAddr);
	req.reg_num_L = storageSize(storageFmt)/2;
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




bool ioDev_ModbusSlave::OnRecvData(char* pData,int iLen)
{	
	statisOnRecv((char*)pData,iLen,getIOAddrStr());
	std::unique_lock<mutex> lock(m_csRecvBuff);
	m_recvBuff.PushStream(pData, iLen);
	m_recvSignal.notify();
	return true;
}

bool ioDev_ModbusSlave::checkRespValication(char* pData, int iLen,string& errorInfo)
{
	unsigned char FCode = pData[1];
	unsigned int byteCount = pData[2];
	if (m_currentReq.fun_code != FCode)
	{
		errorInfo = str::format("功能码不一致,请求功能码:%d,响应功能码:%d", m_currentReq.fun_code, FCode);
		return false;
	}
	else if (m_currentReq.getRegNum() * 2 != byteCount)
	{
		errorInfo = str::format("寄存器个数不一致,请求个数:%d,响应个数:%d", m_currentReq.getRegNum(), byteCount/2);
		return false;
	}

	return true;
}

bool ioDev_ModbusSlave::onRecvPkt(char* pData, int iLen)
{
	unsigned char FCode = pData[1];
	unsigned int byteCount = pData[2];
	char* pRegData = pData + 3;
	string errorInfo;
	string req = str::bytesToHexStr((char*)&m_currentReq, sizeof(m_currentReq));
	string resp = str::bytesToHexStr(pData, iLen);
	unsigned char FCodeResp = 0x80 | m_currentReq.fun_code;
	if (FCode == FCodeResp)
	{
		errorInfo = getExpCodeDesc(pData[2]);
		LOG("[warn][Modbus通讯]返回失败,错误信息:%s,ioAddr:%s,请求:%s,响应:%s", errorInfo.c_str(), getIOAddrStr().c_str(), req.c_str(), resp.c_str());
	}
	else if (!checkRespValication(pData,iLen,errorInfo))
	{
		LOG("[warn][Modbus通讯]请求与响应包不匹配,%s,ioAddr:%s,请求:%s,响应:%s",errorInfo.c_str(), getIOAddrStr().c_str(), req.c_str(), resp.c_str());
	}
	else
	{
		if (FCode == MODBUS_FUNCTION_CODE::readHoldingRegisters)
		{
			for (int i = 0; i < m_pCurrentAcqCmd->ioChannels.size(); i++)
			{
				ioChannel* pC = m_pCurrentAcqCmd->ioChannels[i];
				int regOffsetResp = pC->m_regOffset - m_pCurrentAcqCmd->startRegOffset;
				char* pChanData = pRegData + regOffsetResp * 2;
				json jVal = getChanDataFromBuff(pC, pChanData,(iLen - regOffsetResp * 2));
				pC->input(jVal);
			}
		}
		else if (FCode == MODBUS_FUNCTION_CODE::readInputRegisters)
		{
			for (int i = 0; i < m_pCurrentAcqCmd->ioChannels.size(); i++)
			{
				ioChannel* pC = m_pCurrentAcqCmd->ioChannels[i];
				int regOffsetResp = pC->m_regOffset - m_pCurrentAcqCmd->startRegOffset;
				char* pChanData = pData + regOffsetResp * 2;
				json jVal = getChanDataFromBuff(pC, pChanData, (iLen - regOffsetResp * 2));
				pC->input(jVal);
			}
		}
		else if (FCode == MODBUS_FUNCTION_CODE::readCoils)
		{

		}
		else if (FCode == MODBUS_FUNCTION_CODE::readDiscreteInputs)
		{

		}
	}

	m_bIsAcqing = false;
	m_pCurrentAcqCmd = nullptr;
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
	vector<ACQ_CMD> acqCmd = chanList2MultiAcqCmd(f1List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f2List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f3List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2MultiAcqCmd(f4List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
}

vector<ACQ_CMD> ioDev_ModbusSlave::chanList2MultiAcqCmd(vector<ioChannel*>& list)
{
	vector<ACQ_CMD> vecAcqCmd;
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
			ACQ_CMD ac = chanList2AcqCmd(batchAcqList);
			vecAcqCmd.push_back(ac);
			batchAcqList.clear();
		}
	}
	return vecAcqCmd;
}

ACQ_CMD ioDev_ModbusSlave::chanList2AcqCmd(vector<ioChannel*>& list)
{
	ioChannel* lastChan = list[list.size() - 1];
	ioChannel* firstChan = list[0];
	ACQ_CMD ac;
	ac.fCode = getFCode(firstChan->m_regType);
	ac.startRegOffset = list[0]->m_regOffset;
	//最后1个通道和第一个通道的偏移差
	ac.regNum = lastChan->m_regOffset - firstChan->m_regOffset;
	int lastChanSize = storageSize(lastChan->m_storageFmt); //最后1个通道字节数
	int lastChanRegNum = lastChanSize / 2;//最后1个通道寄存器数
	ac.regNum += lastChanRegNum;
	ac.ioChannels = list;
	return ac;
}
