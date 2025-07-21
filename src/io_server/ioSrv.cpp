#include "pch.h"
#include "ioSrv.h"
#include <thread>
#include "obj.h"
#include "mp.h"
#include "prj.h"
#include "logger.h"
#include "ioChan.h"
#include "rpcHandler.h"
#include "tSockSrv.h"

#include "ioDev_tdsp.h"
#include "ioDev_dcqk.h"
#include "base64.h"
#include "webSrv.h"
#include "statusServer.h"
#include "yyjson.h"
#include "proto_ws.h"


ioServer ioSrv;

void thread_do_ping_heartbeat(std::vector<ioDev*> listDev) {
	if (ioSrv.m_bPingThreadStart)
		return;

	ioSrv.m_bPingThreadStart = true;
	//LOG("[trace] - ping currrent loop count: %d, ips:", listDev.size());
	for (auto itm: listDev){
		if (itm->m_jDevAddr["ip"].is_string()) {

			string ip = itm->m_jDevAddr["ip"].get<string>();
			itm->doPingHeartbeat(ip);
		}
	}
	ioSrv.m_bPingThreadStart = false;
}


void thread_do_ping_single_dev(ioDev* pDev) {
	if (pDev->m_bPingThreadRunning)
		return;

	pDev->m_bPingThreadRunning = true;
	json jIP = pDev->m_jDevAddr["ip"];
	if (jIP.is_string()) {
		string ip = jIP.get<string>();
		pDev->doPingHeartbeat(ip);
	}
	pDev->m_bPingThreadRunning = false;
}


void IOThread()
{
	setThreadName("ioSrv io thread");
	ioSrv.m_bWorkingThreadRunning = true;
	int statisUpdateInterval = 10;

	//加载设备配置缓存
	ioSrv.lock_conf_unique();
	for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
	{
		ioDev* pIoDev = ioSrv.m_vecChildDev[i];
		pIoDev->loadConfBuff();
		pIoDev->loadInfoBuff();
		pIoDev->loadStatusBuff();
	}
	ioSrv.unlock_conf_unique();

	while (1)
	{
		timeopt::sleepMilli(5);

		if (!ioSrv.m_bRunning)
			break;

		ioSrv.lock_conf_unique();
		std::vector<ioDev*> listPing;
		for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
		{
			ioDev* pIoDev = ioSrv.m_vecChildDev[i];
			//空闲设备不轮询数据
			//所有的周期采集命令支持异步处理，doCycleTask不阻塞
			if (pIoDev->m_bRunning && !ioSrv.m_stopCycleAcq && pIoDev->m_bEnableAcq) //m_bEnableAcq对应设备管理中的启用轮询
			{
				pIoDev->DoCycleTask(); 
			}

			//执行ping在线监测
			if (pIoDev->m_bEnablePingOnlineCheck)
			{
				if (timeopt::CalcTimePassMilliSecond(pIoDev->m_stLastPingTime) > pIoDev->m_pingInterval)
				{
					pIoDev->m_stLastPingTime = timeopt::now();
					listPing.push_back(pIoDev);
				}
			}

			if (!ioSrv.m_bRunning)
				break;
		}

		// 开启设备ping检测,串行ping
		//if (listPing.size())
		//{
		//	thread t(thread_do_ping_heartbeat, listPing);
		//	t.detach();
		//}
		//并发ping
		for (int i = 0; i < listPing.size(); i++) {
			ioDev* pDev = listPing[i];
			if (pDev->m_bPingThreadRunning)
				continue;
			thread t(thread_do_ping_single_dev,pDev);
			t.detach();
		}

		ioSrv.unlock_conf_unique();
	}
	ioSrv.m_bWorkingThreadRunning = false;
	ioSrv.m_signalWorkThreadExit.notify();
}


void ioServer::onRecvPkt_iq60(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> ioSession)
{
	string pkt;
	str::fromBuff((char*)pData, iLen,pkt);

	try {
		json jpkt = json::parse(pkt);

		if (jpkt.is_array() && jpkt.size() >= 1)
		{
			//转发给对应设备
			//返回的字段0是设备id
			//返回：["C1201020756", ["AI9", 48.8, 1540697466, 0], ["BO1", 1, 1540697466, 0], "r"]
			string id = jpkt[0];

			//设备上线看做是 给tdsSession->m_IoDev 赋值的过程
			ioDev* pIoDev = ioSession->getBindDev(id);
			if (pIoDev == nullptr)
			{
				pIoDev = ioSrv.handleDevOnline(id, ioSession);
			}

			if (pIoDev)
			{
				if (pIoDev->m_devType == DEV_TYPE_iq60)
				{
					ioDev* p = pIoDev;
					if (p->m_bEnableIoLog)
						p->statisOnRecv((unsigned char*)pkt.c_str(), pkt.length(), p->getIOAddrStr());

					p->bindIOSession(ioSession);
					p->setOnline();
					p->onRecvPkt(jpkt);
				}
				else
				{
					LOG("[error]%s iq60 online,but this addr is configured as not an iq60 dev", id.c_str());
				}
			}
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		string log = "[error]iq60数据包解析错误,错误信息:" + errorType + "\r\n数据包:\r\n" + pkt;
		LOG(log);
		log = "[warn]iq60数据包如果是包不完整，可能是数据包长超出以太网帧长度被截断。使用命令 ip 1 n1000 设置最大包长1000字节，使数据包分包发送";
		LOG(log);
	}
}


ioServer::ioServer()
{
	m_stopCycleAcq = false;
	m_devType = "tds";
	m_totalPtCount = 0;
	tds->ioServer = this;
	m_tdspOnlineReq = false;
	m_ioSrvIP = "0.0.0.0";
	m_bPingThreadStart = false;
	m_bDisableIOHandle = false;
	m_bGb2312Tdsp = true;
}
ioServer::~ioServer()
{
	stop();
}

//void ioServer::statusChange_tcpClt(tcpSessionClt* pTcpSessClt, bool bIsConn)
//{
//	if (bIsConn)
//	{
//		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSessClt));
//		p->type = TDS_SESSION_TYPE::iodev;
//		m_mutexIoSessions.lock();
//		m_IoSessions[pTcpSessClt] = p;
//		m_mutexIoSessions.unlock();
//
//		//io服务主动连上TcpServer模式的设备
//		string ioAddr = str::format("%s:%d", pTcpSessClt->remoteIP.c_str(), pTcpSessClt->remotePort);
//		ioDev* pIoDev = ioSrv.getIODev(ioAddr);
//		if (pIoDev)
//		{
//			p->m_IoDev = pIoDev;
//			p->ioDevType = pIoDev->m_devType;
//			pIoDev->bindIOSession(p);
//			pIoDev->setOnline();
//			timeopt::now(&pIoDev->m_stLastActiveTime);
//			string s = str::format("[ioDev]设备上线,设备类型:%s,ioAddr:%s", pIoDev->m_devType.c_str(), pIoDev->getIOAddrStr().c_str());
//			logger.logInternal(s);
//			pIoDev->onEvent_online();
//		}
//	}
//	else
//	{
//		m_mutexIoSessions.lock();
//		std::shared_ptr<TDS_SESSION> p = m_IoSessions[pTcpSessClt];
//		m_IoSessions.erase(pTcpSessClt);
//		m_mutexIoSessions.unlock();
//		p->onTcpDisconnect();
//	}
//}

//该函数中不要进行阻塞操作，大数量轮询操作，阻塞当前线程会阻塞整个tcpServer的数据通信和连接建立
void ioServer::statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		p->type = TDS_SESSION_TYPE::iodev;
		p->localPort = pTcpSess->localPort;
		p->localIP = pTcpSess->localIP;
		tcpSrv* pts = (tcpSrv*)pTcpSess->pTcpServer;
		if (m_mapPort2DevType.find(pts->m_iServerPort) != m_mapPort2DevType.end()) {
			p->ioDevType = m_mapPort2DevType[pts->m_iServerPort];
		}

		//局域网环境 tcp客户端类型的设备连接，不支持中文地址，忽略端口号，允许设备使用任意端口
		if (p->remoteIP.find("192.") != string::npos) {
			ioDev* pIoDev = ioSrv.getIODev(p->remoteIP, false, true);// getIODev需要持续优化性能，阻塞当前线程会阻塞整个tcpServer的数据通信和连接建立
			if (pIoDev)
			{
				p->bindIoDev(pIoDev);
				pIoDev->setOnline();
				timeopt::now(&pIoDev->m_stLastActiveTime);

				pIoDev->bindIOSession(p);
				if (pIoDev->pIOSession == p)
				{
					string s = str::format("[ioSrv]TcpClient设备Tcp连接成功,ioAddr=%s,remoteAddr=%s:%d,localAddr=%s:%d", pIoDev->getIOAddrStr().c_str(), pTcpSess->remoteIP.c_str(), pTcpSess->remotePort, pTcpSess->localIP.c_str(), pTcpSess->localPort);
					logger.logInternal(s);
				}
				if (pIoDev->m_devType == DEV_TYPE_iq60) {
					pIoDev->sendStr("[]\n");
				}
			}
		}
		//公网环境，此时还找不到ioDev对象，需要依据后续的注册包来匹配设备对象
		else {
			string s = str::format("[ioSrv]TcpClient设备Tcp连接成功,remoteAddr=%s:%d", pTcpSess->remoteIP.c_str(), pTcpSess->remotePort);
			logger.logInternal(s);
		}

		m_mutexIoSessions.lock();
		m_IoSessions[pTcpSess] = p;
		m_mutexIoSessions.unlock();
	}
	else
	{
		m_mutexIoSessions.lock();
		std::shared_ptr<TDS_SESSION> p = m_IoSessions[pTcpSess];
		m_IoSessions.erase(pTcpSess);
		m_mutexIoSessions.unlock();
		//更新该session状态。等待其他零散指针引用销毁后自动删除
		p->onTcpDisconnect();
	}
}

void ioServer::OnRecvData_TCP(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> ioSession) {
	timeopt::now(&ioSession->lastRecvTime);

	//if it's the first time recv data from a connection. check transport layer protocol first
	//if applayer protocol is TDS RPC,transport layer protocol can be HTTP or WebSocket or RawTcp(no transport layer)
	//if applayer protocol is HTTP,transport layer is specified as none
	
	//首次从该链接收到数据时的处理。
	string strData;
	if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN) {
		str::fromBuff((char*)pData, iLen,strData);

		//parse transfer layer protocol
		if (strData.find("HTTP") != string::npos) {
			ioSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP;

			if (CWSPPkt::isHandShake(strData)) {
				ioSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET;
			}
		}
		else {
			ioSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_NONE;
		}

		//if websocket. deal the first handshake pkt 
		if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET) {
			//回复websocket握手
			CWSPPkt req;
			std::string handshakeString = req.GetHandshakeString(strData);

			send(ioSession->sock, handshakeString.c_str(), (int)handshakeString.size(), 0);
			return;
		}
	}

	if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET) {
		stream2pkt& tlBuf = ioSession->m_tlBuf;
		tlBuf.PushStream((unsigned char*)pData, iLen);

		while (tlBuf.PopPkt(IsValidPkt_WEBSOCKET)) {
			CWSPPkt wsPkt;
			wsPkt.unpack(tlBuf.pkt, tlBuf.iPktLen);

			if (wsPkt.isDataFrame()) {
				OnRecvAppLayerData((unsigned char*)wsPkt.payloadData, wsPkt.iPayloadLen, ioSession, wsPkt.fin_ ? true : false);
			}
		}

		if (tlBuf.iStreamLen > 1 * 1024 * 1024) {
			string str = str::format("%s", ioSession->remoteIP.c_str());
			LOG("[error]websocket parse error,can not get a pkt when length exceeded 10Mb,Addr=" + str);

			tlBuf.Init();
		}
	}
	//http处理 http仅依靠ip地址区分设备，可以与其他连接同时存在
	else if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP) {
		stream2pkt& tlBuf = ioSession->m_tlBuf;
		tlBuf.PushStream((unsigned char*)pData, iLen);

		while (tlBuf.PopPkt(IsValidPkt_HTTP, false)) {
			bool handled = false;
			string sHttp = str::fromBuff(( char*)tlBuf.pkt, tlBuf.iPktLen);

			vector<string> vec;
			str::split(vec, sHttp, "\r\n\r\n");
			
			if (vec.size() >= 2) {
				try {
					string& tdspPkt = vec[1];

					yyjson_doc* doc = yyjson_read(tdspPkt.c_str(), tdspPkt.size(), 0);
					if (!doc) {
						return;
					}

					yyjson_val* yyv_resp = yyjson_doc_get_root(doc);
					yyjson_val* yyv_ioAddr = yyjson_obj_get(yyv_resp, "ioAddr");

					if (yyv_ioAddr) {
						string ioAddr = yyjson_get_str(yyv_ioAddr);

						ioDev* p = getIODev(ioAddr);
						if (p && p->isTdsp()) {
							ioDev_tdsp* ptdsp = (ioDev_tdsp*)p;
							ptdsp->ioDev_tdsp::onRecvData((unsigned char*)tdspPkt.c_str(), tdspPkt.size());

							handled = true;
						}
					}

					yyjson_doc_free(doc);
				}
				catch (const std::exception&) {

				}
			}

			if (!handled) {
				LOG("[warn]收到没有处理的设备http请求包,%s", sHttp.c_str());
			}

			string httpResp =
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: application/json; charset=utf-8\r\n"
				"Content-Length: 0\r\n"
				"Connection: close\r\n"
				"\r\n";

			send(ioSession->sock, httpResp.c_str(), (int)httpResp.size(), 0);
		}
	}
	//tcp直连,没有传输层，表示全部都是应用层数据
	else if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_NONE) {
		OnRecvAppLayerData(pData, iLen, ioSession);
	}
}

void ioServer::onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pTcpSess)
{
	if (pTcpSess->bEnable == false) {
		return;
	}

	m_mutexIoSessions.lock();
	std::shared_ptr<TDS_SESSION> ioSession = m_IoSessions[pTcpSess];
	assert(ioSession != nullptr);
	m_mutexIoSessions.unlock();

#ifdef _WIN32
	statusSrv.statisRecv(ioSession->localPort, iLen);
#endif
	OnRecvData_TCP((unsigned char*)pData, iLen, ioSession);
}

//void ioServer::onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* pTcpSessClt)
//{
//	if (pTcpSessClt->bEnable == false) {
//		return;
//	}
//
//	m_mutexIoSessions.lock();
//	std::shared_ptr<TDS_SESSION> ioSession = m_IoSessions[pTcpSessClt];
//	assert(ioSession != nullptr);
//	m_mutexIoSessions.unlock();
//
//	OnRecvData_TCP((unsigned char*)pData, iLen, ioSession);
//}


struct IP_ADDR {
	string type; //tcp or udp
	string ip;
	int port;
};

 bool parseIPFromIoAddr(string s, IP_ADDR& ep) {
	vector<string> vec;
	str::split(vec, s, "/");
	string sIpEp = vec[0];
	if (sIpEp.find("UDP-") == 0) {
		ep.type = "udp";
		sIpEp = sIpEp.substr(4,sIpEp.length() - 4);
	}
	else {
		ep.type = "tcp";
	}

	vector<string> ipPort;
	str::split(ipPort, sIpEp, ":");
	if (ipPort.size() == 2) {
		ep.ip = ipPort[0];
		ep.port = atoi(ipPort[1].c_str());
		return true;
	}
	else {
		return false;
	}
}

 void ioServer::handleUDPTdspPkt(yyjson_val* yyv_pkt, yyjson_doc* doc,string strIP, int port, string ioSessionAddr) {
	 yyjson_val* yyv_method = yyjson_obj_get(yyv_pkt, "method");
	 yyjson_val* yyv_params = yyjson_obj_get(yyv_pkt, "params");
	 if (yyv_method == nullptr) {
		 LOG("[error]解析tdsp数据包失败,没有包含method字段");
		 return;
	 }
	 string method = yyjson_get_str(yyv_method);

	 if (method == "regAdaptor") {
		 if (m_strAdpIp != strIP) {
			 LOG("[适配器] 适配器上线,IP=%s,port=%d", strIP.c_str(), port);
		 }
		 m_strAdpIp = strIP;
		 m_iAdpPort = port;
		 return;
	 }

	 if (method == "regStandAloneIO") {
		 yyjson_val* yyv_type = yyjson_obj_get(yyv_params,"type");
		 if (yyv_type) {
			 string type = yyjson_get_str(yyv_type);
			 if (type != "") {
				 if (m_standAloneIO.find(type) == m_standAloneIO.end()) {
					 STANDALONE_IO info;
					 info.ip = strIP;
					 info.port = port;
					 m_standAloneIO[type] = info;
					 LOG("[独立IO服务] 独立IO服务上线,IP=%s,port=%d,类型=%s", strIP.c_str(), port,type.c_str());
				 }
				 else
				 {
					 if (m_standAloneIO[type].ip!= strIP || m_standAloneIO[type].port != port)
					 {
						 STANDALONE_IO info;
						 info.ip = strIP;
						 info.port = port;
						 m_standAloneIO[type] = info;
						 LOG("[独立IO服务] 独立IO服务上线,IP=%s,port=%d,类型=%s", strIP.c_str(), port, type.c_str());
					 }
				 }
			 }
		 }
		 return;
	 }

	 //适配器上送的地址格式 UDP-192.168.1.100:9009
	 yyjson_val* yyv_ioAddr = yyjson_obj_get(yyv_pkt, "ioAddr");
	 if (yyv_ioAddr == nullptr) {
		 LOG("[error]解析tdsp数据包失败,没有包含ioAddr字段");
		 return;
	 }
	 string ioAddr = yyjson_get_str(yyv_ioAddr);
	 IP_ADDR ipAddr;
	 string ioAddrWithoutPort = removePortFromIoAddr(ioAddr);

	 bool isLan = false; //tds服务和适配器下的设备是否在同一个局域网
	 if (ioAddr.find(":") != string::npos) { //如果适配器上送的地址是局域网ip地址，认为设备是在同一个局域网
		 isLan = true;
		 if (!parseIPFromIoAddr(ioAddr, ipAddr)) {
			 LOG("[warn]tdsp的ioAddr中使用了错误的IP地址格式:" + ioAddr);
			 return;
		 }
	 }

	 //是否是1级设备
	 bool firstLevel = false;
	 if (ioAddr.find("/") == string::npos)
		 firstLevel = true;


	 //获取地址时忽略端口号，设备在进行udp发送时可能使用随机端口。 
	 ioDev* pIoDev = ioSrv.getIODev(ioAddrWithoutPort, false, true);
	 //设备发现
	 if (!pIoDev)
	 {
		 if (firstLevel) {
			 json jAddr;
			 if (isLan) {//目前udp模式，所有的设备都是udpServer. 如果要引入udpClient需要再设计下
				 jAddr["type"] = DEV_ADDR_MODE::udpServer;
				 jAddr["ip"] = ipAddr.ip;
				 jAddr["port"] = ipAddr.port;
			 }
			 else {
				 jAddr["type"] = DEV_ADDR_MODE::deviceID;
				 jAddr["id"] = ioAddr;
			 }

			 pIoDev = ioSrv.onChildDevDiscovered(jAddr, ioSessionAddr, DEV_TYPE::DEV::tdsp_device);
		 }
	 }
	 //设备上线
	 else
	 {
		 if (pIoDev->m_bOnline == false)
		 {
			 pIoDev->setOnline();
			 pIoDev->triggerCycleAcq();
			 timeopt::now(&pIoDev->m_stLastActiveTime);
			 logger.logInternal("[ioDev]设备上线，ioAddr=" + pIoDev->getIOAddrStr());
		 }
	 }

	 if (pIoDev)
		 pIoDev->onRecvPkt(yyv_pkt, doc);
 }


void ioServer::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	IOLogRecv(recvData, recvDataLen, udpSession);

	//暂时udp服务只有tdsp协议，后续加入其他协议再重构
	try {
		string s;
		str::fromBuff((char*)recvData, recvDataLen,s);

		yyjson_doc* doc = yyjson_read((const char*)recvData, recvDataLen, 0);
		if (!doc) {
			LOG("[error]解析tdsp数据包失败,不是正确的json格式");
			return;
		}
		yyjson_val* yyv_pkt = yyjson_doc_get_root(doc);
		handleUDPTdspPkt(yyv_pkt, doc, udpSession.remoteIP, udpSession.remotePort,udpSession.getRemoteIOAddr());
		yyjson_doc_free(doc);
	}
	catch (exception& e) {
		LOG("[warn] handle udp recv data error,%s", e.what());
	}
	
	// udp设备,协议不一定是tdsp，可能是visca over udp等
	//else {
	//	string ioAddr = "UDP-" + strIP + ":" + str::fromInt(port);
	//	string ioAddrWithoutPort = "UDP-" + strIP;
	//	//获取地址时忽略端口号，设备在进行udp发送时可能使用随机端口。 
	//	ioDev* pIoDev = ioSrv.getIODev(ioAddrWithoutPort, false, true);
	//	if (pIoDev)
	//	{
	//		if (pIoDev->m_bOnline == false) {
	//			pIoDev->setOnline();
	//			pIoDev->triggerCycleAcq();
	//			timeopt::now(&pIoDev->m_stLastActiveTime);
	//			logger.logInternal("[ioDev]设备上线，ioAddr=" + pIoDev->getIOAddrStr());
	//		}
	//		pIoDev->onRecvData(recvData, recvDataLen); 
	//	}
	//	else {
	//		LOG("[warn]未知协议空闲设备上线，ioAddr=%s", ioAddr.c_str());  
	//	}
	//}
}

bool ioServer::loadConf()
{
	string conf;
	if (!fs::readFile(tds->conf->confPath + "/io.json", conf))
	{
		LOG("[keyinfo]未找到IO设备配置io.json,新建配置");
		return true;
	}

	try {
		json io = json::parse(conf.c_str());
		bool ret = loadConfAppend(io);
	}
	catch (std::exception& e)
	{
		string error = e.what();
		LOG("[error]解析IO设备配置文件io.json失败," + error);
		return false;
	}
	return true;
}



bool ioServer::loadConfMerge(json& j)
{
	for (auto it : j)
	{
		ioDev* p = getIODevByNodeID(it["nodeID"].get<string>());
		if (!p)
		{
			p = createIODev(it["type"].get<string>());
			p->loadConf(it);
			ioDev::addChild(p);
		}
		else
			p->loadConf(it);
	}
	return true;
}

bool ioServer::loadConfAppend(json& j)
{
	for (auto it : j)
	{
		ioDev* p = createIODev(it["type"].get<string>());
		if (p) {
			p->loadConf(it);
			ioDev::addChild(p);
		}
	}
	return true;
}

void ioServer::saveConf()
{
	json conf;
	json opt;
	opt["getStatus"] = false;
	opt["getDetail"] = false;
	toJson(conf,opt);
	string sConf = conf.dump(3);
	if (fs::writeFile(tds->conf->confPath + "/io.json",sConf))
	{
		
	}
}

void thread_handleDevOnlineAsyn(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession) {
	ioSrv.handleDevOnline(ioAddr, tdsSession);
}

void ioServer::handleDevOnlineAsyn(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession)
{
	thread t(thread_handleDevOnlineAsyn,ioAddr, tdsSession);
	t.detach();
}

//io设备在一个tdsSession上线
//该函数必须返回非空值
//只在tdsSession上收到首发包或者 注册包，第一次确定该session对应的设备地址的时候触发一次
ioDev* ioServer::handleDevOnline(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession) {
	tdsSession->m_ioAddr = ioAddr;
	ioDev* pIoDev = ioSrv.getIODev(ioAddr);

	//设备发现
	if (!pIoDev) {
		json jAddr;
		jAddr["id"] = ioAddr;
		jAddr["type"] = DEV_ADDR_MODE::deviceID;

		pIoDev = ioSrv.onChildDevDiscovered(jAddr,tdsSession->getRemoteAddr(), tdsSession->ioDevType, tdsSession->tdspSubType);
		if (pIoDev) {
			pIoDev->m_devSubType = tdsSession->tdspSubType;

			if (pIoDev->m_devSubType != "") {
				pIoDev->m_bEnableAcq = false;
			}
		}

	}
	//设备上线
	else {
		if (pIoDev->m_bOnline == false) {
			pIoDev->setOnline();
			pIoDev->triggerCycleAcq();

			timeopt::now(&pIoDev->m_stLastActiveTime);

			string log = str::format("[ioDev   ]设备上线，ioAddr=%s,ioSessionAddr=%s,type=%s,subType=%s", pIoDev->getIOAddrStr().c_str(), tdsSession->getRemoteAddr().c_str(), pIoDev->m_devType.c_str(), pIoDev->m_devSubType.c_str());
			logger.logInternal(log);
		}
	}

	if (pIoDev) {
		pIoDev->bindIOSession(tdsSession);
		pIoDev->onEvent_online();
	}
	else {
		T_ASSERT(false);
	}
		
	return pIoDev;
}

void ioServer::rpc_addDev(json& params,RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string type = params["type"].get<string>();
	string subType;
	if (params.contains("subType")) {
		subType = params["subType"];
	}

	params["nodeID"] = generateDevId();

	ioDev* parentDev = this;


	string parentID;
	if (params["parentID"] != nullptr) {
		parentID = params["parentID"].get<string>();
		parentDev = getIODevByNodeID(parentID);
		if (parentDev == NULL)
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devTypeError, "parent device not found, nodeID:" + parentID, "未找到父节点，父节点ID:" + parentID);
			return;
		}
	}
	else
		parentDev = this;


	ioDev* pd = createIODev(type);
	if (pd)
	{
		pd->loadConf(params);
		pd->m_devSubType = subType;
		parentDev->addChild(pd);
		saveConf();
		rpcResp.result = "\"ok\"";
		DEV_QUERIER devQuery;
		devQuery.getConf = true;
		devQuery.getChild = true;
		devQuery.getChan = true;
		devQuery.getStatus = true;
		pd->toJson(params, devQuery);
		rpcSrv.notify("devAdded", params);
		pd->run();
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devTypeError, "device type not supported, type:" + type, "不支持的设备类型:" + type);
	}
}

void ioServer::rpc_searchDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string type;
	if (params.contains("type")) {
		type = params["type"];
	}

	if (mapDevSearchFunc.find(type)!= mapDevSearchFunc.end()){
		fp_searchDev search = mapDevSearchFunc[type];
		search();
	}

	rpcResp.result = RPC_OK;
}


//该函数，对于实现热组态是非常关键的函数
//内存对象正在使用中，并且可能被多线程占用，但要支持删除修改
void ioServer::rpc_deleteDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string sNodeId = params["nodeID"].get<string>();
	bool bDeleted = deleteIODevByNodeID(sNodeId);

	if (bDeleted)
	{
		saveConf();
		rpcResp.result = RPC_OK;
		rpcSrv.notify("devDeleted", params);
	}
	else {
		rpcResp.error = "can not find device of specified NodeID:" + sNodeId;
	}
}


void ioServer::rpc_modifyDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	json devList = json::array(); 
	if (params.is_object())
		devList.push_back(params);
	else
		devList = params;

	bool modified = false;
	for (auto& devConf : devList) {
		string sNodeId = devConf["nodeID"].get<string>();
		bool bFinded = false;
		ioDev* p = getIODevByNodeID(sNodeId); 

		if (p)
		{
			string lastTagBind = p->m_strTagBind;
			string lastConnInfo = p->getConnInfo();

			//保留设备内的实时数据，如果修改设备和对象的绑定关系，可以让新绑定的对象立即有实时数据
			//修改设备不修改设备的子设备（一般是rs485网关的场景）
			devConf.erase("children");
			p->loadConf(devConf);
	
			devConf["ioAddr"] = p->getIOAddrStr(); //用于前端提示通知那台设备修改成功了
			rpcSrv.notify("devModified", devConf);  
			modified = true;

			//如果绑定对象改变，将缓存在io设备中的实时值同步到当前新绑定的对象，免去向设备请求一次
			string currentTagBind = p->m_strTagBind;
			if (currentTagBind != lastTagBind) {
				json calls;
				json call;

				//绑定关系更新
				//原来绑定的设备置为离线
				json tagBindings = json::array();
				json binding;
				binding["ioAddr"] = "";
				binding["tag"] = lastTagBind;
				tagBindings.push_back(binding);
				//新绑定设备置为当前设备状态
				binding["ioAddr"] = p->getIOAddrStr();
				binding["tag"] = currentTagBind;
				tagBindings.push_back(binding);
				call["method"] = "updateTagBinding";
				call["params"] = tagBindings;
				calls.push_back(call);

				//离在线状态更新
				if (p->m_bOnline) {
					call["method"] = "objOnline";
				}
				else {
					call["method"] = "objOffline";
				}
				call["params"] = {
					{"tag",currentTagBind}
				};
				calls.push_back(call);
				call["method"] = "objOffline";
				call["params"] = {
					{"tag",lastTagBind}
				};

				//值更新
				json valList;
				p->getChanVal(valList);
				call["method"] = "input";
				call["params"] = valList;

		
				tds->batchCallAsyn(calls);
			}

			string currentConnInfo = p->getConnInfo();
			if (currentConnInfo != lastConnInfo) {
				p->stop();
				p->run();
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "can not find device of specified NodeID:" + sNodeId);
			break;
		}
	}

	if(modified)
		saveConf();

	if(rpcResp.error == "")
		rpcResp.result = RPC_OK;
}

void ioServer::rpc_disposeDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string sNodeId = params["nodeID"].get<string>();
	string mode = params["mode"].get<string>();
	bool bFinded = false;
	ioDev* p = NULL;

	lock_conf_unique();
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		p = m_vecChildDev[i];
		if (p->m_confNodeId == sNodeId)
		{
			bFinded = true;
			p->m_dispositionMode = mode;
			//置为启用
			if (mode == DEV_DISPOSITION_MODE::managed) {
				p->run();
			}
			//置为备用
			if (mode == DEV_DISPOSITION_MODE::spare) {
				p->stop();
			}
			break;
		}
	}
	unlock_conf_unique();

	if (bFinded)
	{
		saveConf();
		rpcResp.result = "\"ok\"";
		rpcSrv.notify("devDisposed", params);
	}
	else {
		rpcResp.error = "can not find device of specified NodeID:" + sNodeId;
	}
}

void ioServer::rpc_startDevUpgrade(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	if (!params.contains("ioAddr")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: ioAddr");
	}
	else if (!params.contains("firmware")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: firmware");
	}
	else {
		string ioAddr = params["ioAddr"].get<string>();
		string firmware = params["firmware"].get<string>();
		int pktLen = 4096;
		if (params.contains("pktLen")) {
			pktLen = params["pktLen"].get<int>();
		}

		ioDev* pD = ioSrv.getIODev(ioAddr);
		if (pD == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "ioDev with specified ioAddr not found");
		}
		else if (pD->m_devType != DEV_TYPE::DEV::tdsp_device) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device type does not support firmware upgrade");
		}
		else if (pD->isCommBusy())
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device busy,another command is requesting");
		}
		else
		{
			ioDev_tdsp* pTdsp = (ioDev_tdsp*)pD;
			pTdsp->rpc_startUpgrade(firmware, pktLen, rpcResp);
		}
	}
}

void ioServer::rpc_stopDevUpgrade(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	if (!params.contains("ioAddr")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: ioAddr");
	}
	else {
		string ioAddr = params["ioAddr"].get<string>();
		ioDev* pD = ioSrv.getIODev(ioAddr);
		if (pD == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "ioDev with specified ioAddr not found");
		}
		else
		{
			ioDev_tdsp* pTdsp = (ioDev_tdsp*)pD;
			json jp = json::object();
			json rlt, err;
			pTdsp->call("stopUpgrade", jp, nullptr, rlt, err, true);

			if (rlt != nullptr) {
				rpcResp.result = rlt.dump();
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "stopUpgrade fail:" + err.dump());
			}
		}
	}
}

void ioServer::rpc_uploadDevFirmware(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	if (!params.contains("ioAddr")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: ioAddr");
	}
	else if (!params.contains("firmware")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: firmware");
	}
	else if (!params.contains("no")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: no");
	}
	else if (!params.contains("len")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: len");
	}
	else if (!params.contains("enc")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: enc");
	}
	else {
		string ioAddr = params["ioAddr"].get<string>();
		string firmware = params["firmware"].get<string>();
		ioDev* pD = ioSrv.getIODev(ioAddr);
		if (pD == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "ioDev with specified ioAddr not found");
		}
		else if (pD->m_devType != DEV_TYPE::DEV::tdsp_device) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device type does not support firmware upgrade");
		}
		else if (pD->isCommBusy())
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device busy,another command is requesting");
		}
		else
		{
			ioDev_tdsp* pTdsp = (ioDev_tdsp*)pD;
			if (!pTdsp->m_upgradeInfo.loadFirmwareFile(firmware)) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "can not load specified firmware");
			}
			else if (pTdsp->m_upgradeInfo.devType.find(pTdsp->m_strChanTemplate) == string::npos) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device type is " + m_strChanTemplate + ",but firmware device type is " + pTdsp->m_upgradeInfo.devType);
			}
			else {
				json jp;
				UPGRADE_INFO& ui = pTdsp->m_upgradeInfo;
				int pktNo = params["no"].get<int>();
				int pktLen = params["len"].get<int>();
				int pktNum = pTdsp->m_upgradeInfo.calcPktNum(pktLen);

				if (pktNo >= pktNum) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "pkt no over range,total pkt count=" + str::fromInt(pktNum));
				}
				else
				{
					jp["no"] = params["no"];
					jp["len"] = params["len"];
					jp["enc"] = params["enc"];
					char out[10000] = { 0 };
					base64_encode(ui.fileData + pktNo * pktLen, pktLen, out);
					string base64Data = out;
					jp["data"] = base64Data;
					jp["crc"] = common::N_CRC16(ui.fileData + pktNo * pktLen, pktLen);
					json rlt, err;
					pTdsp->call("uploadFirmware", jp, nullptr, rlt, err);
					
					if (rlt != nullptr) {
						json jRlt = params;
						jRlt["crc"] = jp["crc"];
						jRlt["pktNum"] = pktNum;
						jRlt["fileLen"] = ui.fileLen;
						jRlt["data"] = base64Data;
						rpcResp.result = jRlt.dump();
					}
					else {
						rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "uploadFirmware fail:" + err.dump());
					}

				}
			}
		}
	}
}

void ioServer::rpc_startDevUpgradeProc(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	if (!params.contains("ioAddr")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: ioAddr");
	}
	else if (!params.contains("firmware")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: firmware");
	}
	else {
		string ioAddr = params["ioAddr"].get<string>();
		string firmware = params["firmware"].get<string>();
		int pktLen = 4096;
		if (params.contains("pktLen")) {
			pktLen = params["pktLen"].get<int>();
		}

		ioDev* pD = ioSrv.getIODev(ioAddr);
		if (pD == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "ioDev with specified ioAddr not found");
		}
		else if (pD->m_devType != DEV_TYPE::DEV::tdsp_device) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device type does not support firmware upgrade");
		}
		else
		{
			ioDev_tdsp* pTdsp = (ioDev_tdsp*)pD;
			if (pTdsp->m_upgradeInfo.isUpgrading) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device upgrade process is running");
			}
			else if (!pTdsp->m_upgradeInfo.loadFirmwareFile(firmware, pktLen)) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "can not load specified firmware");
			}
			else if (pTdsp->m_upgradeInfo.devType.find(m_strChanTemplate) == string::npos) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device type is " + m_strChanTemplate + ",but firmware device type is " + pTdsp->m_upgradeInfo.devType);
			}
			else {
				pTdsp->startUpgradeProcess(firmware);
				json jp;
				UPGRADE_INFO& ui = pTdsp->m_upgradeInfo;
				jp["fileLen"] = ui.fileLen;
				jp["fileCrc"] = ui.fileCrc;
				jp["pktNum"] = ui.pktNum;
				jp["pktLen"] = ui.pktLen;
				jp["version"] = ui.version;
				jp["devType"] = ui.devType;
				rpcResp.result = jp.dump();
			}
		}
	}
}

void ioServer::rpc_stopDevUpgradeProc(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	if (!params.contains("ioAddr")) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: ioAddr");
	}
	else {
		string ioAddr = params["ioAddr"].get<string>();
		ioDev* pD = ioSrv.getIODev(ioAddr);
		if (pD == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "ioDev with specified ioAddr not found");
		}
		else
		{
			ioDev_tdsp* pTdsp = (ioDev_tdsp*)pD;
			json jp = json::object();
			json rlt, err;
			pTdsp->m_upgradeInfo.stopUpgradeSignal = true;
			rpcResp.result = "\"ok\"";
		}
	}
}

json CHAN_TEMPLATE::toJson() {
	json j;
	j["name"] = name;
	j["label"] = label;
	j["channels"] = channels;
	return j;
}

void ioServer::rpc_getChanTemplate(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	if (params.contains("name")) {
		string name = params["name"];
		if (ioSrv.m_mapChanTempalte.find(name) != ioSrv.m_mapChanTempalte.end()) {
			CHAN_TEMPLATE ct = ioSrv.m_mapChanTempalte[name];
			rpcResp.result = ct.channels.dump();
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_chanTemplateNotFound, "chan template not found");
		}
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param name is not specified");
	}
}

void ioServer::rpc_setChanTemplate(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	CHAN_TEMPLATE ct;
	ct.label = params["name"];
	str::hanZi2Pinyin(ct.label, ct.name);
	ct.channels = params["channels"];
	ioSrv.m_mapChanTempalte[ct.name] = ct;
	ioSrv.saveChanTemplate();
	rpcResp.result = "\"ok\"";
}

//中文ioAddr查找模式bChn
ioDev* ioServer::getIODev(string ioAddr,bool bChn,bool ignorePort)
{
	ioDev* p = nullptr;
	lock_conf_shared();
	p= ioDev::getIODev(ioAddr,bChn,ignorePort);
	unlock_conf_shared();
	return p;
}



void ioServer::updateTag2IOAddrBinding()
{
	lock_conf_shared();
	json tagBindings = json::array();
	for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
	{
		ioDev* p = ioSrv.m_vecChildDev[i];
		if (p->m_strTagBind != "")
		{
			json binding;
			binding["ioAddr"] = p->getIOAddrStr();
			binding["tag"] = p->m_strTagBind;
			binding["online"] = p->m_bOnline;
			tagBindings.push_back(binding);
		}
		for (int i = 0; i < p->m_vecChildDev.size(); i++)
		{
			ioDev* pp = p->m_vecChildDev[i];
			if (pp->m_strTagBind != "")
			{
				json binding;
				binding["ioAddr"] = pp->getIOAddrStr();
				binding["tag"] = pp->m_strTagBind;
				binding["online"] = p->m_bOnline;
				tagBindings.push_back(binding);
			}
		}
	}
	unlock_conf_shared();
	tds->callAsyn("updateTagBinding", tagBindings);
}

void ioServer::updateAllChanVal()
{
	json valList;
	getChanVal(valList);
	tds->callAsyn("input", valList);

	for (auto& i : m_vecChildDev) {
		if (i->m_devSubType == TDSP_SUB_TYPE::childTds) {
			i->syncDataToBindTag();
		}
	}
}

void ioServer::clear()
{
	lock_conf_unique();
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		delete m_vecChildDev[i];
	}
	m_vecChildDev.clear();
	unlock_conf_unique();
}

size_t ioServer::getBindedChanCount()
{
	lock_conf_shared();
	size_t count = 0;
	ioDev::recursiveGetBindedChanCount(this, count);
	unlock_conf_shared();
	return count;
}

size_t ioServer::getChanCount()
{
	lock_conf_shared();
	size_t count = 0;
	ioDev::recursiveGetChanCount(this, count);
	unlock_conf_shared();
	return count;
}

json ioServer::getDevTemplate(string devTplType)
{
	for (auto& i : m_mapChanTempalte) {
		string tplName = i.first;
		if (devTplType.find(tplName) != string::npos) {
			return i.second.toJson();
		}
	}
	return nullptr;
}

bool ioServer::loadChanTemplate()
{
	string p = tds->conf->confPath + "/template/device/conf.json";
	string tplListStr;
	if (fs::readFile(p, tplListStr)) {
		try {
			json jTplList = json::parse(tplListStr);
			for (auto& i : jTplList) {
				CHAN_TEMPLATE ct;
				ct.name = i["name"];
				ct.label = i["label"];
				string tplDataStr;
				string p1 = tds->conf->confPath + "/template/device/" + ct.name + ".json";
				if (fs::readFile(p1, tplDataStr)) {
					ct.channels = json::parse(tplDataStr);
					m_mapChanTempalte[ct.name] = ct;
				}
			}
		}
		catch (exception& e) {
			LOG("[error]加载/template/device/conf.json失败,error=%s", e.what());
		}
	}
	return false;
}

void ioServer::saveChanTemplate()
{
	string p = tds->conf->confPath + "/template/device/conf.json";
	json jConf = json::array();
	for (auto& i : m_mapChanTempalte) {
		json c;
		c["name"] = i.second.name;
		c["label"] = i.second.label;
		jConf.push_back(c);
	}
	string sConf = jConf.dump(2);
	fs::writeFile(p, sConf);

	string chanPath = tds->conf->confPath + "/template/device/";
	for (auto& i : m_mapChanTempalte) {
		string s = i.second.channels.dump(2);
		fs::writeFile(chanPath + "/" + i.second.name + ".json", s);
	}
}

bool ioServer::run()
{
	runAsCloud();
	return false;
}

bool ioServer::runAsCloud()
{
	m_bRunning = true;
	loadChanTemplate();

	m_tdspSingleTransaction = tds->conf->getInt("tdspSingleTransaction", 0);
	m_serialSendDelayAfterRecv = tds->conf->getInt("serialSendDelayAfterRecv", 0);
	m_bGb2312Tdsp = tds->conf->getInt("gb2312Tdsp", 0) ? true : false;
	m_ioSrvIPAsClient = tds->conf->getStr("ioSrvIP", "0.0.0.0");
	m_ioSrvIP = "0.0.0.0";

	if (m_bGb2312Tdsp) {
		LOG("[warn]TDSP设备使用GB2312编码");
	}


	//int leakDetectPort = tds->conf->getInt("leakDetectPort", 8085);
	int mbTcpPort = tds->conf->getInt("mbTcpPort", 502);
	int tdspPort = tds->conf->getInt("tdspPort", 665);
	int mbPort = tds->conf->getInt("mbPort", 664);
	int iq60Port = tds->conf->getInt("iq60Port", 663);
	//adaptor接入端口
	int adpPort = tds->conf->getInt("adpPort", 662);
	//
	int jepPort = tds->conf->getInt("jepPort", 6011);

	m_mapPort2DevType[tdspPort] = DEV_TYPE_tdsp;
	m_mapPort2DevType[mbPort] = DEV_TYPE_rs485_gateway;
	m_mapPort2DevType[iq60Port] = DEV_TYPE_iq60;
	//m_mapPort2DevType[leakDetectPort] = DEV_TYPE_leak_detect;
	m_mapPort2DevType[mbTcpPort] = DEV_TYPE_modbus_tcp_slave;
	m_mapPort2DevType[jepPort] = DEV_TYPE_jep;

	//启动服务端口
	if(tdspPort)LOG("[IO服务    ] 监听地址:" + m_ioSrvIP + ":" + str::fromInt(tdspPort) + "设备协议 TDSP");
	if(tdspPort)LOG("[IO服务    ] 监听地址:UDP-" + m_ioSrvIP + ":" + str::fromInt(tdspPort) + "设备协议 TDSP, Adaptor接入");
	if(mbTcpPort)LOG("[IO服务    ] 监听地址:" + m_ioSrvIP + ":" + str::fromInt(mbTcpPort) + " 设备协议 modbus TCP");
	if(iq60Port)LOG("[IO服务    ] 监听地址:" + m_ioSrvIP + ":" + str::fromInt(iq60Port) + " 设备协议 IQ60");
	if (jepPort)LOG("[IO服务    ] 监听地址:" + m_ioSrvIP + ":" + str::fromInt(jepPort) + "设备协议 JEP");
	//if(leakDetectPort)LOG("[IO服务    ] 监听地址:" + m_ioSrvIP + ":" + str::fromInt(leakDetectPort) + " 设备协议 漏点监测");


	//io服务 6011 JEP
	m_tcpSrv_jep = new tcpSrv();
	m_tcpSrv_jep->m_strName = "jep";
	m_tcpSrv_jep->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_jep->run(this, jepPort, m_ioSrvIP))
	{

	}
	else
	{
#ifndef _WIN32
		LOG("[error]linux need sudo to bind port under 1024,run this cmd to allow to bind without sudo\nsudo setcap 'cap_net_bind_service=+ep' .\\tds");
#endif

		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(jepPort));
	}


	//io服务 665 TDSP
	m_tcpSrv_tdsp = new tcpSrv();
	m_tcpSrv_tdsp->m_strName = "tdsp";
	m_tcpSrv_tdsp->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_tdsp->run(this, tds->conf->tdspPort, m_ioSrvIP))
	{
		
	}
	else
	{
#ifndef _WIN32
		LOG("[error]linux need sudo to bind port under 1024,run this cmd to allow to bind without sudo\nsudo setcap 'cap_net_bind_service=+ep' .\\tds");
#endif

		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(tds->conf->tdspPort));
	}


	//modbus 接收端口可以支持多个端口，用于udp设备回包不是回给请求的udp端口，而是回给固定的udp端口的场景。
	for (int i = 0; i < tds->conf->mbPort.size(); i++) {
		//io服务 664 Modbus over TCP
		int mbPort = tds->conf->mbPort[i];
		if (mbPort == 0)
			continue;

		LOG("[IO服务    ] 监听地址:" + m_ioSrvIP + ":" + str::fromInt(mbPort) + " 设备协议 modbus RTU over TCP");

		tcpSrv* ts = new tcpSrv();
		ts->m_strName = "modbus rtu";
		ts->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
		if (ts->run(this,mbPort, m_ioSrvIP))
		{

		}
		else
		{
			LOG("[error][IO服务    ] 启动失败 TCP端口:" + str::fromInt(mbPort));
		}

		//io服务 664 Modbus over UDP
		//针对mbRtu的ioSrv处理服务。用于固定端口接收场景
		udpServer* us = new udpServer();
		if (us->run(&this->ioHandler_mbRtu_udp, mbPort, m_ioSrvIP))
		{

		}
		else
		{
			LOG("[error][IO服务    ] 启动失败 UDP端口:" + str::fromInt(mbPort));
		}
	}


	//io服务 502 Modbus over TCP
	m_tcpSrv_mbTcp = new tcpSrv();
	m_tcpSrv_mbTcp->m_strName = "modbus tcp";
	m_tcpSrv_mbTcp->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_mbTcp->run(this, mbTcpPort, m_ioSrvIP))
	{

	}
	else
	{
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(mbTcpPort));
	}

	//io服务 663 IQ60
	m_tcpSrv_iq60 = new tcpSrv();
	m_tcpSrv_iq60->m_strName = "iq60";
	m_tcpSrv_iq60->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_iq60->run(this, tds->conf->iq60Port, m_ioSrvIP))
	{
		
	}
	else
	{
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(tds->conf->iq60Port));
	}

	//io服务 663 IQ60
	//m_tcpSrv_leakDetect = new tcpSrv();
	//m_tcpSrv_leakDetect->m_strName = "leakDetect";
	//m_tcpSrv_leakDetect->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	//if (m_tcpSrv_leakDetect->run(this, leakDetectPort, m_ioSrvIP))
	//{

	//}
	//else
	//{
	//	LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(leakDetectPort));
	//}

	//adaptor接入服务
	m_udpSrv_tdsp = new udpServer();
	if (m_udpSrv_tdsp->run(this,tdspPort, m_ioSrvIP)) {

	}
	else {
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(tdspPort));
	}


	//启动所有子设备
	for (auto i : m_vecChildDev) {
		i->run();
	}

	//启动IO工作线程
	std::thread io(IOThread);
	io.detach();


	return true;
}


void ioServer::stop()
{
	if (m_tcpSrv_iq60)
		m_tcpSrv_iq60->stop();
	for (int i = 0; i < m_udpSrv_rtu.size(); i++) {
		m_udpSrv_rtu[i]->stop();
	}
	for (int i = 0; i < m_tcpSrv_rtu.size(); i++) {
		m_tcpSrv_rtu[i]->stop();
	}
	if (m_tcpSrv_tdsp)
		m_tcpSrv_tdsp->stop();

	if (m_tcpSrv_jep)
		m_tcpSrv_jep->stop();

	LOG("stoping ioServer...");
	ioDev::stop();
	LOG("ioServer stopped");
}

void ioServer::queryDev(DEV_QUERIER devQuerier, DEV_STATIS& devStatis, vector<ioDev*>& filterRlt) {
	devStatis.rootTag = devQuerier.rootTag;
	for (auto& dev : m_vecChildDev)
	{
		//根据绑定位号进行过滤
		if (devQuerier.tag != "") {
			if (dev->m_strTagBind == "") {
				continue;
			}

			TAG_SELECTOR tagSel;
			tagSel.init(devQuerier.tag);
			if (!tagSel.match(dev->m_strTagBind)) {
				continue;
			}
		}

		//根据是否隶属ioServer进行过滤
		if (devQuerier.standAloneIO != "") {
			if (dev->m_standAloneIOType != devQuerier.standAloneIO) {
				continue;
			}
		}

		//根据指定的rootTag进行过滤；
		if (dev->m_strTagBind != "" && devQuerier.rootTag != "")
		{
			if (dev->m_strTagBind.find(devQuerier.rootTag) == string::npos)
			{
				continue;
			}
		}

		//根据设备类型进行过滤
		if (devQuerier.type.size() > 0) {
			bool typeMatched = false;
			for (int i = 0; i < devQuerier.type.size(); i++) {
				string s = devQuerier.type[i];
				if (s == dev->m_devType) {
					typeMatched = true;
				}
			}
			if (!typeMatched) {
				continue;
			}
		}

		//根据设备子类型进行过滤
		if (devQuerier.subType.size() > 0) {
			bool subtypeMatched = false;
			for (int i = 0; i < devQuerier.subType.size(); i++) {
				string s = devQuerier.subType[i];
				if (s == dev->m_devSubType) {
					subtypeMatched = true;
				}
			}
			if (!subtypeMatched) {
				continue;
			}
		}

		dev->recursiveGetChanCount(dev, devStatis.iChan);
		if (dev->m_bOnline) {
			devStatis.iOnline++;
		}
		else {
			devStatis.iOffline++;
		}

		if (dev->m_dispositionMode == DEV_DISPOSITION_MODE::managed) {
			devStatis.iInservice++;
		}
		else {
			devStatis.iSpare++;
		}
		devStatis.iTotal++;

		filterRlt.push_back(dev);
	}
}

bool ioServer::toJson(json& conf, json opt)
{
	lock_conf_shared();

	//只差找绑定位号属于某个根位号的设备。
	string rootTag = "";
	string interfaceType = "net"; //默认没有串口，指定接口类型为*所有才发串口
	bool paging = false;
	int pageNo = 0;
	int pageSize = 0;
	bool getStatis = false;
	string tagBind = "";  
	if (opt != nullptr)
	{
		if(opt.contains("rootTag"))
			rootTag = opt["rootTag"].get<string>();
		if (opt.contains("interface"))
			interfaceType = opt["interface"].get<string>();
		if (opt.contains("getStatis"))
			getStatis = opt["getStatis"].get<bool>();
		if (opt.contains("tag"))
			tagBind = opt["tag"].get<string>();

		//分页参数
		if (opt.contains("pageNo") && opt.contains("pageSize")) {
			paging = true;
			pageNo = opt["pageNo"].get<int>();
			pageNo -= 1;
			if (pageNo < 0)
				pageNo = 0;
			pageSize = opt["pageSize"].get<int>();
		}
	}

	DEV_QUERIER devQuery;
	devQuery.parseQueryOpt(opt);
	devQuery.rootTag = rootTag;
	devQuery.tag = tagBind;
	DEV_STATIS devStatis;
	vector<ioDev*> filterRlt;
	queryDev(devQuery,devStatis, filterRlt);
	

	if (paging && pageSize>0) {
		conf = json::object();
		size_t recCount = filterRlt.size();
		size_t pageCount = recCount / pageSize;
		if (recCount % pageSize > 0)
			pageCount++;

		size_t startIdx = pageNo * pageSize;
		size_t endIdx = pageNo * pageSize + pageSize;
		if (endIdx > recCount) {
			endIdx = recCount;
		}

		json jDevices = json::array();
		for (size_t i = startIdx; i < endIdx; i++) {
			json j;
			DEV_QUERIER query;
			query.parseQueryOpt(opt);
			filterRlt[i]->toJson(j, query);
			jDevices.push_back(j);
		}

		conf["pageNo"] = pageNo;
		conf["pageCount"] = pageCount;
		conf["pageSize"] = pageSize;
		conf["devList"] = jDevices;
		if (getStatis) {
			conf["statis"] = devStatis.toJson();
		}
	}
	else {
		conf = json::array();
		for (int i = 0; i < filterRlt.size(); i++) {
			json j;
			filterRlt[i]->toJson(j, devQuery);
			conf.push_back(j);
		}
	}
	
	unlock_conf_shared();
	return true;
}

bool ioServer::getStatus(json& conf, string opt)
{
	conf = json::array();//empty array
	for (auto& i : m_vecChildDev)
	{
		json j;
		i->getStatus(j, opt);
		string s = j.dump();
		conf.push_back(j);
	}
	return true;
}


string ioServer::getTag(string strDataChannelID)
{
	/*for (int i = 0; i < m_vecGlobalChannel.size(); i++)
	{
		string strID = str::trim(m_vecGlobalChannel[i].strID);
		str::replace(strID, "\\", "/");
		str::replace(strDataChannelID, "\\", "/");
		if (strID == strDataChannelID)
		{
			return m_vecGlobalChannel[i].strLink_MP;
		}
	}*/

	return "";
}


//childDevAddr 需要包含AddrType
ioDev* ioServer::onChildDevDiscovered(json childDevAddr,string ioSessionAddr, string type,string subType)
{
	ioDev* p = createIODev(type);
	if (p == nullptr) return nullptr;

	p->m_jDevAddr = childDevAddr;
	if (childDevAddr.is_string()) {
		p->m_devAddr = p->m_jDevAddr.get<string>();
		p->m_addrType = DEV_ADDR_MODE::deviceID;
	}
	else {
		p->m_addrType = childDevAddr["type"];
	}
		
	p->m_dispositionMode = DEV_DISPOSITION_MODE::spare;
	ioSrv.addChild(p);

	//通知设备上线
	p->setOnline();
	logger.logInternal(str::format("[ioDev]空闲设备上线，ioAddr=%s,ioSessionAddr=%s,设备类型=%s,子类型=%s",p->getIOAddrStr().c_str(), ioSessionAddr.c_str(), type.c_str(), subType.c_str()));

	//通知设备发现
	json j;

	DEV_QUERIER devQuery;
	devQuery.getConf = true;
	devQuery.getChan = true;
	devQuery.getChild = true;
	devQuery.getStatus = true;
	p->toJson(j, devQuery);
	rpcSrv.notify("devDiscovered", j);

	return p;
}

void ioServer::getAllSmartDev(vector<ioDev*>& aryDev)
{
	for (auto& it : m_vecChildDev)
	{
		if (it->m_strTagBind != "" && it->m_level != IO_DEV_LEVEL::channel)
		{
			aryDev.push_back(it);
		}
	}
}

void ioServer::getAllTDSPDev(vector<ioDev*>& aryDev)
{
	for (auto& it : m_vecChildDev)
	{
		if (it->m_devType == DEV_TYPE::DEV::tdsp_device)
		{
			aryDev.push_back(it);
		}
	}
}


//如果1个位号是某个TDS下级服务绑定位号的子位号，那么该位号来自于该tds下级服务
//根据某个位号找所属的TDS下级服务
ioDev* ioServer::getOwnerChildTdsDev(string tag) {
	vector<ioDev*> aryChildTds;
	for (auto& it : m_vecChildDev) {
		if (it->m_devSubType == TDSP_SUB_TYPE::childTds) {
			size_t pos = tag.find(it->m_strTagBind);

			//指定位号前半段是子服务位号，并且后面跟的是 . 符号
			//避免混淆 浙江.杭州.办公室   和 浙江.杭州.办公室Linux  两种位号。必须判断 . 符号
			if (pos==0 && tag.length() > it->m_strTagBind.length()) {
				if (tag.at(it->m_strTagBind.length()) == '.') {
					aryChildTds.push_back(it);
				}
			}
		}
	}

	if (aryChildTds.size() == 0) {
		return nullptr;
	}
	else {
		int maxIndex = 0;
		for (auto& it : aryChildTds) {
			if (it->m_strTagBind.length() > aryChildTds[maxIndex]->m_strTagBind.length()) {
				maxIndex = &it - &aryChildTds[0];
			}
		}
		return aryChildTds[maxIndex];
	}
}

bool ioServer::getOwnerChildTdsInfo(string tag, CHILD_TDS_INFO& info)
{
	ioDev_tdsp* childTds =(ioDev_tdsp*) getOwnerChildTdsDev(tag);
	if (childTds == nullptr)
		return false;

	info.tag = childTds->m_strTagBind;
	info.httpPort = childTds->m_childTdsHttpPort;
	info.httpsPort = childTds->m_childTdsHttpsPort;

	if (childTds->pIOSession != nullptr) {
		info.ip = childTds->pIOSession->getRemoteIP();
	}

	return true;
}


//onRecvData需要组包
bool ioServer::OnRecvAppLayerData(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt) {
	IOLogRecv(pData, iLen, tdsSession->getRemoteAddr(),tdsSession->getLocalAddr());

	if (m_bDisableIOHandle) {
		return true;
	}

	//协议检测
	if (tdsSession->ioDevType == "") {//应用层协议类型检测
		//应用层协议智能检测。根据收到的首包数据进行检测
		//傲华尔远程控制协议
		if ((pData[0] == '[' && pData[iLen - 1] == ']') ||
			(pData[0] == '[' && pData[iLen - 1] == '\n' && pData[iLen - 2] == ']')) {
			tdsSession->iALProto = IO_PROTO::IQ60;
			tdsSession->type = TDS_SESSION_TYPE::iodev;
		}
	}

	//应用层协议处理
	if (tdsSession->bridgedIoSessionClient != NULL) {
		if (tdsSession->ioDevType == DEV_TYPE_tdsp) {
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);

			while (pab->PopPkt(IsValidPkt_textEnd_LFLF)) {
				size_t iSend = tdsSession->bridgedIoSessionClient->send(pab->pkt, pab->iPktLen);

				string s = str::fromBuff((char*)pab->pkt, pab->iPktLen);
				LOG("[IO设备透传]dev->client " + s);
			}
		}
		//iq60的命令行数据包需要组包后再转发，否则可能导致中文utf8字符被分割后无法解析
		else if (tdsSession->ioDevType == DEV_TYPE_iq60) {
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);

			while (1) {
				//每一个数据包都先检测是否是iq60数据包。因为iq60也属于 textEnd_LF 类型
				if(pab->PopPkt(IsValidPkt_IQ60)) {
					onRecvPkt_iq60(pab->pkt, pab->iPktLen, tdsSession);
					continue;
				}

				if (pab->PopPkt(IsValidPkt_terminalPrompt) ||
					pab->PopPkt(IsValidPkt_textEnd_LF) ||
					pab->PopPkt(IsValidPkt_textEnd_LFLF) ||
					pab->PopPkt(IsValidPkt_textEnd_CRLF)) {

					tdsSession->bridgedIoSessionClient->send(pab->pkt, pab->iPktLen);

					string s = str::fromBuff((char*)pab->pkt, pab->iPktLen);
					LOG("[IO设备透传]dev->client " + s);
					continue;
				}

				break;
			}
		}
	}
	else if (tdsSession->ioDevType == DEV_TYPE_iq60) {
		bool regPkt = false;
		if (!tdsSession->m_bAppDataRecved) {//首包数据,按照tdsp注册包处理
			string s = str::fromBuff((char*)pData, iLen);
			LOG("[IQ60首发数据]remoteAddr=%s,data=%s",tdsSession->getRemoteAddr().c_str(), s.c_str());

			if (s.find("IQ60_") == 0) {
				s = s.substr(0, 16);
				LOG("IQ60首发数据," + s);

				regPkt = true;
				string strIoAddr = s.substr(5, s.length() - 5);
				ioSrv.handleDevOnline(strIoAddr, tdsSession);

				if (iLen > 16) {
					stream2pkt* pab = &tdsSession->m_alBuf;
					pab->PushStream(pData + 16, iLen - 16);

					while (pab->PopPkt(IsValidPkt_IQ60)) {
						onRecvPkt_iq60(pab->pkt, pab->iPktLen, tdsSession);
					}
				}
			}
		}

		if (!regPkt) {
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);

			while (pab->PopPkt(IsValidPkt_IQ60)) {
				onRecvPkt_iq60(pab->pkt, pab->iPktLen, tdsSession);
			}
		}
	}
	else if (tdsSession->ioDevType == DEV_TYPE_tdsp) {
		if (handleFirstRegPkt(pData, iLen, tdsSession)) {
			tdsSession->m_bSingleDevMode = true;
		}
		else {
			if (isPkt) {
				onRecvPkt_tdsp(pData, iLen, tdsSession);
			}
			else {
				stream2pkt* pab = &tdsSession->m_alBuf;
				pab->PushStream(pData, iLen);

				while (pab->PopPkt(IsValidPkt_TDSP)) {
					if (pab->abandonData != "") {
						string remoteAddr = tdsSession->getRemoteAddr();
						LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);

						tdsSession->abandonLen += pab->iAbandonLen;
					}

					tdsSession->iALProto = pab->m_protocolType;
					onRecvPkt_tdsp(pab->pkt, pab->iPktLen, tdsSession);
				}
			}
		}
	}
	else if (tdsSession->ioDevType == DEV_TYPE_modbus_tcp_slave) {
		if (handleFirstRegPkt(pData, iLen, tdsSession)) {

		}
		else {
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);

			while (pab->PopPkt(IsValidPkt_ModbusTcp, false)) {
				onRecvPkt_mbTcp(pab->pkt, pab->iPktLen, tdsSession);
			}
		}
	}
	else if (tdsSession->ioDevType == DEV_TYPE_rs485_gateway) {
		//检查是否是imei直接注册包,15位且都是数字，认为是imei
		if (handleFirstRegPkt(pData, iLen, tdsSession)) {//首包数据,按照tdsp注册包处理
		
		}
		else {
			unsigned char* pGwData = pData;
			size_t gwLen = iLen;
			
			//485直接透传到设备
			if (tdsSession->m_mapBindIoDev.size()>0) {
				for (auto& i : tdsSession->m_mapBindIoDev) {
					i.first->onRecvData(pGwData, gwLen);
				}
			}
		}
	}
	else if (tdsSession->ioDevType == DEV_TYPE_leak_detect) {
		stream2pkt* pab = &tdsSession->m_alBuf;
		pab->PushStream((unsigned char*)pData, iLen);

		while (pab->PopPkt(IsValidPkt_LeakDetect)) {
			if (pab->abandonData != "") {
				string remoteAddr = tdsSession->getRemoteAddr();
				LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);

				tdsSession->abandonLen += pab->iAbandonLen;
			}

			onRecvPkt_leakDetect((unsigned char*)pab->pkt, pab->iPktLen, tdsSession);
		}
	}
	else if (tdsSession->ioDevType == DEV_TYPE_jep) {
		string ioAddr = tdsSession->remoteIP;
		ioDev* pDev = getIODev(ioAddr, false, true);
		pDev->onRecvData(pData, iLen);
	}
	else {
		string ioAddr = tdsSession->remoteIP + ":" + str::fromInt(tdsSession->remotePort);
		ioDev* pDev = getIODev(ioAddr);
		pDev->onRecvData(pData, iLen);
	}

	tdsSession->m_bAppDataRecved = true; //放在上方处理的后面
	return true;
}

void ioServer::onRecvPkt_tdsp(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession) {
	try {
		if (tdsSession->tdspSubType == TDSP_SUB_TYPE::streamPusher) {
			tdsSession->m_csPuller.lock();

			size_t sizeLast = tdsSession->m_vecPuller.size();
			for (size_t i = 0; i < tdsSession->m_vecPuller.size(); i++) {
				std::shared_ptr<TDS_SESSION> p = tdsSession->m_vecPuller[i];

				int iSent = p->send(pData, iLen);
				if (iSent <= 0) {
					tdsSession->m_vecPuller.erase(tdsSession->m_vecPuller.begin() + i);
					i--;
				}
			}

			size_t sizeNow = tdsSession->m_vecPuller.size();

			tdsSession->m_csPuller.unlock();

			if (sizeLast > 0 && sizeNow == 0) {
				LOG("[warn]拉流端全部断开，关闭推流,tag=%s,断开客户端数:%d", tdsSession->streamId.c_str(), sizeLast);
				tdsSession->disconnect();
			}
		}
		else {
			string sResp;
			if (m_bGb2312Tdsp) {
				str::fromBuff((char*)pData, iLen,sResp);

				size_t ipos = 0; 
				string errChar;
				if (!charCodec::isValidGB2312(sResp, ipos, errChar)) {//硬件启用gb2312传输中文后。出bug的可能性很大。做一次有效性检测
					LOG("[error][TDSP]GB2312编码数据包包含非法字符，无法解析\nGB2312字符范围A1A1-FEFE,ascII范围0-7F\n错误字符位置:" + str::fromInt(ipos) + ",错误字符:" + errChar + "\n" + str::bytesToHexStr(pData, iLen));
					return;
				}

				sResp = charCodec::gb_to_utf8(sResp);
				pData = (unsigned char*)sResp.c_str();
				iLen = sResp.length();
			}
			
			yyjson_doc* doc = yyjson_read((const char*)pData, iLen, 0);
			if (!doc) {
				LOG("[error]解析tdsp数据包失败,不是正确的json格式");
				return;
			}

			yyjson_val* yyv_resp = yyjson_doc_get_root(doc);
			yyjson_val* yyv_method = yyjson_obj_get(yyv_resp, "method");
			if (yyv_method == nullptr) {
				LOG("[error]解析tdsp数据包失败,没有包含method字段");
				return;
			}

			string method = yyjson_get_str(yyv_method);

			//获得io地址
			yyjson_val* yyv_ioAddr = yyjson_obj_get(yyv_resp, "addr");
			if (yyv_ioAddr == nullptr) {
				yyv_ioAddr = yyjson_obj_get(yyv_resp, "ioAddr"); //ioAddr用于兼容老的格式
			}

			string strIoAddr;
			if (yyv_ioAddr) {
				strIoAddr = yyjson_get_str(yyv_ioAddr);
			}

			//设备或者子服务注册
			if (method == "devRegister") {
				if (strIoAddr == "") {
					LOG("[error]注册包devRegister中的addr或ioAddr为空，无效");
					return;
				}

				yyjson_val* yyv_params = yyjson_obj_get(yyv_resp, "params");
				yyjson_val* yyv_devType = yyjson_obj_get(yyv_params,"devType");

				string devType;
				if (yyv_devType) {
					devType = yyjson_get_str(yyv_devType);
				}

				if (devType != "") {
					tdsSession->tdspSubType = devType;
					if (devType == TDSP_SUB_TYPE::streamPusher) {
						string rootTag, tag;

						yyjson_val* yyv_rootTag = yyjson_obj_get(yyv_params, "rootTag");
						if (yyv_rootTag) {
							rootTag = yyjson_get_str(yyv_rootTag);
						}

						yyjson_val* yyv_tag = yyjson_obj_get(yyv_params, "tag");
						if (yyv_tag) {
							tag = yyjson_get_str(yyv_tag);
						}

						tag = TAG::addRoot(tag, rootTag);

						std::shared_ptr<TDS_SESSION> pusherSession = getStreamPusher(tag);

						//该位号推流已经存在
						if (pusherSession != nullptr) {
							LOG("[warn][数据流   ]位号:%s的推流已经存在", tag.c_str());
							tdsSession->disconnect();
						}
						//没有推流端，获取可能在等待的拉流端
						else {
							//以下复制相当于创建了一个流节点
							tdsSession->streamId = tag;

							MP* pmp = prj.GetMPByTag(tag, "zh");
							if (pmp) {
								vector< std::shared_ptr<TDS_SESSION>> puller;

								pmp->m_csPuller.lock();
								puller = pmp->m_vecPuller;
								pmp->m_vecPuller.clear();
								pmp->m_csPuller.unlock();

								size_t pullerCount = 0;

								tdsSession->m_csPuller.lock();
								tdsSession->m_vecPuller = puller;
								pullerCount = tdsSession->m_vecPuller.size();
								tdsSession->m_csPuller.unlock();

								LOG("[数据流   ]收到推流请求,开始接收。推流端地址:%s,位号:%s,拉流客户端数:%d", tdsSession->getRemoteAddr().c_str(), tag.c_str(), pullerCount);
							}
							else {
								LOG("[数据流   ]收到推流请求,没有找到位号。推流端地址:%s,位号:%s", tdsSession->getRemoteAddr().c_str(), tag.c_str());
							}
						}
						return;
					}
				}
			}

			//是否有设备在该session上上线，处理设备上线
			ioDev* pIoDev = tdsSession->getBindDev(strIoAddr);

			if (pIoDev == nullptr) {
				pIoDev = ioSrv.handleDevOnline(strIoAddr, tdsSession);
			}

			if (pIoDev) {
				pIoDev->onRecvPkt(yyv_resp, doc);
			}

			yyjson_doc_free(doc);
		}
	}
	catch (const std::exception& e) {
		string errorType = e.what();
		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("onRecvPkt_tdsp 处理异常" + errorType);
	}
}

void ioServer::onRecvPkt_mbRtu(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	for(auto& i: tdsSession->m_mapBindIoDev)
	{
		i.first->onRecvPkt(pData, iLen);
	}
}

void ioServer::onRecvPkt_dlt645_2007(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	for (auto& i : tdsSession->m_mapBindIoDev)
	{
		i.first->onRecvPkt(pData, iLen);
	}
}

void ioServer::onRecvPkt_mbTcp(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	for (auto& i : tdsSession->m_mapBindIoDev)
	{
		i.first->onRecvPkt(pData, iLen);
	}
}

void ioServer::onRecvPkt_leakDetect(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	//int id = pData[6];
	//string sId = str::fromInt(id);

	////设备上线看做是 给tdsSession->m_IoDev 赋值的过程
	//ioDev* pIoDev = tdsSession->m_IoDev;
	//if (tdsSession->m_IoDev == nullptr)
	//{
	//	pIoDev = ioSrv.handleDevOnline(sId, tdsSession);
	//}

	//if (pIoDev)
	//{
	//	ioDev* p = pIoDev;
	//	p->bindIOSession(tdsSession);
	//	p->setOnline();
	//	p->onRecvPkt(pData,iLen);
	//}
}

bool isCommonRegPkt(unsigned char* pData, size_t iLen) {
	for (size_t i = 0; i < iLen; i++) {
		unsigned char uc = pData[i];
		if (uc >= 32 && uc <= 126 && uc !='{' && uc !='}') //从数字0到字母z,不包含 { }符号
		{

		}
		else {
			return false;
		}
	}
	return true;
}


bool ioServer::handleFirstRegPkt(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	if (!tdsSession->m_bAppDataRecved)
	{
		//15位IMEI模式
		if (iLen == 15 && str::isDigits((char*)pData, iLen))
		{
			string imei = str::fromBuff((char*)pData, iLen);
			LOG("收到首发注册包,15位IMEI格式,IMEI=" + imei);
			ioSrv.handleDevOnline(imei, tdsSession);
			return true;
		}
		//imei前缀模式
		else if (iLen > 4 && (str::fromBuff((char*)pData, 4) == "imei" || str::fromBuff((char*)pData, 4) == "IMEI"))
		{
			string imei = str::fromBuff((char*)pData, iLen);
			LOG("收到首发注册包,IMEI前缀格式,IMEI=" + imei);
			ioSrv.handleDevOnline(imei, tdsSession);
			return true;
		}
		//RS485
		else if (iLen > 5 && (str::fromBuff((char*)pData, 5) == "RS485" || str::fromBuff((char*)pData, 5) == "rs485"))
		{
			string reg = str::fromBuff((char*)pData, iLen);
			LOG("收到首发注册包," + reg);
			ioSrv.handleDevOnline(reg, tdsSession);

			//如果开启了前缀功能，记录注册包作为前缀
			if (tds->conf->getInt("485GwPrefix", 0)) {
				tdsSession->regPkt.resize(iLen);
				memcpy(tdsSession->regPkt.data(), pData, iLen);
				tdsSession->m_alBuf.m_prefix = tdsSession->regPkt;
			}
			return true;
		}
		//通用注册包，全部都是ASII字符，并且没有 { } 符号，不是json
		else if (isCommonRegPkt(pData, iLen)) {
			string reg = str::fromBuff((char*)pData, iLen);
			LOG("收到首发注册包," + reg);
			ioSrv.handleDevOnline(reg, tdsSession);

			return true;
		}
	}

	return false;
}




void ioServer::rpc_getSessionStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION session) {
	json typeFilter = nullptr;
	json nameFilter = nullptr;
	if (params != nullptr) typeFilter = params["type"];
	if (params != nullptr) nameFilter = params["name"];
	lock_guard<mutex> g(m_mutexIoSessions);
	json jList = json::array();
	for (auto i : m_IoSessions)
	{
		std::shared_ptr<TDS_SESSION> p = i.second;
		if (nameFilter != nullptr && nameFilter.is_string())
		{
			if (nameFilter.get<string>() != p->name)
				continue;
		}
		ioDev* dev = nullptr;
		if (p->m_mapBindIoDev.size() != 0) {
			dev = p->m_mapBindIoDev.begin()->first;
		}


		json jSession;
		jSession["type"] = p->type;
		jSession["ip"] = p->remoteIP;
		jSession["port"] = p->remotePort;
		jSession["enable"] = p->m_bEnableIO;
		jSession["name"] = p->name;
		jSession["transLayer"] = p->iTLProto;
		jSession["createTime"] = timeopt::st2str(p->stCreateTime);
		if (p->lastMethodCalled != "")
		{
			jSession["lastMethodCalled"] = p->lastMethodCalled;
		}
		jSession["lastRecvTime"] = timeopt::st2str(p->lastRecvTime);
		jSession["lastSendTime"] = timeopt::st2str(p->lastSendTime);
		jSession["sendBytes"] = p->getSendedBytes();
		jSession["recvBytes"] = p->getRecvedBytes();
		jSession["buffLen"] = p->m_alBuf.iStreamLen;
		jSession["abandonLen"] = p->abandonLen;
		jSession["lastMethod"] = p->lastMethodCalled;
		if(dev){
			jSession["transactionSuccessCount"] = dev->m_transactionSuccessCount;
			jSession["transactionFailCount"] = dev->m_transactionFailCount;
		}
		jSession["avgTransactionTime"] = 0;

		//string ioAddrInSession = "";
		//for (int i = 0; i < p->m_vecIoDev.size(); i++)
		//{
		//	if (i > 0)
		//		ioAddrInSession += ";";
		//	ioAddrInSession += p->m_vecIoDev[i];
		//	ioAddrInSession += ",";
		//	ioAddrInSession += p->m_vecIoBindTag[i];
		//}
		if (dev) {
			jSession["ioAddr"] = dev->getIOAddrStr();
			jSession["bindTag"] = dev->m_strTagBind;
		}
	

		string ioAddrInSessionHist = "";
		jSession["ioAddrHist"] = ioAddrInSessionHist;

		if (p->type == "video" && p->pTcpSession)
		{
			jSession["sendBytes"] = p->pTcpSession->iSendSucCount;
			jSession["sendFailBytes"] = p->pTcpSession->iSendFailCount;
		}
		jList.push_back(jSession);
	}

	//串口会话
	for (auto i : m_vecChildDev)
	{
		ioDev* p = i;
		if (p->m_devType != DEV_TYPE_local_serial)
			continue;

		json jSession;
		jSession["type"] = "";
		jSession["ip"] = p->getIOAddrStr();
		jSession["port"] = 0;
		jSession["name"] = "";
		jSession["transLayer"] = "";
		jSession["createTime"] = "";
		jSession["lastRecvTime"] = timeopt::st2str(p->lastRecvTime);
		jSession["lastSendTime"] = timeopt::st2str(p->lastSendTime);
		jSession["sendBytes"] = p->m_sendBytes;
		jSession["recvBytes"] = p->m_recvBytes;
		jSession["buffLen"] = p->m_pab.iStreamLen;
		jSession["abandonLen"] = p->m_pab.iAbandonLen;
		jSession["lastMethod"] = "";
		jSession["transactionSuccessCount"] = p->m_transactionSuccessCount;
		jSession["transactionFailCount"] = p->m_transactionFailCount;
		jSession["ioAddr"] = p->getIOAddrStr();
		jSession["ioAddrHist"] = "";
		jSession["avgTransactionTime"] =p->m_avgTransactionTime;
		jList.push_back(jSession);
	}

	rpcResp.result = jList.dump(2);
}

//此处加锁，连接断开现成可能会并发操作此列表
shared_ptr<TDS_SESSION> ioServer::getTDSSession(tcpSession* pTcpSess)
{
	lock_guard<mutex> g(m_mutexIoSessions);
	/*for (int i = 0; i < m_IoSessions.size(); i++)
	{
		shared_ptr<TDS_SESSION> p = m_IoSessions.at(i);
		if (p->pTcpSession == pTcpSess)
		{
			return p;
		}
	}*/
	return nullptr;
}


shared_ptr<TDS_SESSION> ioServer::getTDSSession(string remoteIP, int remotePort)
{
	lock_guard<mutex> g(m_mutexIoSessions);
	for (auto& i: m_IoSessions)
	{
		shared_ptr<TDS_SESSION> p = i.second;
		std::unique_lock<recursive_mutex> lock(p->m_mutexTcpLink);
		if (p->isConnected())
		{
			if (p->pTcpSessionClt)
			{
				//客户端模式remoteAddr 只有1个，但本地有可以有多个连接，因此使用本地端口+ip作为id
				if (p->pTcpSessionClt->remoteIP== remoteIP && p->pTcpSessionClt->remotePort == remotePort)
				{
					return p;
				}
			}
			else
			{
				if (p->pTcpSession->remoteIP == remoteIP && p->pTcpSession->remotePort == remotePort)
				{
					return p;
				}
			}
		}
	}
	return nullptr;
}

shared_ptr<TDS_SESSION> ioServer::getTDSSession(string remoteAddr)
{
	size_t pos = remoteAddr.find(":");
	if (pos < 0)
		return nullptr;
	string ip = remoteAddr.substr(0, pos);
	string sPort = remoteAddr.substr(pos + 1, remoteAddr.length() - pos - 1);
	int iPort = atoi(sPort.c_str());
	return getTDSSession(ip, iPort);
}



shared_ptr<TDS_SESSION> ioServer::getTDSSession(tcpSessionClt* pTcpSess)
{
	lock_guard<mutex> g(m_mutexIoSessions);
	/*for (int i = 0; i < m_IoSessions.size(); i++)
	{
		shared_ptr<TDS_SESSION> p = m_IoSessions.at(i);
		if (p->pTcpSessionClt == pTcpSess->tcpClt)
		{
			return p;
		}
	}*/
	return nullptr;
}


std::shared_ptr<TDS_SESSION> ioServer::getStreamPusher(string tag)
{
	for (auto& i: m_IoSessions) 
	{
		std::shared_ptr<TDS_SESSION> p =  i.second;
		if (p->tdspSubType == TDSP_SUB_TYPE::streamPusher) {
			if (p->streamId == tag) {
				return p;
			}
		}
	}
	return nullptr;
}

vector<std::shared_ptr<TDS_SESSION>> ioServer::getStreamPushers(string tag)
{
	vector<std::shared_ptr<TDS_SESSION>> vec;
	for (auto& i : m_IoSessions)
	{
		std::shared_ptr<TDS_SESSION> p = i.second;
		if (p->tdspSubType == TDSP_SUB_TYPE::streamPusher) {
			if (p->streamId == tag) {
				vec.push_back(p);
			}
		}
	}
	return vec;
}

void ioHandler_mbRtu::onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pCltInfo)
{
}

void ioHandler_mbRtu::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
}

void ioHandler_mbRtu::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	IOLogRecv(recvData, recvDataLen, udpSession);

	ioDev* p = ioSrv.getIODev("UDP-" + udpSession.remoteIP, false, true);
	if (p) {
		p->onRecvData(recvData, recvDataLen);
	}
}
