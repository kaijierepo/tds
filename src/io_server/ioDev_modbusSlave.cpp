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
	m_addrMode = DEV_ADDR_MODE::busAddr;
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
	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		for (int i = 0; i < m_vecChildDev.size(); i++)
		{
			ioChannel* pC = (ioChannel*)m_vecChildDev[i];
			json jVal = acqModbusReg(pC->m_regType, pC->m_devAddr, pC->m_storageFmt);
			if (!jVal.is_null())
				pC->input(jVal);
		}
		GetLocalTime(&m_stLastAcqTime);
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
	MRP_REQ_READ_REG req;
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

json ioDev_ModbusSlave::acqModbusReg(string regType,string regAddr,string storageFmt, int regNum)
{
	if (regType == "" || regAddr == "" || storageFmt == "")
		return nullptr;

	json jRet;
	MRP_REQ_READ_REG req;
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
		else if (storageFmt == STORAGE_FMT::Int32)
		{
			int mbVal;
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
	if (a->m_regOffset <= b->m_regOffset)
		return true;
	return false;
}

void ioDev_ModbusSlave::generateAcqCmd()
{
	return;
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
	std::sort(f3List.begin(), f3List.end(), sortChan);
	std::sort(f4List.begin(), f4List.end(), sortChan);

	//间隔不超过100个寄存器的通道，使用同1条命令读取
	vector<ACQ_CMD> acqCmd = chanList2AcqCmd(f1List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2AcqCmd(f2List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2AcqCmd(f3List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
	acqCmd = chanList2AcqCmd(f4List);
	m_vecAcqCmd.insert(m_vecAcqCmd.end(), acqCmd.begin(), acqCmd.end());
}

vector<ACQ_CMD> ioDev_ModbusSlave::chanList2AcqCmd(vector<ioChannel*>& list)
{
	vector<ACQ_CMD> vecAcqCmd;
	ACQ_CMD ac;
	for (int i = 0; i < list.size(); i++)
	{
		ioChannel* c = list[i];
		if(ac.fCode == 0)
			ac.fCode = funcName2funcCode(c->m_regType);
		//是否需要生成1条读取指令
		bool bAddCmd = false;
		if (i == 0)
		{
			bAddCmd = true;
		}
		else
		{
			unsigned short lastOffset = list[i - 1]->m_regOffset;
			unsigned short Offset = list[i]->m_regOffset;
			if (Offset - lastOffset > 100)
			{
				bAddCmd = true;
			}
		}

		if (bAddCmd)
		{
			if (ac.regNum > 0)
			{
				vecAcqCmd.push_back(ac);
			}

			ac.startRegOffset = c->m_regOffset;
			ac.regNum = 0;
		}
		else
		{
			ac.regNum++;
		}
	}
	return vecAcqCmd;
}
