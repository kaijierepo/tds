#include "pch.h"
#include "ioDev_bacnet.h"
#include "ioChan.h"
#include "logger.h"
#include "common.h"
#include "ioSrv.h"
#include "webSrv.h"

ioDev_searcher_bacnet* pSearcher = nullptr;

void searchDev_bacnet() {
	if (pSearcher == nullptr) {
		pSearcher = new ioDev_searcher_bacnet();
	}

	pSearcher->search();
}


ioDev* createDev_bacnet()
{
	return new ioDev_bacnet();
}

class createReg_bacnet {
public:
	createReg_bacnet();
};
createReg_bacnet::createReg_bacnet()
{
	mapDevCreateFunc["bacnet-ip"] = createDev_bacnet;
	mapDevTypeLabel["bacnet-ip"] = "BacNet/IP";
	mapDevSearchFunc["bacnet-ip"] = searchDev_bacnet;
}
createReg_bacnet regBacnet;


ioDev_bacnet::ioDev_bacnet(void)
{
	m_devType = "bacnet-ip";
	m_bCycleAcqThreadRunning = false;
	m_level = "device";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	memset(sessionHandle, 0, 4);
	m_invokeId = 0;
}


ioDev_bacnet::~ioDev_bacnet(void)
{
}

bool ioDev_bacnet::run()
{
	m_bRunning = true;
	if (m_addrType == DEV_ADDR_MODE::udpServer)
	{
		string ip;
		if (m_jDevAddr.contains("ip"))
		{
			ip = m_jDevAddr["ip"].get<string>();
		}
		else
		{
			LOG("[error]IODev启动失败,设备地址模式=%s,没有找到ip配置", m_addrType.c_str());
			return false;
		}


		int port;
		if (m_jDevAddr.contains("port"))
		{
			port = m_jDevAddr["port"].get<int>();
		}
		else
		{
			LOG("[error]IODev启动失败,设备地址模式=%s,没有找到port配置", m_addrType.c_str());
			return false;
		}


		if (m_udpClt == nullptr) {
			m_udpClt = new UdpClt();
		}
		m_udpClt->m_remoteIP = ip;
		m_udpClt->m_remotePort = port;
		m_udpClt->run(this);

		LOG("[IO设备]启动设备,地址模式:%s,设备类型:%s,设备地址:%s", m_addrType.c_str(), m_devType.c_str(), getDevAddrStr().c_str());
	}
	return true;
}

void ioDev_bacnet::stop()
{
	m_bRunning = false;  //设为false以后，应当不会有新的线程尝试占用通信锁
	m_transaction.m_respSignal.notify(); //中断所有等待中的会话

	if(m_udpClt)
		m_udpClt->stop();

	//此处怎么设计找沈春种或者廖灰灰讨论一次。有没有基于信号量的方法
	while (m_bWorkingThreadRunning) {
		timeopt::sleepMilli(1);
	}
}



bool ioDev_bacnet::loadConf(json& conf)
{
	ioDev::loadConf(conf);
	run();
	return true;
}

void ioDev_bacnet::output(string chanAddr, json jVal, json& rlt,json& err, bool sync)
{
	ioChannel* pC = getChanByDevAddr(chanAddr);
	if (pC)
		output(pC, jVal, rlt,err, sync);
	else
		return ;
}

void ioDev_bacnet::output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync)
{
	if (pC->m_jDevAddr["objType"].is_string() && pC->m_jDevAddr["instNo"].is_number_unsigned()) {
	}
	else {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devAddrFmtError, "dev addr format error"));
		return;
	}
		

	string objType = pC->m_jDevAddr["objType"];
	unsigned int instNo = pC->m_jDevAddr["instNo"].get<unsigned int>();

	BACNET_OBJECT_TYPE baType = getBacnetObjType(objType);
	BACNET_VAL bacVal;

	if (jVal.is_boolean()) {
		bacVal.bVal = jVal.get<bool>();
	}
	else if (jVal.is_number()) {
		bacVal.fVal = jVal.get<float>();
	}
	else {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "value type not supported"));
		return;
	}

	BACNET_PKT req,resp;
	req.pack_write_current_val(m_invokeId++, baType, instNo,bacVal);
	string errInfo;
	if (doTransaction(req, resp, errInfo)) {
		if (errInfo == "") {
			rlt = "ok";
		}
		else {
			err = errInfo;
		}
	}
	else {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::IO_reqTimeout, "time out"));
	}
}

bool ioDev_bacnet::isCommBusy()
{
	assert(m_pParent);
	return m_pParent->isCommBusy();
}

bool ioDev_bacnet::isConnected()
{
	return true;
}

bool ioDev_bacnet::disconnect()
{
	return false;
}



void cycleAcq_thread(ioDev_bacnet* pDev) {
	//采样周期为0进入全速采集模式，不退出线程
	while (pDev->m_bRunning) {
		for (int i = 0; i < pDev->m_channels.size(); i++)
		{
			ioChannel* pC = pDev->m_channels[i];

			if (pC->m_jDevAddr["objType"].is_string() && pC->m_jDevAddr["instNo"].is_number_unsigned()) {
				string objType = pC->m_jDevAddr["objType"];
				unsigned int instNo = pC->m_jDevAddr["instNo"].get<unsigned int>();

				BACNET_PKT req,resp;
				req.pack_read_current_val(pDev->m_invokeId, getBacnetObjType(objType), instNo);
				pDev->m_invokeId++;
				

				string s;
				s = str::bytesToHexStr(req.data, req.len);

				string errInfo;
				if (pDev->doTransaction(req, resp, errInfo)) {
					unsigned char* pData = resp.data;
					unsigned char* p = pData;
					p = (pData + sizeof(BVLC_Header));
					NPDU_Header* pNPDU = (NPDU_Header*)p;
					if (pNPDU->ctrl.dest == 0 && pNPDU->ctrl.src == 0) {
						p +=2;
						APDU_Type* pAPDU = (APDU_Type*)p;
						if (pAPDU->type == APDU_TYPE::COMPLEX_ACK) {
							p += 1;
							unsigned char invokeId = *(p);
							p += 1;
							unsigned char service_choice = *(p);
							if (service_choice == Bacnet_Service::Read_Property_Multiple) {
								p += 1;
								Bacnet_Tag* pBt = (Bacnet_Tag*)p;
								p += 1;
								Object_Identifier* objIdent = (Object_Identifier*)p;
								p += 4;
								pBt = (Bacnet_Tag*)p; //opening tag
								p += 1;
								pBt = (Bacnet_Tag*)p; //context specific tag,context tag number=2,length_value_type=1
								p += 1;
								unsigned char property_ident = *p;
								if (property_ident == 0x55) //present value
								{
									p += 1;
									pBt = (Bacnet_Tag*)p; //opening tag
									p += 1;
									pBt = (Bacnet_Tag*)p; //application tag
									p += 1;
									if (pBt->TagNumber == Application_Tag_Number::Real) {

										float fVal = *((float*)p);
										double dbVal = fVal;
										pC->input(dbVal);
									}
									else if (pBt->TagNumber == Application_Tag_Number::Enumerated) {
										unsigned char ucVal = *p;
										bool bVal = ucVal > 0 ? true : false;
										pC->input(bVal);
									}
								}
							}
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


void ioDev_bacnet::DoCycleTask()
{
	if (!m_bEnableAcq)return;
	if (!m_bRunning)return;


	if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval)
	{
		if (!m_bCycleAcqThreadRunning)
		{
			// 任务触发线程， 执行线程， 标记位  3者的逻辑关系
			//由任务触发线程将标志位置为true,可以保证线程一定不会被触发产生2个。线程退出标志位才置为false
			//不要将 标志位置为true 放在线程里面执行。这样逻辑上触发线程可能触发2个线程出来。如果执行线程还没有置位，触发线程已经判断为false
			//这样就会产生2个执行线程实例
			m_bCycleAcqThreadRunning = true;
			thread t(cycleAcq_thread, this);
			t.detach();
			timeopt::now(&m_stLastAcqTime);
		}
	}
}

bool ioDev_bacnet::isEipSessionOpen()
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
bool ioDev_bacnet::doTransaction(BACNET_PKT& pduReq, BACNET_PKT& pduResp, string& errorInfo)
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


bool ioDev_bacnet::CommLock(int dwTimeoutMS)
{
	return m_pParent->CommLock(dwTimeoutMS);
}

void ioDev_bacnet::CommUnlock()
{
	m_pParent->CommUnlock();
}


size_t isValidPkt_BACNET_IP(unsigned char* p, size_t iLen)
{
	if (iLen < 4)
		return 0;

	BVLC_Header* pHeader = (BVLC_Header*)p;

	if (pHeader->type != 0x81) {
		return 0;
	}

	if (iLen < pHeader->getLen()) {
		return 0;
	}

	return pHeader->getLen();
}



void ioDev_bacnet::onEvent_online()
{

}


ioDev_searcher_bacnet::ioDev_searcher_bacnet()
{
	udpClt.run(this, 47809);
}

ioDev_searcher_bacnet::~ioDev_searcher_bacnet()
{
}


vector<string> getAllBroadCastAddr() {
	vector<string> vec;
	char hostname[256];
	gethostname(hostname, sizeof(hostname));
	struct hostent* host = gethostbyname(hostname);
	if (host == NULL)
	{
		return vec;
	}

	map<string, string> mapAddr;
	while (*(host->h_addr_list) != NULL) //输出ipv4地址
	{
		string ip = inet_ntoa(*(struct in_addr*)*host->h_addr_list);
		size_t pos = ip.rfind(".");
		if (pos > 0) {
			ip = ip.substr(0, pos);
			ip += ".255";
		}
		mapAddr[ip] = ip;
		host->h_addr_list++;
	}

	for (auto& i : mapAddr) {
		vec.push_back(i.first);
	}

	return vec;
}




void ioDev_searcher_bacnet::search()
{
	string sPkt = "81 0B 00 0C 01 20 FF FF 00 FF 10 08";
	vector<unsigned char> pkt = str::hexStrToBytes(sPkt);

	vector<string> addrs = getAllBroadCastAddr();

	for (int i = 0; i < addrs.size(); i++) {
		string ip = addrs[i];
		udpClt.SendData(pkt.data(), pkt.size(), ip, BACNET_IP_PORT);
	}
}


/*   I am 响应数据包结构
X'lO'       PDU Type = l(Unconfirmed - Service - Request - POU)
X'OO'       Service Choice = O(I - A, n - Request)
X'C4'       Application Tag 12 (Object Identifier, L 4) (I - Am Device Identifier)
X'02000003' Device, Instance Number = 3
X'22'       Application Tag 2 (Unsigned Integer, L = 2) (Max APDU Length Accepted)
X'0400'	    1024
X'91'       Application Tag 9 (Enumerated, L = l) (Segmentation Supported)
X'03'       3 (NO SEGMENTATION)
X'21'       Application Tag 2 (Unsigned Integer, L = 1) (Vendor ID)
X'63'       99
*/
void ioDev_searcher_bacnet::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	IOLogRecv(recvData, recvDataLen, udpSession);

	if(isValidPkt_BACNET_IP(recvData, recvDataLen)) {
		NPDU_Header* pNPDU = (NPDU_Header*)(recvData + sizeof(BVLC_Header));
		if (pNPDU->ctrl.dest == 0 && pNPDU->ctrl.src == 0) {
			APDU_Type* pAPDU = (APDU_Type*)(recvData + sizeof(BVLC_Header) + 2);
			if (pAPDU->type == APDU_TYPE::UNCONFIRMED_REQUEST) {
				unsigned char service_choice = *((unsigned char*)pAPDU + 1);
				if (service_choice == Bacnet_Service::I_Am) {
					Bacnet_Tag* pBT = (Bacnet_Tag*)((char*)pAPDU + 2);
					if (pBT->TagNumber == Application_Tag_Number::BACnetObjectldentifier) {
						json jAddr;
						jAddr["type"] = DEV_ADDR_MODE::udpServer;
						jAddr["ip"] = udpSession.remoteIP;
						jAddr["port"] = udpSession.remotePort;
						ioSrv.onChildDevDiscovered(jAddr, udpSession.getRemoteIOAddr(), "bacnet-ip");
					}
				}
			}
		}
	}
}


void ioDev_bacnet::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	string remoteAddr = "UDP-" + udpSession.remoteIP + ":" + str::fromInt(udpSession.remotePort);
	string localAddr = "UDP-" + udpSession.localIP + ":" + str::fromInt(udpSession.localPort);
	IOLogRecv(recvData, recvDataLen, remoteAddr, localAddr,getIOAddrStr());
	setOnline();

	if (isValidPkt_BACNET_IP(recvData, recvDataLen)) {
		NPDU_Header* pNPDU = (NPDU_Header*)(recvData + sizeof(BVLC_Header));
		if (pNPDU->ctrl.dest == 0 && pNPDU->ctrl.src == 0) {
			APDU_Type* pAPDU = (APDU_Type*)(recvData + sizeof(BVLC_Header) + 2);
			//读数据 返回 pAPDU->type == APDU_TYPE::COMPLEX_ACK 
			//写数据 返回 pAPDU->type == APDU_TYPE::SIMPLE_ACK
			unsigned char invokeId = *((unsigned char*)pAPDU + 1);
			if (invokeId == m_transaction.m_transactionId) {
				BACNET_PKT* resp = new BACNET_PKT();
				resp->setData(recvData, recvDataLen);
				if(m_bIsWaitingResp)
					m_transaction.setResp(resp);
				delete resp;
			}
		}
	}
}