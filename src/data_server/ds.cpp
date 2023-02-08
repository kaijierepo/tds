#include "pch.h"
#include "ds.h"
#include "logger.h"
#include "prj.h"
#include "mp.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "tcpClt.h"
#include "tdsConf.h"
#include "tds.h"
#include "users/userMng.h"
#include "ioChan.h"

dataServer ds;

dataServer::dataServer()
{
}

dataServer::~dataServer()
{
}

void dataServer::rpc_startStreamPush(json params, RPC_RESP& resp, RPC_SESSION session)
{
	if (params["srcTag"] == nullptr) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param srcTag missing");
		return;
	}
	if (params["destTag"] == nullptr) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param destTag missing");
		return;
	}
	if (!params["port"].is_number_integer()) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param port error");
		return;
	}
	string srcTag = params["srcTag"];
	string destTag = params["destTag"];
	int port = params["port"].get<int>();

	ioChannel* pChan = ioSrv.getChanByTag(srcTag);
	if (pChan == nullptr) {
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "channel bind specified tag not found");
		return;
	}

	tcpClt* pClt = new tcpClt();
	if (pClt->connect(this, session.hostName, port)) {

		tcpSessionClt* pTcpSess = &pClt->m_session;
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		pChan->m_csStreamPuller.lock();
		pChan->m_vecStreamPuller.push_back(p);
		pChan->m_csStreamPuller.unlock();

		LOG("[数据流   ]  推流服务连接成功,服务地址:%s:%d,源位号:%s,目标位号:%s", pTcpSess->srvIP.c_str(), pTcpSess->srvPort,srcTag.c_str(),destTag.c_str());
		sendStreamPusherRegPkt(p, destTag);

		resp.result = RPC_OK;
		m_tcpClt_streamPusher[pClt] = pClt;
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "connect fail");
		delete pClt;
	}
}

void dataServer::sendChildTdsRegPkt(std::shared_ptr<TDS_SESSION> p)
{
	json jReg;
	jReg["method"] = "devRegister";
	json jParams;
	jParams["devType"] = "childTds";
	jParams["httpPort"] = tds->conf->httpPort;
	jParams["httpsPort"] = tds->conf->httpsPort;
	jReg["params"] = jParams;
	string s = jReg.dump() + "\n\n";
	p->sendStr(s);
}

void dataServer::sendStreamPusherRegPkt(std::shared_ptr<TDS_SESSION> p,string tag)
{
	json jReg;
	jReg["method"] = "devRegister";
	json jParams;
	jParams["devType"] = "streamPusher";
	jParams["tag"] = tag;
	jReg["params"] = jParams;
	string s = jReg.dump() + "\n\n";
	p->sendStr(s);
}

void dataServer::statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		m_mutexSessions.lock();
		m_Sessions[pTcpSess] = p;
		m_mutexSessions.unlock();
	}
	else
	{
		m_mutexSessions.lock();
		std::shared_ptr<TDS_SESSION> p = m_Sessions[pTcpSess];
		m_Sessions.erase(pTcpSess);
		m_mutexSessions.unlock();
		//更新该session状态。等待其他零散指针引用销毁后自动删除
		p->onTcpDisconnect();
	}
}

void tdsEdgeRegisterThread(std::shared_ptr<TDS_SESSION> p)
{
	Sleep(1000);
	//向服务器发送注册包
	json j;
	j["method"] = "devRegister";
	j["ioAddr"] = tds->conf->deviceID;

	string s = j.dump() + "\n\n";
	p->send((char*)s.c_str(), s.length());
}


void deleteTcpClt(tcpClt* p) {
	delete p;
}

void dataServer::statusChange_tcpClt(tcpSessionClt* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		if (m_tcpClt_ParentTds.find( pTcpSess->tcpClt)!= m_tcpClt_ParentTds.end()) {
			std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
			m_mutexSessions.lock();
			m_Sessions[pTcpSess] = p;
			m_mutexSessions.unlock();

			LOG("[主从服务]连接到主服务成功,%s:%d", pTcpSess->srvIP.c_str(), pTcpSess->srvPort);
			sendChildTdsRegPkt(p);
		}
	}
	else
	{
		if (m_tcpClt_ParentTds.find( pTcpSess->tcpClt)!= m_tcpClt_ParentTds.end()){
			LOG("[warn][主从服务]从主服务断开,%s:%d", pTcpSess->srvIP.c_str(), pTcpSess->srvPort);

			m_mutexSessions.lock();
			std::shared_ptr<TDS_SESSION> p = m_Sessions[pTcpSess];
			m_Sessions.erase(pTcpSess);
			m_mutexSessions.unlock();
			p->onTcpDisconnect();
		}

		if (m_tcpClt_streamPusher.find(pTcpSess->tcpClt) != m_tcpClt_streamPusher.end()) {
			LOG("[warn][数据流  ]推流客户端断开,%s:%d", pTcpSess->srvIP.c_str(), pTcpSess->srvPort);

			m_csTcpClt_streamPusher.lock();
			m_tcpClt_streamPusher.erase(pTcpSess->tcpClt);
			m_csTcpClt_streamPusher.unlock();

			thread t(deleteTcpClt, pTcpSess->tcpClt);
			t.detach();
		}

	}
}

int dataServer::Send(SOCKET sock, char* pBuffer, int iLength)
{
	return send(sock, pBuffer, iLength, 0);
}


void streamPusherMng_thread() {
	while (1) {
		Sleep(2000);
		vector<string> toErase;
		prj.m_csPrj.lock_shared();
		for (auto i : ds.m_mapPullerActive) {
			TIME st = i.second;
			if (timeopt::CalcTimePassSecond(st) > 5) {
				MP* pmp = prj.GetMPByTag(i.first);
				string src = "?";
				string status = "";
				if (pmp) {

				}
				else {
					status = "位号未找到";
				}
				LOG("[流媒体]5秒没有活动的媒体客户端，断开媒体源，tag=%s,src=%s,status=%s", i.first.c_str(), src.c_str(),status.c_str());
				toErase.push_back(i.first);
			}
		}
		prj.m_csPrj.unlock_shared();

		for (int i = 0; i < toErase.size(); i++) {
			ds.m_mapPullerActive.erase(toErase[i]);
		}
	}
}

bool dataServer::run()
{
	string masterTdsAddrs = tds->conf->getStr("masterTds", "");

	vector<string> vecAddrs;
	str::split(vecAddrs, masterTdsAddrs, ",");


	for (int i = 0; i < vecAddrs.size(); i++) {
		string addr = vecAddrs[i];
		tcpClt* p = new tcpClt();
		p->m_keepAliveTimeout = 10;
		p->run(this, addr);
		m_tcpClt_ParentTds[p] = p;

		LOG("[子服务模式] 连接到上级服务%s", addr.c_str());
	}

	return false;
}

void dataServer::stop()
{
	LOG("[keyinfo]正在停止数据服务DataServer...");
	LOG("[keyinfo]数据服务已停止");
}



/*
生产者-临时消费者模式  
该机制主要为了避免 处理接收数据时间过长，影响数据接收的实时性

tdsSessionProcessThread  为消费者，临时线程
OnRecvData_TCPServer 为生产者，常驻线程
tdsSession->dataBuff 为任务队列
当任务队列中有数据时，该模式控制 必有1个消费者 且 只有1个消费者

此处使用队列的原因。
不能直接将OnRecvData_TCPServer收到的数据多线程调用tdsSessionProcessThread去处理
因为可能网络中一个大数据包可能会被分包为多次回调，触发多个tdsSessionProcessThread之后，
多线程可能不按照数据流本身的先后顺序执行处理，导致数据包分片数据错误从而导致处理出错
*/
void tdsSessionProcessThread(std::shared_ptr<TDS_SESSION> tdsSession)
{
	//控制只有一个 - m_bSessionProcessing为false才能进入，因此不会出现两个工作者，
	//控制必有一个 - 此处如果return后，创建消费者线程的代码前面的代码已经插入了新任务，并且解锁后已存在工作者一定会进行一次待办任务确认，不会有不被执行的任务
	tdsSession->m_mutexTcpBuff.lock();
	if (tdsSession->m_bSessionProcessing)
	{
		tdsSession->m_mutexTcpBuff.unlock();//必须保证此处unlock后，已有的消费者一定会去检查任务队列
		return;
	}
	tdsSession->m_bSessionProcessing = true;
	tdsSession->m_mutexTcpBuff.unlock();

	while(1)
	{
		//取出任务
		//是否继续工作判断。 当其他线程获得锁，并且m_bSessionProcessing==true时，当前消费者线程一定还在while循环当中
		tdsSession->m_mutexTcpBuff.lock();
		if (tdsSession->dataBuff.size() == 0)
		{
			tdsSession->m_bSessionProcessing = false;
			tdsSession->m_mutexTcpBuff.unlock();
			break;
		}
		TCP_DATA_BUFF tdb = tdsSession->dataBuff.front();
		tdsSession->dataBuff.pop();
		tdsSession->m_mutexTcpBuff.unlock();

		//执行任务
		ds.OnRecvData_TCP(tdb.pData, tdb.iLen, tdsSession);
		delete tdb.pData;
	}
}

void dataServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	m_mutexSessions.lock();
	std::shared_ptr<TDS_SESSION> tdsSession = m_Sessions[pTcpSess];
	m_mutexSessions.unlock();
	OnRecvData_TCP(pData, iLen, tdsSession);
}

void dataServer::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* pTcpSess)
{
	m_mutexSessions.lock();
	std::shared_ptr<TDS_SESSION> tdsSession = m_Sessions[pTcpSess];
	m_mutexSessions.unlock();
	OnRecvData_TCP(pData, iLen, tdsSession);
}





void dataServer::OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	timeopt::now(&tdsSession->lastRecvTime);
	stream2pkt& tlBuf = tdsSession->m_tlBuf;
	tlBuf.PushStream((unsigned char*)pData, iLen);
	while (tlBuf.PopPkt(IsValidPkt_textEnd_LFLF, false))
	{
		string req = str::fromBuff((char*)tlBuf.pkt, tlBuf.iPktLen);

		if (req == "ping\n\n") { 
			string s = "pong\n\n";
			tdsSession->send((unsigned char*)s.data(), s.length(), false);
		}
		else if (req == "pong\n\n") {

		}
		else {
			RPC_RESP resp;
			rpcSrv.handleRpcCall(req, resp, tdsSession, false);
			tdsSession->send((unsigned char*)resp.strResp.data(), resp.strResp.length(), false);
		}
	}
}



void dataServer::sendToAllSessions(unsigned char* pData, int len)
{
	m_mutexSessions.lock();
	for (auto& i : m_Sessions) {
		i.second->send(pData, len);
	}
	m_mutexSessions.unlock();
}

void dataServer::sendToAllSessions(string& s)
{
	sendToAllSessions((unsigned char*)s.c_str(), s.length());
}
