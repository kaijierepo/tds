#include "pch.h"
#include  "reverseInterface.h"
#include "logger.h"
#include "prj.h"
#include "mp.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "tcpClt.h"
#include "tdsConf.h"
#include "tds.h"
#include "userMng.h"
#include "ioChan.h"

ReverseInterface reverseInterface;

ReverseInterface::ReverseInterface()
{
}

ReverseInterface::~ReverseInterface()
{
	stop();
}

void ReverseInterface::rpc_startStreamPush(json params, RPC_RESP& resp, RPC_SESSION session)
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
	if (pClt->connect(this, session.remoteIP, port)) {

		tcpSessionClt* pTcpSess = &pClt->m_session;

		//创建TDS Session
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		pTcpSess->pALSession = p;
		p->name = "数据流推流客户端";
		p->type = TDS_SESSION_TYPE::dataStream;

		//相对于本地io通道来说是拉流
		pChan->m_csStreamPuller.lock();
		pChan->m_vecStreamPuller.push_back(p);
		pChan->m_csStreamPuller.unlock();

		LOG("[数据流   ]  推流服务连接成功,服务地址:%s:%d,源位号:%s,目标位号:%s", session.remoteIP.c_str(), port,srcTag.c_str(),destTag.c_str());
		sendStreamPusherRegPkt(p, destTag);

		resp.result = RPC_OK;
		m_tcpClt_streamPusher[pClt] = pClt;
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "connect fail");
		delete pClt;
	}
}

void ReverseInterface::sendChildTdsRegPkt(std::shared_ptr<TDS_SESSION> p)
{
	json jReg;
	jReg["method"] = "devRegister";
	json jParams;
	jParams["devType"] = "childTds";
	jParams["httpPort"] = tds->conf->httpPort;
	jParams["httpsPort"] = tds->conf->httpsPort;
	jReg["params"] = jParams;
	jReg["addr"] = prj.m_name;
	string s = jReg.dump() + "\n\n";
	p->sendStr(s);
}

void ReverseInterface::sendStreamPusherRegPkt(std::shared_ptr<TDS_SESSION> p,string tag)
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

void ReverseInterface::statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		m_mutexSessions.lock();
		m_reverseTdsSessions[pTcpSess] = p;
		m_mutexSessions.unlock();
	}
	else
	{
		m_mutexSessions.lock();
		std::shared_ptr<TDS_SESSION> p = m_reverseTdsSessions[pTcpSess];
		m_reverseTdsSessions.erase(pTcpSess);
		m_mutexSessions.unlock();
		//更新该session状态。等待其他零散指针引用销毁后自动删除
		p->onTcpDisconnect();
	}
}

void tdsEdgeRegisterThread(std::shared_ptr<TDS_SESSION> p)
{
	timeopt::sleepMilli(1000);
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

void ReverseInterface::statusChange_tcpClt(tcpSessionClt* pTcpSess, bool bIsConn)
{
	std::shared_ptr<TDS_SESSION> p = pTcpSess->pALSession;
	if (bIsConn)
	{
		p->pTcpSessionClt = pTcpSess;
		m_mutexSessions.lock();
		m_reverseTdsSessions[pTcpSess] = p;
		m_mutexSessions.unlock();
		LOG("[TCP连接  ] 连接到服务,名称:%s,类型:%s,服务地址:%s",p->name.c_str(),p->type.c_str(), p->getRemoteAddr().c_str());
		
		
		if (p->type == TDS_SESSION_TYPE::tdsClient) {
			if (p->m_bParentTds) {
				sendChildTdsRegPkt(p);
			}
		}
	}
	else
	{
		LOG("[warn][TCP连接  ]从服务断开,名称:%s,类型:%s,服务地址:%s", p->name.c_str(), p->type.c_str(), p->getRemoteAddr().c_str());
		p->onTcpDisconnect();

		m_mutexSessions.lock();
		m_reverseTdsSessions.erase(pTcpSess);
		m_mutexSessions.unlock();

		//删除推流客户端对象
		m_csTcpClt_streamPusher.lock();
		if (m_tcpClt_streamPusher.find(pTcpSess->pTcpClt) != m_tcpClt_streamPusher.end()) {
			m_tcpClt_streamPusher.erase(pTcpSess->pTcpClt);
			thread t(deleteTcpClt, pTcpSess->pTcpClt);
			t.detach();
		}
		m_csTcpClt_streamPusher.unlock();

		//子服务客户端对象不删除，不断重连
	}
}

void ReverseInterface::onTcpCltEvent_error(tcpClt* pClt, string error)
{
	LOG("[warn]Tcp客户端,remoteAddr=%s:%d,错误事件,%s",pClt->m_remoteIP.c_str(),pClt->m_remotePort,error.c_str());
}


void streamPusherMng_thread() {
	while (1) {
		timeopt::sleepMilli(2000);
		vector<string> toErase;
		prj.m_csPrj.lock_shared();
		for (auto i : reverseInterface.m_mapPullerActive) {
			TIME st = i.second;
			if (timeopt::CalcTimePassSecond(st) > 5) {
				MP* pmp = prj.GetMPByTag(i.first,"zh");
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
			reverseInterface.m_mapPullerActive.erase(toErase[i]);
		}
	}
}

bool ReverseInterface::run()
{
	string masterTdsAddrs = tds->conf->getStr("masterTds", "");
	string childTdsIP = tds->conf->getStr("childTdsIP", "");

	if (masterTdsAddrs != "") {
		vector<string> vecAddrs;
		str::split(vecAddrs, masterTdsAddrs, ",");

		LOG("[服务级联  ] 连接到上级服务 %s,绑定本地地址:%s", masterTdsAddrs.c_str(), childTdsIP.c_str());

		for (int i = 0; i < vecAddrs.size(); i++) {
			string addr = vecAddrs[i];
			tcpClt* pTcpClt = new tcpClt();

			string ip;
			int port;
			str::parseIpPort(addr, ip, port);

			//创建TDS Session
			tcpSessionClt* pTcpSess = &pTcpClt->m_session;
			std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
			pTcpSess->pALSession = p;
			p->type = TDS_SESSION_TYPE::tdsClient;
			p->name = "上级服务";
			p->m_bParentTds = true;
			p->remoteIP = ip;
			p->remotePort = port;

			pTcpClt->m_keepAliveTimeout = 10;
			pTcpClt->run(this, addr, childTdsIP);
			m_tcpClt_ParentTds[pTcpClt] = pTcpClt;
		}
	}


	

	int tcpPort = tds->conf->getInt("tcpPort", 670);
	if (tcpPort > 0) {
		LOG("[TCP数据服务] 端口%d", tcpPort);
		m_tcpSrv.run(this, tcpPort);
	}

	int udpPort = tds->conf->getInt("udpPort", 666);
	if (udpPort > 0) {
		LOG("[UDP数据服务] 端口%d", udpPort);
		m_udpSrv.run(this, udpPort);
	}

	return false;
}

void ReverseInterface::stop()
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
		reverseInterface.OnRecvData_TCP(tdb.pData, tdb.iLen, tdsSession);
		delete tdb.pData;
	}
}

void ReverseInterface::OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pTcpSess)
{
	m_mutexSessions.lock();
	std::shared_ptr<TDS_SESSION> tdsSession = m_reverseTdsSessions[pTcpSess];
	m_mutexSessions.unlock();
	OnRecvData_TCP((char*)pData, iLen, tdsSession);
}

void ReverseInterface::OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* pTcpSess)
{
	if (pTcpSess->pALSession) {
		std::shared_ptr<TDS_SESSION> p = pTcpSess->pALSession;
		OnRecvData_TCP((char*)pData, iLen, p);
	}
}

void ReverseInterface::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, std::string strIP, int port)
{
	string req;
	str::fromBuff(recvData, recvDataLen,req);
	std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	RPC_SESSION rpcSess;
	rpcSess.remoteAddr = strIP;
	rpcSess.remotePort = port;
	pSession->setRpcSession(&rpcSess);
	rpcSrv.handleRpcCallAsyn(req, pSession,false);
}





void ReverseInterface::OnRecvData_TCP(char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	timeopt::now(&tdsSession->lastRecvTime);
	stream2pkt& tlBuf = tdsSession->m_tlBuf;
	tlBuf.PushStream((unsigned char*)pData, iLen);
	while (tlBuf.PopPkt(IsValidPkt_textEnd_LFLF, false) || tlBuf.PopPkt(IsValidPkt_textEnd_CRLFCRLF, false))
	{
		string req = str::fromBuff((char*)tlBuf.pkt, tlBuf.iPktLen);

		if (req == "ping\n\n") { 
			string s = "pong\n\n";
			tdsSession->send((unsigned char*)s.data(), s.length(), false);
		}
		else if (req == "pong\n\n") {

		}
		else {
			//rpc命令统一启动线程处理，调用异步接口，因为很多rpc处理是阻塞等待的
			//不路由，不认证
			bool bEdgeDevMode = true;
			if (tdsSession->pTcpSession != nullptr) {
				bEdgeDevMode = false;
			}
			rpcSrv.handleRpcCallAsyn(req, tdsSession, false, bEdgeDevMode);
		}
	}
}



void ReverseInterface::sendToAllSessions(unsigned char* pData, size_t len, bool specialNotify)
{
	m_mutexSessions.lock();
	for (auto& i : m_reverseTdsSessions) {
		if(specialNotify)
			i.second->send(pData, len);
		else {
			if (i.second->pTcpSessionClt) {
				i.second->send(pData, len);
			}
			else {
				//tcp客户端模式直接访问的暂时不通知数据变化
			}
		}
	}
	m_mutexSessions.unlock();
}

void ReverseInterface::sendToAllSessions(string& s, bool specialNotify)
{
	sendToAllSessions((unsigned char*)s.c_str(), s.length(), specialNotify);
}
