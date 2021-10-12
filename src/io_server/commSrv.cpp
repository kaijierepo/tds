#include "pch.h"
#include "commSrv.h"
#include "common.hpp"
#include "logger.h"
#include "json.hpp"
#include "conf.h"
#include  "ioSrv.h"
#include  "tdscore.h"

using json = nlohmann::json;

commServer commSrv;
vector<std::shared_ptr<TDS_SESSION>> commpktSessions;
void sendToCommLog(string s)
{
	for (int i = 0; i < commpktSessions.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = commpktSessions[i];
		if (!session->bConnected)
		{
			commpktSessions.erase(commpktSessions.begin() + i);
			i--;
			continue;
		}
			

		session->send((char*)s.c_str(), s.length());
	}
}

//接受数据异步处理线程
DWORD WINAPI ThreadRecvPktAsynDeal(LPVOID lparam) {
	commServer* pThis = (commServer*)lparam;

	while (pThis->DealPackageAsyn()) {
		Sleep(1);
	}
	return 0;
}

REQ_PARAM::REQ_PARAM()
{
	iRetryCount = 1;
	iWaitTime = 3000;
	strLogMsgWhenSend = "";
}

void PktQueue::Clear()
{
	if (!queueBufPacket.empty()) {
		Lock();
		while (!queueBufPacket.empty()) {
			PKT_DATA* pkt = queueBufPacket.front();
			queueBufPacket.pop();

			string strLog;
			strLog = "[通信]丢弃数据包[";

			for (int i = 0; i < pkt->m_iDataBufLen; i++) {
				string str = str::format("%02X", char(pkt->m_DataBuf[i]));
				strLog += str;
			}
			strLog += "]";
			LOG(strLog);
			delete pkt;
		}
		Unlock();
	}
}

commServer::commServer(void) : m_threadPackageDeal(NULL)
{
	m_tcpServer.SettIOCPName("CommSrv");
	m_RemoteBridgeSock = 0;
	m_bEndSession = false;


	m_threadPackageDeal = CreateThread(NULL, 0, ThreadRecvPktAsynDeal, this, 0, NULL);
	if (!m_threadPackageDeal) {
		string strLog;
		strLog = "创建缓存包处理线程失败!";
		LOG(strLog);
	}
}


commServer::~commServer(void)
{
	
}



void commServer::Run()
{
	string confPath = tds->conf->projectConfPath + "\\commSrv.json";
	string confData;
	fs::readFile(confPath, confData);
	auto jc = json::parse(confData);

	int iPort = 5011;
	
	m_tcpServer.keepAliveTimeout = 5 * 60;//5分钟清一次 设备端涉及到高速载波总线中的tcp通信，tcp的协议交互容易出现异常。海南观察到过一个高速缺口存在2个tcp连接
	if (!m_tcpServer.run(this, iPort))
	{
		if (m_tcpServer.m_lastError == 10048)//通常每个套接字地址 (协议/网络地址/端口)只允许使用一次。
		{
			string strData = str::format("[错误]端口%d 被占用，服务启动失败.", iPort);
			LOG(strData);
		}
	}
}


bool commServer::CheckIsGateWay(string strIP)
{
	ioAddress addr;
	addr.devAddr = strIP;
	addr.gwAddr = "";
	ioDev* p = ioSrv.getIODev(addr);
	if (p && p->IsGateway())
	{
		CAN_TRANSMIT_BUF* buf = new CAN_TRANSMIT_BUF;
		buf->type = p->m_devType;
		//buf->gwType = p->GetGatewayType();
		commSrv.m_mapGateWay[strIP] = buf;
		return true;
	}
	else
	{
		/*if (TestIsGateWay(strIP))
		{
			CAN_TRANSMIT_BUF* buf = new CAN_TRANSMIT_BUF;
			buf->type = JET_LOWSPEED_PLC_GATEWAY;
			buf->gwType = GW_USER_PROTO_OVER_CAN;
			m_mapGateWay[strIP] = buf;
			return true;
		}*/
	}

	return false;
}

void commServer::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{

}

void commServer::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		if (CheckIsGateWay(pCltInfo->strIP))
		{
			CAN_TRANSMIT_BUF* buf = m_mapGateWay[pCltInfo->strIP];
			pCltInfo->bIsTransmit = true;
			//pCltInfo->pData1 = buf;
		}
		else
		{
			
		}
	}
}

void commServer::OnRecvData(char* pData, int iLen, tcpSessionClt* connInfo)
{
}

// 315服务端数据接收
void commServer::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
{
	OnRecvData_EqpAppLayerData(pData, iLen, "", connInfo->srvIP);
}

void commServer::OnRecvData_EqpAppLayerPkt(PKT_DATA* ppd, ioAddrSession* pAddrInfo)
{
	//bind ioDev and ioAddr
	if (pAddrInfo->pIODev == NULL)
	{
		ioDev* pRun = ioSrv.getIODev(ppd->addr);
		//do new ioDev discovered routine
		if (pRun == NULL)
		{
			
		}
		//bind
		if (pRun)
		{
			pAddrInfo->pIODev = pRun;
			pRun->m_pCommAddrInfo = pAddrInfo;
		}
	}

	if (pAddrInfo->pIODev && pAddrInfo->pIODev->m_bOnline == false)
	{
		pAddrInfo->pIODev->m_bOnline = true;
		GetLocalTime(&pAddrInfo->pIODev->m_stEqpOnLineDateTime);

		string strData = "";
		strData = str::format("[设备连接]设备上线，设备地址:%s", ppd->addr.ToString());
		LOG(strData);
	}

	//确定应用层包进行同步处理还是异步处理
	//异步处理由低层向上回调线程进行处理； 同步处理放入缓存由应用层线程进行处理
	if (pAddrInfo->addr.proto == APP_LAYER_PROTO::MODBUS_RTU)
	{
		ppd->dealType = RECV_PKT_ASYN;
	}
	else
	{
		ppd->dealType = RECV_PKT_ASYN;
	}
	commSrv.StatisOnRecv((char*)ppd->m_DataBuf, ppd->m_iDataBufLen, ppd->addr, ppd->dealType);

	//push队列必须放在所有操作之后，使得某一时刻只有一个线程在操作ppd,否则从队列中取出的线程和当前线程可能同时操作ppd，造成奔溃
	if (ppd->dealType == RECV_PKT_ASYN) {  // 通知包
		pAddrInfo->AysnPktQueue.Push(ppd);
	}
	else if (ppd->dealType == RECV_PKT_SYNC) { // 响应包
		pAddrInfo->SyncPktQueue.Push(ppd);
	}
}

void commServer::OnRecvData_EqpAppLayerData(char* pData, int iLen, string strID, string strIP, bool bIsWholePkt)
{
	ioAddress addr;
	addr.devAddr = strIP;
	addr.gwAddr = strID;

	ioAddrSession* pAddrInfo = GetCommAddrInfo(addr);


	if (bIsWholePkt)
	{
		PKT_DATA* ppd = new PKT_DATA(pData, iLen);
		ppd->addr = addr;
		ppd->proto = pAddrInfo->addr.proto;
		OnRecvData_EqpAppLayerPkt(ppd, pAddrInfo);
	}
	else
	{
		stream2pkt* pab = &pAddrInfo->stream2pkt;
		pab->PushStream(pData, iLen);
		//设备上线时，会根据设备的配置指定该地址的协议，如果协议已知，则只提取该协议的数据包
		//如果协议为unknow，pAddrInfo->addr.proto初始化为unknown,则尝试提取所有已知协议的数据包
		while (pab->PopPkt(pAddrInfo->addr.proto))
		{
			PKT_DATA* ppd = new PKT_DATA(pab->pkt, pab->iPktLen);
			ppd->addr = addr;
			ppd->proto = pab->m_protocolType;
			OnRecvData_EqpAppLayerPkt(ppd, pAddrInfo);
		}
	}
}


void commServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
	//本函数实现传输层数据到应用层数据转换
	//pCltInfo中的 iData1 用来建立 ID 映射关系
	//1.如果为Can传输层数据
	if (pCltInfo->bIsTransmit) {
		CAN_TRANSMIT_BUF* pGWBuf = NULL;//(CAN_TRANSMIT_BUF*)pCltInfo->pData1;
		if (pGWBuf->type == "local_serial")//透明网关
		{
			OnRecvData_EqpAppLayerData(pData, iLen, "", pCltInfo->strIP);
		}
		else if (pGWBuf->type == "can_gateway")
		{
			OnRecvData_EqpAppLayerData(pData, iLen, "", pCltInfo->strIP);
		}
		else //can网关
		{
			//打印中继数据收发信息
			ioAddress addr;
			addr.devAddr = pCltInfo->strIP;
			addr.gwAddr = "";
			StatisOnRecv((char*)pData, iLen, addr);

			if (iLen % 13 != 0)
			{
				string strData = "";
				string strPkt;
				strPkt  = str::fromBytes((char*)pData, iLen );

				if (iLen <= 30)
					strData = str::format("[异常]收到Can中继[%s]数据长度不是13的倍数,可能是异常数据包,丢弃,长度:%d,数据:%s",
						pCltInfo->strIP, iLen, strPkt);
				else
					strData = str::format("[异常]收到Can中继[%s]数据长度不是13的倍数,可能是异常数据包,丢弃,长度:%d,数据:过长数据不打印",
						pCltInfo->strIP, iLen);

				LOG(strData);
				return;
			}

			//放入can中继缓冲
			CAN_TRANSMIT_BUF* pCanBuf = NULL;//(CAN_TRANSMIT_BUF*)pCltInfo->pData1;
			memcpy(pCanBuf->canBuff + pCanBuf->iBuffLen, pData, iLen);
			pCanBuf->iBuffLen += iLen;

			while (pCanBuf->iBuffLen >= 13)
			{
				//解析出CanID
				CAN_FRAME* pCanPkt = (CAN_FRAME*)pCanBuf->canBuff;
				int iID = 0;
				if (pCanPkt->IsCanEx())
				{
					CAN_PKT_V2* pCanExPkt = (CAN_PKT_V2*)pCanBuf->canBuff;
					char bAddress = ((pCanExPkt->sHead.Address1) << 5) | (pCanExPkt->sHead.Address);
					iID = (int)bAddress;
				}
				else
				{
					char* pIID = (char*)&iID;
					*pIID = pCanPkt->byIDLowLow;
					*(pIID + 1) = pCanPkt->byIDLowHigh;
					*(pIID + 2) = pCanPkt->byIDHighLow;
					*(pIID + 3) = pCanPkt->byIDHighHigh;
				}

				//获得地址信息
				ioAddress addr;
				addr.devAddr = pCltInfo->strIP;
				addr.gwAddr = str::format("%d", iID);
				ioAddrSession* pAddrInfo = GetCommAddrInfo(addr);

				//根据地址协议类型处理
				if (pGWBuf->type == "can_gateway")
				{
					//将can载荷上送到应用层组包
					string strID;
					strID =str::format("%d", iID);
					OnRecvData_EqpAppLayerData(pCanBuf->canBuff + 5, pCanPkt->DLC, strID, pCltInfo->strIP);
				}
				else if (pGWBuf->type == "can_gateway_frame")
				{
					if (pAddrInfo->addr.tlProto == TRANSFER_LAYER_PROTO_TYPE::TLT_CAN_V2)
					{
						bool bWholtPkt = false;
						CAN_PKT_V2* pCanExPkt = (CAN_PKT_V2*)pCanBuf->canBuff;
						char bAddress = ((pCanExPkt->sHead.Address1) << 5) | (pCanExPkt->sHead.Address);
						iID = (int)bAddress;
						addr.gwAddr = str::format("%d", iID);

						if (pAddrInfo->addr.proto == APP_LAYER_PROTO::PROTOCOL_FRAMING_PROTOCOL)
						{
							OnRecvData_EqpAppLayerData(pCanBuf->canBuff + 5, pCanPkt->DLC, addr.gwAddr, addr.devAddr);
						}
						else
						{
							if (pCanExPkt->sHead.InxFrame == 0 && (pCanExPkt->sHead.Type == 4 || pCanExPkt->sHead.Type == 0)) //can单帧数据
							{
								memcpy(pCanBuf->canPayloadBuff + pCanBuf->iPayloadBuffLen, pCanExPkt->arrData, pCanExPkt->sHead.DLC);
								pCanBuf->iPayloadBuffLen += pCanExPkt->sHead.DLC;
								bWholtPkt = true;
							}
							else if (pCanExPkt->sHead.InxFrame == 0x0 && pCanExPkt->sHead.Type == 3)   //分包传输的第一帧
							{
								if (*(DWORD*)(pCanExPkt->arrData) == 0xAA55A800)    //特殊标记结束
								{
									memcpy(pCanBuf->canPayloadBuff + pCanBuf->iPayloadBuffLen, pCanExPkt->arrData, pCanExPkt->sHead.DLC);
									pCanBuf->iPayloadBuffLen += pCanExPkt->sHead.DLC;
									bWholtPkt = true;
								}
								else
								{
									memcpy(pCanBuf->canPayloadBuff + pCanBuf->iPayloadBuffLen, pCanExPkt->arrData, pCanExPkt->sHead.DLC);
									pCanBuf->iPayloadBuffLen += pCanExPkt->sHead.DLC;
								}

							}
							else if (pCanExPkt->sHead.InxFrame > 0x1 && pCanExPkt->sHead.Type == 2) //分包传输的最后一帧
							{
								memcpy(pCanBuf->canPayloadBuff + pCanBuf->iPayloadBuffLen, pCanExPkt->arrData, pCanExPkt->sHead.DLC);
								pCanBuf->iPayloadBuffLen += pCanExPkt->sHead.DLC;
								bWholtPkt = true;
							}
							else
							{
								memcpy(pCanBuf->canPayloadBuff + pCanBuf->iPayloadBuffLen, pCanExPkt->arrData, pCanExPkt->sHead.DLC);
								pCanBuf->iPayloadBuffLen += pCanExPkt->sHead.DLC;
							}

							if (bWholtPkt)
							{
								//将can载荷上送到应用层
								string strID;
								strID = str::format("%d", iID);
								OnRecvData_EqpAppLayerData(pCanBuf->canPayloadBuff, pCanBuf->iPayloadBuffLen, strID, pCltInfo->strIP, true);
								pCanBuf->iPayloadBuffLen = 0;
							}
						}
					}
					else if (pAddrInfo->addr.proto == TRANSFER_LAYER_PROTO_TYPE::TLT_CAN_V1)
					{
						OnRecvData_EqpAppLayerData(pCanBuf->canBuff + 5, pCanPkt->DLC, addr.gwAddr, addr.devAddr);
					}
				}

				//删除第一个can包
				memcpy(pCanBuf->canBuff, pCanBuf->canBuff + 13, pCanBuf->iBuffLen - 13);
				pCanBuf->iBuffLen -= 13;
			}
		}
	}
	else
	{
		//2.TCP应用层数据=设备应用数据 。直接上传到应用层	
		OnRecvData_EqpAppLayerData(pData, iLen, "", pCltInfo->strIP);
	}
}

void commServer::Stop()
{
	m_tcpServer.stop();
}

ioAddrSession* commServer::GetCommAddrInfo(ioAddress addr)
{
	ioAddrSession* p = NULL;
	m_csRecvBuffListLock.lock();
	auto pos = m_mapCommAddrInfo.find(addr);
	if (pos == m_mapCommAddrInfo.end()) {
		ioAddrSession* pCai = new ioAddrSession();
		ioDev* pRun = ioSrv.getIODev(addr);
		if (pRun)
			pCai->addr = pRun->getIOAddr(); //从配置设备可以取到协议类型
		else
			pCai->addr = addr; //只有ip和id
		m_mapCommAddrInfo[addr] = pCai;
	}
	p = m_mapCommAddrInfo[addr];
	m_csRecvBuffListLock.unlock();
	return p;
}



ioAddrSession* commServer::GetCommAddrInfo(string iID, string strIP)
{
	//根据IP和ID信息  获取 ioAddrSession 的信息结构体
	std::map<ioAddress, ioAddrSession*>::iterator mapiter;
	for (mapiter = m_mapCommAddrInfo.begin(); mapiter != m_mapCommAddrInfo.end(); mapiter++)
	{
		if (mapiter->first.gwAddr == iID && mapiter->first.devAddr == strIP)
		{
			return mapiter->second;
		}
	}

	return NULL;
}

void commServer::sendCanV1(char* pData, int iLen, string strIP, int iID)
{
	
}

void commServer::sendCanV3(char* pData, int iLen, string strIP, int iID)
{
	CAN_FRAME req;
	memset(&req, 0, 13);
	req.byIDLowLow = iID;
	req.byIDLowHigh = iID >> 8;
	memcpy(req.arrData, pData, iLen);
	req.DLC = iLen;

	m_tcpServer.SendData((char*)&req, 13, strIP);
}


void commServer::sendCanV2(char* pData, int iLen, string strIP, int iID)
{
	
}


void CommServer_SendData(char* pData, int iLen, ioAddress addr)
{
	commSrv.SendData(pData, iLen, addr);
}

bool commServer::SendData(char* pData, int iLen, ioAddress addr)
{
	StatisOnSend((char*)pData, iLen, addr);


	if (m_tcpClientList.find(addr) != m_tcpClientList.end())
	{
		tcpClt* pTcpClient = m_tcpClientList[addr];
		pTcpClient->SendData((char*)pData, iLen);
	}
	else
	{
		//如果外部没有指定协议，查找内部缓存的协议
		if (addr.gwType == GW_UNKNOWN || addr.tlProto == TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN)
		{
			ioAddrSession* p = GetCommAddrInfo(addr);
			if (p)
				addr = p->addr;
		}

		//中继
		if (addr.gwAddr != "")
		{
			if (addr.gwType == GW_CAN_TRANSPARENT)
			{
				if (addr.tlProto == TRANSFER_LAYER_PROTO_TYPE::TLT_CAN_V1)
					sendCanV1((char*)pData, iLen, addr.devAddr, atoi(addr.gwAddr.c_str()));
				else if (addr.tlProto == TRANSFER_LAYER_PROTO_TYPE::TLT_CAN_V2)
					sendCanV2((char*)pData, iLen, addr.devAddr, atoi(addr.gwAddr.c_str()));
			}
			else 
				m_tcpServer.SendData((char*)pData, iLen, addr.devAddr);

			return true;
		}
		else//原始数据
		{
			m_tcpServer.SendData((char*)pData, iLen, addr.devAddr);
			return true;
		}
	}
	return false;
}


bool commServer::SendCanFrameRaw(void* buf, int iDataLen, int iID)
{
	return true;
}


void commServer::CommLock(ioAddress addr)
{
	ioAddrSession* pAddrInfo = GetCommAddrInfo(addr);
	pAddrInfo->CommLock();
}


bool commServer::CommLock(ioAddress addr, int iMilliSecond)
{
	ioAddrSession* pAddrInfo = GetCommAddrInfo(addr);
	return pAddrInfo->CommLockWithTime(iMilliSecond);
}

void commServer::CommUnlock(ioAddress addr)
{
	ioAddrSession* pAddrInfo = GetCommAddrInfo(addr);
	pAddrInfo->CommUnlock();
}


void commServer::ClearRecvBuff(ioAddress addr)
{
	PktQueue* ptrPktBuffer = NULL;
	// 清除响应队列
	if (m_mapCommAddrInfo.find(addr) != m_mapCommAddrInfo.end()) {
		m_mapCommAddrInfo[addr]->SyncPktQueue.Clear();
	}
}

bool commServer::GetResponse(PKT_DATA& req, PKT_DATA& resp, ioAddress addr, REQ_PARAM* reqParam)
{
	ioAddrSession* pCommInfo = GetCommAddrInfo(addr);
	if (!pCommInfo) return false;

	//检查是否收到了与请求包对应的响应包
		while (1)
		{
			//取出一个
			PKT_DATA* ppd = pCommInfo->SyncPktQueue.Pop();
			if (!ppd)
				break;


			//校验是否是与请求对应的响应
			bool bSameCmd = ppd->GetCmdID() == req.GetCmdID();

			if (bSameCmd)
			{
				resp = *ppd;

				delete ppd;
				return true;
			}
			else
			{
				string strData = "";
				//直接用函数返回值当作参数  产生崩溃，原因不明，这里用string临时接收返回值，的这一方案作为暂时解决措施，后续再查
				string addrStr = addr.ToString();

				string reqJepDescStr = req.GetCmdName();
				string reqJepIDStr = req.GetCmdID();
				string jepDescStr = ppd->GetCmdName();
				string jepIDStr = ppd->GetCmdID();
				strData = str::format("[异常]收到回包与发出包命令号不匹配，设备地址:%s,发出包：%s(%s) %s，回包：%s(%s) %s,丢弃数据包",
					addrStr, reqJepDescStr, reqJepIDStr, "", jepDescStr, jepIDStr, "");
				LOG(strData);
			}

			delete ppd;
		}

	return false;
}


bool commServer::IsAddrConnected(ioAddress addr)
{
	return m_tcpServer.IsIPOnline(addr.devAddr);
}



bool commServer::RequestAndWaitResponse(PKT_DATA* req, PKT_DATA* resp, ioAddress addr, REQ_PARAM* reqParam)
{
	string strCmd = ""; //该变量用于统计rtt时缓存cmd
	if (this == NULL)
		return false;

	//准备请求命令相关的参数，如果无参数，使用默认参数
	REQ_PARAM reqParamTemp;
	if (!reqParam)
		reqParam = &reqParamTemp;
	if(reqParam->strCmdID=="")
	reqParam->strCmdID = req->GetCmdID();
	ioAddrSession* pCommInfo = GetCommAddrInfo(addr);

	bool bRet = false;
	bool bEndComm = false;

	if (!pCommInfo)
		return false;

	int iRttStart, iRttEnd, iRtt;
	for (int iRetry = 0; iRetry < reqParam->iRetryCount; iRetry++)
	{
		if (iRetry >= 1)
		{
			//只有在线设备通讯不上才记录下面的日志，不会产生大量日志
			req->m_strCmdName = req->GetCmdName();
			string strLog;
			strLog = str::format("[警告]commServer::RequestAndWaitResponse,重试次数:%d,addr=%s,%s,%s", iRetry, addr.ToString(), req->m_strCmdName, req->m_strCmdContent);
			LOG(strLog);
		}

		//锁通信
		//CommLock(addr);
		int iStart = GetTickCount();
		pCommInfo->CommLock();
		int iEnd = GetTickCount();
		if (iEnd - iStart > 100)
		{
			req->m_strCmdName = req->GetCmdName();
			string strLog;
			strLog = str::format("[警告]commServer::RequestAndWaitResponset通信锁等待，等待时间:%d,addr=%s,%s,%s", iEnd - iStart, addr.ToString(), req->m_strCmdName, req->m_strCmdContent);

			if (iEnd - iStart > 1000)
			{
				LOG(strLog);
			}
			else
			{
				LOG(strLog);
			}
		}
		pCommInfo->strInSyncCmdID = reqParam->strCmdID;

		//清空接收缓存
		pCommInfo->SyncPktQueue.Clear();

		iRttStart = GetTickCount();
		iRttEnd = -1;
		bool bSendRet = SendData(req->m_DataBuf, req->m_iDataBufLen, addr);
		int iWaitTime = reqParam->iWaitTime;//默认3000毫秒

		if (reqParam->strLogMsgWhenSend != "")
		{
			string strSendInfo;
			strSendInfo = str::format("(WaitTime:%d,Retry:%d/%d)", iWaitTime, iRetry, reqParam->iRetryCount);
			reqParam->strLogMsgWhenSend += strSendInfo;
			LOG(reqParam->strLogMsgWhenSend);
		}

		while (iWaitTime > 0)
		{
			Sleep(20);//实时图片界面调用该函数，必须使用pumpmessage
			iWaitTime -= 20;

			if (GetResponse(*req, *resp, addr, reqParam))
			{
				iRttEnd = GetTickCount();

				bRet = true;
				bEndComm = true;
				break;
			}

			if (m_bEndSession)//只有实时图片界面会让它为true，退出while和for
			{
				bRet = false;
				bEndComm = true;
				break;
			}
		}

		//释放通信锁
		//CommUnlock(addr.strIP);
		pCommInfo->strInSyncCmdID = "";
		pCommInfo->CommUnlock();

		//是否结束
		if (bEndComm)
			break;
	}

	if (!bRet)
	{
		pCommInfo->pIODev->m_iSendDataFailCount++;

		req->m_strCmdName = req->GetCmdName();
		string strLog;
		strLog = str::format("[警告]commServer::RequestAndWaitResponset通讯失败,重试:%d次,累计失败:%d次,addr=%s,%s,%s", reqParam->iRetryCount, pCommInfo->pIODev->m_iSendDataFailCount,
			addr.ToString(), req->m_strCmdName, req->m_strCmdContent);
		LOG(strLog);
	}
	else
		pCommInfo->pIODev->m_iSendDataFailCount = 0;

	//计算rtt，当appMode时，设备在线时才做这些；当远程模式时，设备在不在线都做
	strCmd = req->GetCmdName();
	if (strCmd.length() > 0) {
		//计算平均往返时间
		if (iRttEnd != -1) {
			iRtt = iRttEnd - iRttStart;
		}
		else {
			iRtt = -1;//通信失败
		}
		static mutex csRttStatis;//该函数会被多线程调用，此处加互斥锁
		csRttStatis.lock();
		//得到统计列表
		vector<int>* pTmpVec;
		if (m_mapCmdRtt.find(strCmd) != m_mapCmdRtt.end()) {
			pTmpVec = m_mapCmdRtt[strCmd];
		}
		else {
			pTmpVec = new vector<int>(); //目前没必要释放
			m_mapCmdRtt[strCmd] = pTmpVec;
		}
		//如果超过10个统计，删除最早的一个
		if (pTmpVec->size() >= 10) {
			pTmpVec->erase(pTmpVec->begin());//删除头部第一个元素
		}
		pTmpVec->push_back(iRtt);//元素插入尾部
		csRttStatis.unlock();
	}

END:
	if (!bRet && pCommInfo->pIODev && pCommInfo->pIODev->m_iSendDataFailCount >=3 && pCommInfo->pIODev->m_bOnline == true)
	{
		pCommInfo->pIODev->m_bOnline = false;
		GetLocalTime(&pCommInfo->pIODev->m_stEqpOffLineDateTime);

		string strData = "";
		strData = str::format("[设备连接]设备掉线,设备地址:%s", addr.ToString());
		LOG(strData);
	}

	return bRet;
}


void commServer::StatisOnRecv(char* recvData, int len, ioAddress addr, recvPktType dealType)
{
	json j;
	SYSTEMTIME st;
	GetLocalTime(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["ioAddr"] = addr.ToString();
	j["type"] = "接收";
	j["len"] = len;
	j["data"] = str::fromBytes(recvData, len);
	string s = j.dump();

	sendToCommLog(s);
}

void commServer::StatisOnSend(char* sendData, int len, ioAddress addr)
{
	json j;
	SYSTEMTIME st;
	GetLocalTime(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["ioAddr"] = addr.ToString();
	j["type"] = "发送";
	j["len"] = len;
	j["data"] = str::fromBytes(sendData, len);
	string s = j.dump();

	sendToCommLog(s);
}



bool commServer::DealPackageAsyn()
{
	std::map<ioAddress, ioAddrSession*>::iterator i;

	
	//先一次性取出所有地址。该列表可能被另外一个线程添加成员。需要锁住
	vector<ioAddrSession*> addrList;
	m_csRecvBuffListLock.lock();
	for (i = m_mapCommAddrInfo.begin(); i != m_mapCommAddrInfo.end(); i++)
	{
		ioAddrSession* pi = i->second;
		addrList.push_back(pi);
	}
	m_csRecvBuffListLock.unlock();


	//取出所有的等待异步处理的数据包
	vector<PKT_DATA*> pktList;
	for (int i = 0; i < addrList.size(); i++)
	{
		ioAddrSession* pi = addrList[i];
		pi->m_queueAysnLock.lock();
		while (1)
		{
			PKT_DATA* ppd = pi->AysnPktQueue.Pop();
			if (!ppd) {
				break;
			}
			pktList.push_back(ppd);
		}
		pi->m_queueAysnLock.unlock();
	}

	
	//处理所有异步数据包.在调用OnRecvData时，确保此时没有锁住任何锁。因为OnRecvData内部代码未知，万一有锁，锁套锁容易死锁
	for (int i = 0; i < pktList.size(); i++)
	{
		PKT_DATA* ppd = pktList[i];
		ioDev* pRun = ioSrv.getIODev(ppd->addr);
		if (pRun && pRun->m_pMO)
		{
			pRun->OnRecvData(ppd->m_DataBuf, ppd->m_iDataBufLen);
		}
		delete ppd;
	}

	return 1;
}


void ioAddrSession::CommLock()
{
	while (!CommLockWithTime(20)) {
		Sleep(20);
	}
	m_dwLockThread = GetCurrentThreadId();
}

bool ioAddrSession::CommLockWithTime(int dwTimeoutMS)
{
	if (!tds->conf->bConcurrentGateway && addr.gwAddr.length() > 0) //和串行网关下的一个设备通信，锁中继
	{
		ioAddress gwAddr = addr;
		gwAddr.gwAddr = "";
		ioAddrSession* pGw = commSrv.GetCommAddrInfo(gwAddr);
		if (pGw)
			return pGw->CommLockWithTime(dwTimeoutMS);
		else
			return true;
	}
	else
	{
		if (dwTimeoutMS)
		{
			chrono::milliseconds timeout(dwTimeoutMS);
			return m_csCommLock.try_lock_for(timeout);
		}
		else
		{
			m_csCommLock.lock();
			return true;
		}
	}
}
