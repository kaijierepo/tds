#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_leakDetect.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"

using namespace httplib;

ioDev* createDev_leakDetect()
{
	return new ioDev_leakDetect();
}


class createReg_leakDetect {
public:
	createReg_leakDetect();
};
createReg_leakDetect::createReg_leakDetect()
{
	mapDevCreateFunc[IO_DEV_TYPE::DEV::leakDetect] = createDev_leakDetect;
}
createReg_leakDetect regLeakDetect;


ioDev_leakDetect::ioDev_leakDetect()
{
	m_devType = IO_DEV_TYPE::DEV::leakDetect;
	m_devTypeLabel = "漏点监测设备";
	m_parentDevType = "tds";
	m_channelType = "io-point";
	m_channelTypeLabel = "IO点";
	m_level = "device";
	m_bIsReadingFile = false;
	m_bIsWaitingAcq = false;
}

ioDev_leakDetect::~ioDev_leakDetect()
{
}


void doSetAcqTime_thread(ioDev_leakDetect* p) {
	SYSTEMTIME stAcqTime;
	GetLocalTime(&stAcqTime);
	int acqDelay = 20;
	stAcqTime = timeopt::addTime(stAcqTime, 0, 0, acqDelay);
	LDP_PKT req,resp;
	char cmdData[3] = { 0 };
	cmdData[0] = stAcqTime.wHour;
	cmdData[1] = stAcqTime.wMinute;
	cmdData[2] = stAcqTime.wSecond;
	req.setID(atoi(p->getIOAddrStr().c_str()));
	req.rwFlag = RW_FLAG::WRITE;
	req.pack(LDP_CMD::setAcqTime_0x25, cmdData, 3);
	string acqTime = str::format("%02d:%02d:%02d", cmdData[0], cmdData[1], cmdData[2]);

	if (p->requestAndWaitResp(req, resp)) {
		LOG("[漏点监测]设置采集时间成功,时间:%s,设备ID:%s,延时:%d秒",acqTime.c_str(),p->getDevAddrStr().c_str(),acqDelay);
		p->m_bIsWaitingAcq = true;
	}
	else
	{
		LOG("[漏点监测]设置采集时间失败,错误信息:通信超时,时间:" + acqTime + ",设备ID:" + p->getDevAddrStr());
	}
}

void doAcqDataFile_thread(ioDev_leakDetect* p) {
	if (p->m_bIsReadingFile)
		return;

	p->m_bIsReadingFile = true;
	timeopt::sleepMilli(2000);
	SYSTEMTIME stNow;
	GetLocalTime(&stNow);
	LDP_PKT req, resp;
	LDP_READ_FILE_REQ cmd;
	int subPktNo = 0;
	int fileLen = 0;
	int totalPktCount = 0;
	p->m_fileBuff.Init();
	LOG("[漏点监测][开始]文件读取,设备地址:%s,", p->getDevAddrStr().c_str(), subPktNo);
	int retry = 0;
	while (retry > 3) {
		cmd.setSubPktNo(subPktNo);
		req.rwFlag = RW_FLAG::READ;
		req.setID(atoi(p->getIOAddrStr().c_str()));
		req.pack(LDP_CMD::readData_0x23, (char*)&cmd, sizeof(cmd));

		if (p->requestAndWaitResp(req, resp)) {
			LDP_READ_FILE_RESP* pRf = (LDP_READ_FILE_RESP*)resp.cmd_data;
			
			if (subPktNo == 0)
			{
				if (pRf->subPkt.head.isCompressFile()) {
					fileLen = pRf->subPkt.head.getFileLen() + 28;//gps数据和采样时间数据;
					totalPktCount = fileLen / 1000;
					if (fileLen % 1000)
						totalPktCount++;
					p->m_fileBuff.PushStream(pRf->subPkt.data, 1000);
				}
				else
				{
					LOG("[error][漏点监测][错误]未找到压缩文件标记,设备地址:%s", p->getDevAddrStr().c_str());
					break;
				}

			}
			else if (subPktNo == totalPktCount - 1) {
				int lastPktLen = 1000;
				if (fileLen % 1000 > 0)
					lastPktLen = fileLen % 1000;
				p->m_fileBuff.PushStream(pRf->subPkt.data, lastPktLen);
			}
			else
			{
				p->m_fileBuff.PushStream(pRf->subPkt.data, 1000);
			}
			LOG("[漏点监测][成功]读取文件分包,设备地址:%s,分包号:%d,总包数:%d", p->getDevAddrStr().c_str(), subPktNo, totalPktCount);
			


			if (subPktNo == totalPktCount - 1 && p->m_fileBuff.iStreamLen < fileLen) {
				LOG("[error][漏点监测][错误]已读取最后一包，读取的长度小于文件长度,设备地址:%s,读取长度:%d,文件长度:%d", p->getDevAddrStr().c_str(), p->m_fileBuff.iStreamLen, fileLen);
				break;
			}

			if (p->m_fileBuff.iStreamLen > fileLen) {
				LOG("[error][漏点监测][错误]读取的长度大于文件长度,设备地址:%s,读取长度:%d,文件长度:%d", p->getDevAddrStr().c_str(), p->m_fileBuff.iStreamLen , fileLen);
				break;
			}
			
			if (p->m_fileBuff.iStreamLen == fileLen)
			{
				LOG("[漏点监测][结束]文件读取成功,设备地址:%s,总长度:%d,总分包数:%d", p->getDevAddrStr().c_str(), fileLen, totalPktCount);
				LDP_FILE_TAIL* pTail = (LDP_FILE_TAIL*)(p->m_fileBuff.stream + (fileLen - sizeof(LDP_FILE_TAIL)));
				SYSTEMTIME stNow;
				GetLocalTime(&stNow);
				stNow.wHour = pTail->acqTimeHour;
				stNow.wMinute = pTail->acqTimeMin;
				stNow.wSecond = pTail->acqTimeSec;
				json de= json::object();
				db.Insert(p->m_strTagBind, stNow, de);
				db.saveDEFile(p->m_strTagBind, stNow,p->m_fileBuff.stream, p->m_fileBuff.iStreamLen,"ld.dat");
				break;
			}

			subPktNo++;
			continue;
		}
		else
		{
			LOG("[error][漏点监测]读取文件超时，重试读取,设备地址:%s,分包号:%d" ,p->getDevAddrStr().c_str(),subPktNo);
			retry++;
			continue;
		}
	}
	p->m_bIsReadingFile = false;
	p->m_bIsWaitingAcq = false;
}

void ioDev_leakDetect::DoCycleTask()
{
	if (!m_bOnline)
		return;

	if (m_dispositionMode == DEV_DISPOSITION_MODE::spare)
		return;

	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval) {
		GetLocalTime(&m_stLastAcqTime);

		if (!m_bIsWaitingResp && !m_bIsWaitingAcq)
		{
			thread t(doSetAcqTime_thread, this);
			t.detach();
		}
	}
}

bool ioDev_leakDetect::onRecvPkt(char* pData, int iLen)
{
	LDP_PKT resp;
	resp.unpack((unsigned char*)pData, iLen);

	//通知单类命令处理
	if (resp.cmdCode == LDP_CMD::acqSignal_0x22) {
		if (resp.cmd_data[0] == ACQ_SIGNAL::START_0xAA) {
			LOG("[漏点监测]开始AD采样,设备地址:%s", getDevAddrStr().c_str());
		}
		else if (resp.cmd_data[0] == ACQ_SIGNAL::END_0x55) {
			LOG("[漏点监测]结束AD采样,设备地址:%s", getDevAddrStr().c_str());
			thread t(doAcqDataFile_thread, this);
			t.detach();
		}
	}
	else if (resp.cmdCode == LDP_CMD::heatbeat_0xFF) {

	}
	//请求响应类命令，放入响应缓存
	else if(m_bIsWaitingResp && resp.cmdCode == m_currentReq.cmdCode){
		//以下写法主要为了方便打断点调试
		if (resp.cmdCode == LDP_CMD::readData_0x23)
		{
			m_currentResp = resp;
			m_respSignal.notify();
		}
		else if (resp.cmdCode == LDP_CMD::setAcqTime_0x25) {
			m_currentResp = resp;
			m_respSignal.notify();
		}
	}
	//收到没有同步等待的响应命令
	else {
		string pkt = str::bytesToHexStr(pData, iLen);
		LOG("[漏点监测]收到没有同步等待的响应命令,设备地址:%s,数据:%s",getDevAddrStr().c_str(), pkt.c_str());
	}

	return false;
}


bool ioDev_leakDetect::getCurrentVal()
{
	return false;
}

bool ioDev_leakDetect::sendData(char* pData, int iLen)
{
	unique_lock<mutex> lock(m_csIOSession);
	if (pIOSession)
	{
		pIOSession->send(pData, iLen);
		if (m_bEnableIoLog)
			statisOnSend((char*)pData, iLen, getIOAddrStr());
	}
	else
		return false;
	return true;
}

bool ioDev_leakDetect::requestAndWaitResp(LDP_PKT& req, LDP_PKT& resp)
{
	CommLock();
	m_currentReq = req;
	bool ret = false;
	if (sendData((char*)req.data, req.len))
	{
		m_bIsWaitingResp = true;
		if (m_respSignal.wait_for(12000)) {
			resp = m_currentResp;
			ret = true;
		}
		else
		{
			ret = false;
		}
		m_bIsWaitingResp = false;
	}
	CommUnlock();
	return ret;
}


