#include "pch.h"
#include "ioSrv.h"
#include <thread>
#include "mo.h"
#include "mp.h"
#include "prj.h"
#include "logger.h"
#include "ioChan.h"
#include "ioDev_genicam.h"
#include "rpcHandler.h"
#include "ds.h"
#include "httplib.h"


ioServer ioSrv;

void IOThread()
{
	setThreadName("ioSrv io thread");
	ioSrv.m_bWorkingThreadRunning = true;
	int statisUpdateInterval = 10;

	//加载设备配置缓存
	ioSrv.m_csThis.lock();
	for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
	{
		ioDev* pIoDev = ioSrv.m_vecChildDev[i];
		pIoDev->loadConfBuff();
		pIoDev->loadInfoBuff();
		pIoDev->loadStatusBuff();
	}
	ioSrv.m_csThis.unlock();

	while (1)
	{
		Sleep(5);

		if (!ioSrv.m_bRunning)
			break;

		ioSrv.m_csThis.lock();
		for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
		{
			ioDev* pIoDev = ioSrv.m_vecChildDev[i];
			//空闲设备不轮询数据
			//所有的周期采集命令支持异步处理，doCycleTask不阻塞
			if (pIoDev->m_bRunning && !ioSrv.m_stopCycleAcq)
			{
				pIoDev->DoCycleTask();
			}
				

			if (!ioSrv.m_bRunning)
				break;
		}
		ioSrv.m_csThis.unlock();
	}
	ioSrv.m_bWorkingThreadRunning = false;
	ioSrv.m_signalWorkThreadExit.notify();
}


void onRecvIQ60Pkt(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	string pkt = str::fromBuff((char*)pData, iLen);

	try {
		json jpkt = json::parse(pkt);

		if (jpkt.is_array() && jpkt.size() >= 1)
		{
			//转发给对应设备
			string id = jpkt[0];

			//设备上线看做是 给tdsSession->m_IoDev 赋值的过程
			ioDev* pIoDev = tdsSession->m_IoDev;
			if (tdsSession->m_IoDev == nullptr)
			{
				pIoDev = ioSrv.handleDevOnline(id, tdsSession);
			}

			if (pIoDev)
			{
				if (pIoDev->m_devType == IO_DEV_TYPE::DEV::iq60_gateway)
				{
					ioDev* p = pIoDev;
					if (p->m_bEnableIoLog)
						p->statisOnRecv((char*)pkt.c_str(), pkt.length(), p->getIOAddrStr());

					p->bindIOSession(tdsSession);
					p->setOnline();
					json j;
					p->toJson(j);
					if (!tdsSession->getIODev(p->getIOAddrStr()))
					{
						tdsSession->m_vecIoDev.push_back(p->getIOAddrStr());
					}
					p->onRecvPkt(jpkt);
				}
				else
				{
					LOG("[error]%s iq60 online,but this addr is configured as not an iq60 dev", id);
				}
			}
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		string log = "pkt from iq60,json parse error. " + errorType;
		LOG(log);
	}
}


ioServer::ioServer()
{
	m_stopCycleAcq = false;
	m_devType = IO_DEV_TYPE::SERVER::tds;
}
ioServer::~ioServer()
{
}

void ioServer::statusChange_tcpClt(tcpSessionClt* pTcpSessClt, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSessClt));
		m_mutexIoSessions.lock();
		m_IoSessions[pTcpSessClt] = p;
		m_mutexIoSessions.unlock();

		//io服务主动连上TcpServer模式的设备
		string ioAddr = str::format("%s:%d", pTcpSessClt->srvIP.c_str(), pTcpSessClt->srvPort);
		ioDev* pIoDev = ioSrv.getIODev(ioAddr);
		if (pIoDev)
		{
			p->m_IoDev = pIoDev;
			if (pIoDev->m_devType == IO_DEV_TYPE::DEV::iq60_gateway)
			{
				p->iALProto = IO_PROTO::IQ60;
			}
			else if (pIoDev->m_devType == IO_DEV_TYPE::DEV::tdsp_device)
			{
				p->iALProto = IO_PROTO::TDSRPC;
			}
			else if (pIoDev->m_devType == IO_DEV_TYPE::GW::rs485_gateway)
			{
				p->iALProto = IO_PROTO::MODBUS_RTU;
			}
			pIoDev->setOnline();
			GetLocalTime(&pIoDev->m_stLastActiveTime);
			string s = str::format("[ioDev]设备上线,设备类型:%s,ioAddr:%s", pIoDev->m_devType.c_str(), pIoDev->getIOAddrStr().c_str());
			logger.logInternal(s);
			pIoDev->bindIOSession(p);
		}
	}
	else
	{
		m_mutexIoSessions.lock();
		std::shared_ptr<TDS_SESSION> p = m_IoSessions[pTcpSessClt];
		m_IoSessions.erase(pTcpSessClt);
		m_mutexIoSessions.unlock();
		p->onTcpDisconnect();
	}
}

void ioServer::statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));

		tcpSrv* pts = (tcpSrv*)pTcpSess->pTcpServer;
		if (m_mapPort2DevType.find(pts->m_iServerPort) != m_mapPort2DevType.end()) {
			p->ioDevType = m_mapPort2DevType[pts->m_iServerPort];
		}

		ioDev* pIoDev = ioSrv.getIODev(p->ip);
		if (pIoDev)
		{
			p->m_IoDev = pIoDev;
			pIoDev->setOnline();
			GetLocalTime(&pIoDev->m_stLastActiveTime);
			logger.logInternal("[ioDev]设备上线,ioAddr=" + pIoDev->getIOAddrStr());
			pIoDev->bindIOSession(p);
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

void ioServer::OnRecvData_TCP(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> ioSession)
{
	GetLocalTime(&ioSession->lastRecvTime);

	//if it's the first time recv data from a connection. check transport layer protocol first
	//if applayer protocol is TDS RPC,transport layer protocol can be HTTP or WebSocket or RawTcp(no transport layer)
	//if applayer protocol is HTTP,transport layer is specified as none
	//首次从该链接收到数据时的处理。
	if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN)
	{
		string strData = str::fromBuff((char*)pData, iLen);
		//parse transfer layer protocol
		if (strData.find("HTTP") != string::npos)
		{
			ioSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP;
			if (CWSPPkt::isHandShake(strData))
			{
				ioSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET;
			}
		}
		else
		{
			ioSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_NONE;
		}

		//if websocket. deal the first handshake pkt 
		if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
		{
			//回复websocket握手
			CWSPPkt req;
			std::string handshakeString = req.GetHandshakeString(strData);
			send(ioSession->sock, handshakeString.c_str(), handshakeString.size(), 0);
			return;
		}
	}


	if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
	{
		stream2pkt& tlBuf = ioSession->m_tlBuf;
		tlBuf.PushStream((unsigned char*)pData, iLen);
		while (tlBuf.PopPkt(APP_LAYER_PROTO::PROTOCOL_WEBSOCKET))
		{
			CWSPPkt wsPkt;
			wsPkt.unpack(tlBuf.pkt, tlBuf.iPktLen);
			if (wsPkt.isDataFrame())
				OnRecvAppLayerData((unsigned char*)wsPkt.payloadData, wsPkt.iPayloadLen,ioSession,wsPkt.fin_?true:false);
		}

		if (tlBuf.iStreamLen > 1 * 1024 * 1024)
		{
			string str = str::format("%s", ioSession->ip.c_str());
			LOG("[error]websocket parse error,can not get a pkt when length exceeded 10Mb,Addr=" + str);
			tlBuf.Init();
		}
	}
	//http处理   1.网页请求  2.tdsRpc over http   
	else if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP)
	{
		stream2pkt& tlBuf = ioSession->m_tlBuf;
		tlBuf.PushStream((unsigned char*)pData, iLen);
		while (tlBuf.PopPkt(APP_LAYER_PROTO::HTTP))
		{
			string sHttp = str::fromBuff(( char*)tlBuf.pkt, tlBuf.iPktLen);
			httplib::Request httpReq;
			httplib::Server srv;
			srv.parse_request_line(sHttp.c_str(), httpReq);
			OnRecvAppLayerData((unsigned char*)httpReq.body.c_str(),httpReq.body.length(), ioSession,true);
		}
	}
	//tcp直连,没有传输层，表示全部都是应用层数据
	else if (ioSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_NONE)
	{
		OnRecvAppLayerData(pData, iLen, ioSession);
	}
}

void ioServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	m_mutexIoSessions.lock();
	std::shared_ptr<TDS_SESSION> ioSession = m_IoSessions[pTcpSess];
	assert(ioSession != nullptr);
	m_mutexIoSessions.unlock();
	OnRecvData_TCP((unsigned char*)pData, iLen, ioSession);
}

void ioServer::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* pTcpSessClt)
{
	m_mutexIoSessions.lock();
	std::shared_ptr<TDS_SESSION> ioSession = m_IoSessions[pTcpSessClt];
	assert(ioSession != nullptr);
	m_mutexIoSessions.unlock();
	OnRecvData_TCP((unsigned char*)pData, iLen, ioSession);
}

bool ioServer::loadConf()
{
	string conf;
	if (!fs::readFile(tds->conf->confPath + "/io.json", conf))
	{
		LOG("[warn]未找到IO设备配置io.json,新建配置");
		return true;
	}

	try {
		json io = json::parse(conf.c_str());
		return loadConfAppend(io);
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
		p->loadConf(it);
		ioDev::addChild(p);
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
ioDev* ioServer::handleDevOnline(string ioAddr, std::shared_ptr<TDS_SESSION> tdsSession)
{
	tdsSession->m_ioAddr = ioAddr;
	ioDev* pIoDev = ioSrv.getIODev(ioAddr);
	//设备发现
	if (!pIoDev)
	{
		json jAddr;
		jAddr["id"] = ioAddr;
		pIoDev = ioSrv.onChildDevDiscovered(jAddr, tdsSession->ioDevType);
	}
	//设备上线
	else
	{
		if (pIoDev->m_bOnline == false)
		{
			pIoDev->setOnline();
			pIoDev->triggerCycleAcq();
			GetLocalTime(&pIoDev->m_stLastActiveTime);
			logger.logInternal("[ioDev]设备上线，ioAddr=" + pIoDev->getIOAddrStr());
		}
	}

	if(pIoDev)
		pIoDev->bindIOSession(tdsSession);
	return pIoDev;
}

void ioServer::rpc_addDev(json& params,RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string type = params["type"].get<string>();
	if (!params.contains("nodeID"))
	{
		params["nodeID"] = common::guid();
	}

	ioDev* parentDev = this;
	string parentID;
	if (params["parentID"] != nullptr) {
		parentID = params["parentID"].get<string>();
		parentDev = getIODevByNodeID(parentID);
	}
	else
		parentDev = this;

	if (parentDev == NULL)
	{
		rpcResp.error =  makeRPCError(RPC_ERROR_CODE::IO_devTypeError, "parent device not found, nodeID:" + parentID,"未找到父节点，父节点ID:" + parentID);
	}

	ioDev* pd = createIODev(type);
	if (pd)
	{
		pd->loadConf(params);
		parentDev->addChild(pd);
		saveConf();
		rpcResp.result = "\"ok\"";
		json opt;
		opt["getConf"] = true;
		opt["getChild"] = true;
		opt["getChan"] = true;
		opt["getStatus"] = true;
		pd->toJson(params,opt);
		rpcSrv.notify("devAdded", params);
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devTypeError, "device type not supported, type:" + type, "不支持的设备类型:" + type);
	}
}

void ioServer::rpc_deleteDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string sNodeId = params["nodeID"].get<string>();
	bool bDeleted = deleteIODevByNodeID(sNodeId);

	if (bDeleted)
	{
		saveConf();
		rpcResp.result = "\"ok\"";
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
			p->loadConf(devConf);
			p->toJson(devConf);
			rpcSrv.notify("devModified", devConf);
			rpcResp.result = devConf.dump(2);
			modified = true;
		}
		else {
			rpcResp.error = "can not find device of specified NodeID:" + sNodeId;
			break;
		}
	}

	if(modified)
		saveConf();
}

void ioServer::rpc_disposeDev(json& params, RPC_RESP& rpcResp, RPC_SESSION sesion)
{
	string sNodeId = params["nodeID"].get<string>();
	string mode = params["mode"].get<string>();
	bool bFinded = false;
	ioDev* p = NULL;

	m_csThis.lock();
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		p = m_vecChildDev[i];
		if (p->m_confNodeId == sNodeId)
		{
			bFinded = true;
			p->m_dispositionMode = mode;
			break;
		}
	}
	m_csThis.unlock();

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

ioDev* ioServer::getIODev(string ioAddr,bool bChn)
{
	std::shared_lock<shared_mutex> lock(m_csThis); //读锁
	return ioDev::getIODev(ioAddr,bChn);
}



void ioServer::updateTag2IOAddrBinding()
{
	m_csThis.lock_shared();
	json tagBindings = json::array();
	for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
	{
		ioDev* p = ioSrv.m_vecChildDev[i];
		if (p->m_strTagBind != "")
		{
			json binding;
			binding["ioAddr"] = p->getIOAddrStr();
			binding["tag"] = p->m_strTagBind;
			tagBindings.push_back(binding);
		}
	}
	m_csThis.unlock_shared();
	tds->callAsyn("updateTagBinding", tagBindings.dump());
}

void ioServer::clear()
{
	std::unique_lock<shared_mutex> lock(m_csThis); //写锁
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		delete m_vecChildDev[i];
	}
	m_vecChildDev.clear();
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
	string p = tds->conf->confPath + "/template/conf.json";
	string tplListStr;
	if (fs::readFile(p, tplListStr)) {
		try {
			json jTplList = json::parse(tplListStr);
			for (auto& i : jTplList) {
				CHAN_TEMPLATE ct;
				ct.name = i["name"];
				ct.label = i["label"];
				string tplDataStr;
				string p1 = tds->conf->confPath + "/template/" + ct.name + ".json";
				if (fs::readFile(p1, tplDataStr)) {
					ct.channels = json::parse(tplDataStr);
					m_mapChanTempalte[ct.name] = ct;
				}
			}
		}
		catch (exception& e) {

		}
	}
	return false;
}

void ioServer::refreshSerialIODev()
{
	//从操作系统的设备管理器获得串口列表信息
	vector<sys::COM_INFO> aryNew;
	aryNew = sys::getCOMInfoList();


	//新列表里有，当前列表没有。  为新上线的串口创建对应的ioDev.置为在线
	for (auto& i : aryNew)
	{
		sys::COM_INFO ci = i;
		ioDev* ls = getIODev(ci.portNum);
		if (!ls)
		{
			ls = onChildDevDiscovered(ci.portNum, IO_DEV_TYPE::GW::local_serial);
			if(ls)
				ls->m_devTypeLabel = ci.desc;
		}
	}

	//当前列表里有，新列表没有。 表示离线。 离线的空闲设备，从io设备列表中删除。已配置设备保留
	vector<ioDev*> ary = ioSrv.getChildren(IO_DEV_TYPE::GW::local_serial);
	for (auto& i : ary)
	{
		bool bOnline = false;
		//在新列表里面找的到才在线
		for (auto& newStatus : aryNew)
		{
			if (newStatus.portNum == i->getIOAddrStr())
			{
				bOnline = true;
			}
		}

		if (bOnline == false && i->m_dispositionMode == DEV_DISPOSITION_MODE::spare)
		{
			ioSrv.deleteDescendant(i);
		}
	}
}

bool ioServer::run()
{
	if (tds->conf->edge)
	{
		runAsEdge();
	}
	else
	{
		runAsCloud();
	}
	return false;
}

bool ioServer::runAsCloud()
{
	m_bRunning = true;
	loadChanTemplate();

	int leakDetectPort = tds->conf->getInt("leakDetectPort", 8085);

	m_mapPort2DevType[tds->conf->tdspPort] = IO_DEV_TYPE::DEV::tdsp_device;
	m_mapPort2DevType[tds->conf->mbPort] = IO_DEV_TYPE::GW::rs485_gateway;
	m_mapPort2DevType[tds->conf->iq60Port] = IO_DEV_TYPE::DEV::iq60_gateway;
	m_mapPort2DevType[leakDetectPort] = IO_DEV_TYPE::DEV::leakDetect;

	//启动服务端口
	LOG("[keyinfo][IO服务    ] 端口:" + str::fromInt(tds->conf->tdspPort) + " 设备通信协议 TDSP");
	LOG("[keyinfo][IO服务    ] 端口:" + str::fromInt(tds->conf->mbPort) + " 设备通信协议 modbus RTU over TCP");
	LOG("[keyinfo][IO服务    ] 端口:" + str::fromInt(tds->conf->iq60Port) + " 设备通信协议 IQ60物云通信协议");
	LOG("[keyinfo][IO服务    ] 端口:" + str::fromInt(leakDetectPort) + " 设备通信协议 漏点监测通信协议");

	//io服务 665 TDSP
	m_tcpSrv_tdsp = new tcpSrv();
	m_tcpSrv_tdsp->m_strName = "tdsp";
	m_tcpSrv_tdsp->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_tdsp->run(this, tds->conf->tdspPort))
	{
		
	}
	else
	{
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(tds->conf->tdspPort));
	}


	//io服务 664 Modbus over TCP
	m_tcpSrv_rtu = new tcpSrv();
	m_tcpSrv_rtu->m_strName = "modbus rtu";
	m_tcpSrv_rtu->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_rtu->run(this, tds->conf->mbPort))
	{
		
	}
	else
	{
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(tds->conf->mbPort));
	}

	//io服务 663 IQ60
	m_tcpSrv_iq60 = new tcpSrv();
	m_tcpSrv_iq60->m_strName = "iq60";
	m_tcpSrv_iq60->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_iq60->run(this, tds->conf->iq60Port))
	{
		
	}
	else
	{
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(tds->conf->iq60Port));
	}

	//io服务 663 IQ60
	m_tcpSrv_leakDetect = new tcpSrv();
	m_tcpSrv_leakDetect->m_strName = "leakDetect";
	m_tcpSrv_leakDetect->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_leakDetect->run(this, leakDetectPort))
	{

	}
	else
	{
		LOG("[error][IO服务    ] 启动失败 端口:" + str::fromInt(leakDetectPort));
	}


	//启动所有子设备
	for (auto i : m_vecChildDev)
	{
		i->run();
	}

	//启动IO工作线程
	std::thread io(IOThread);
	io.detach();

	//启动设备发现线程
	ioDiscoverService.run();
	refreshSerialIODev();

	
	return true;
}

bool ioServer::runAsEdge()
{
	m_bRunning = true;

	for (auto i : m_vecChildDev)
	{
		i->run();
	}
	std::thread io(IOThread);
	io.detach();
	
	ioDiscoverService.run();
	refreshSerialIODev();
	return false;
}

void ioServer::stop()
{
	if (m_tcpSrv_iq60)
		m_tcpSrv_iq60->stop();
	if (m_tcpSrv_rtu)
		m_tcpSrv_rtu->stop();
	if (m_tcpSrv_tdsp)
		m_tcpSrv_tdsp->stop();

	LOG("stoping ioServer...");
	ioDev::stop();
	LOG("ioServer stopped");
}

bool ioServer::toJson(json& conf, json opt)
{
	std::shared_lock<shared_mutex> lock(m_csThis);
	conf = json::array();//empty array

	//只差找绑定位号属于某个根位号的设备。
	string rootTag = "";
	string interfaceType = "net"; //默认没有串口，指定接口类型为*所有才发串口
	if (opt != nullptr)
	{
		if(opt.contains("rootTag"))
			rootTag = opt["rootTag"].get<string>();
		if (opt.contains("interface"))
			interfaceType = opt["interface"].get<string>();
	}

	

	for (auto& i : m_vecChildDev)
	{
		json j;

		//为指定所有忽略串口
		if (i->m_devType == IO_DEV_TYPE::GW::local_serial && interfaceType!="*")
		{
			continue;
		}
		
		if (opt != nullptr)
		{
			if (opt.contains("tagBind"))
			{
				string tagBind = opt["tagBind"].get<string>();

				//指定查找智能设备。但是是非智能设备
				if (i->m_strTagBind == "")
				{
					if(tagBind != "")
						continue;
				}
				else
				{
					if (tagBind != "*" && tagBind != i->m_strTagBind)
						continue;
				}
			}
		}

		//启用设备，绑定了监控对象的，根据指定的rootTag进行过滤；
		if (i->m_dispositionMode == DEV_DISPOSITION_MODE::managed)
		{
			if (i->m_strTagBind != "" && rootTag != "")
			{
				if (i->m_strTagBind.find(rootTag) == string::npos)
				{
					continue;
				}
			}
		}


		i->toJson(j, opt);
		string s = j.dump();
		conf.push_back(j);
	}
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

ioDev* ioServer::onChildDevDiscovered(json childDevAddr, string type)
{
	ioDev* p = createIODev(type);
	if (p == nullptr) return nullptr;

	p->m_jDevAddr = childDevAddr;
	if (childDevAddr.is_string())
		p->m_devAddr = p->m_jDevAddr.get<string>();
	else if (childDevAddr.is_object())
	{
		if (childDevAddr.contains("id"))
		{
			p->m_addrMode = DEV_ADDR_MODE::deviceID;
		}
	}
	p->m_dispositionMode = DEV_DISPOSITION_MODE::spare;
	ioSrv.addChild(p);

	//通知设备上线
	p->setOnline();
	logger.logInternal(str::format("[ioDev]空闲设备上线，ioAddr=%s,设备类型=%s",p->getIOAddrStr().c_str(),type.c_str()));

	//通知设备发现
	json j;
	json opt;
	opt["getConf"] = true;
	opt["getChan"] = true;
	opt["getChild"] = true;
	opt["getStatus"] = true;
	p->toJson(j,opt);
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
		if (it->m_devType == IO_DEV_TYPE::DEV::tdsp_device)
		{
			aryDev.push_back(it);
		}
	}
}

void ioServer::handleAppLayerData(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt)
{
	//协议检测
	if (tdsSession->ioDevType == "")//应用层协议类型检测
	{
		//应用层协议智能检测。根据收到的首包数据进行检测
		//傲华尔远程控制协议
		if ((pData[0] == '[' && pData[iLen - 1] == ']') ||
			(pData[0] == '[' && pData[iLen - 1] == '\n' && pData[iLen - 2] == ']')
			)
		{
			tdsSession->iALProto = IO_PROTO::IQ60;
			tdsSession->type = TDS_SESSION_TYPE::iodev + ".IQ60";
		}
	}

	//应用层协议处理
	if (tdsSession->bridgedIoSessionClient != NULL)
	{
		if (tdsSession->iALProto == IO_PROTO::TDSRPC)
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			while (pab->PopPkt(APP_LAYER_PROTO::textEnd2LF))
			{
				tdsSession->bridgedIoSessionClient->send((char*)pab->pkt, pab->iPktLen);
				string s = str::fromBuff((char*)pab->pkt, pab->iPktLen);
				LOG("[IO设备透传]dev->client " + s);
			}
		}
		//iq60的命令行数据包需要组包后再转发，否则可能导致中文utf8字符被分割后无法解析
		else if (tdsSession->iALProto == IO_PROTO::IQ60)
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			while (pab->PopPkt(IsValidPkt_IQ60) || pab->PopPkt(APP_LAYER_PROTO::terminalPrompt))
			{
				tdsSession->bridgedIoSessionClient->send((char*)pab->pkt, pab->iPktLen);
				string s = str::fromBuff((char*)pab->pkt, pab->iPktLen);
				LOG("[IO设备透传]dev->client " + s);
			}
		}
	}
	else if (tdsSession->ioDevType == IO_DEV_TYPE::DEV::iq60_gateway)
	{
		bool regPkt = false;
		if (!tdsSession->m_bAppDataRecved)//首包数据,按照tdsp注册包处理
		{
			string s = str::fromBuff((char*)pData, iLen);
			LOG("[IQ60首发数据]" + s);
			if (s.find("IQ60_") == 0)
			{
				s = s.substr(0, 16);
				LOG("IQ60首发数据," + s);
				regPkt = true;
				string strIoAddr = s.substr(5, s.length() - 5);
				ioSrv.handleDevOnline(strIoAddr, tdsSession);

				if (iLen > 16)
				{
					stream2pkt* pab = &tdsSession->m_alBuf;
					pab->PushStream(pData + 16, iLen - 16);
					while (pab->PopPkt(IsValidPkt_IQ60))
					{
						onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession);
					}
				}
			}
		}

		if (!regPkt)
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			while (pab->PopPkt(IsValidPkt_IQ60))
			{
				onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession);
			}
		}
	}
	else if (tdsSession->ioDevType == IO_DEV_TYPE::DEV::tdsp_device)
	{
		if (handleFirstRegPkt(pData, iLen, tdsSession))
		{
			tdsSession->m_bSingleDevMode = true;
		}
		else
		{
			if (isPkt)
			{
				onRecvPkt_ioDev(pData, iLen, tdsSession);
			}
			else
			{
				stream2pkt* pab = &tdsSession->m_alBuf;
				pab->PushStream(pData, iLen);
				while (pab->PopPkt(IsValidPkt_TDSP))
				{
					if (pab->abandonData != "")
					{
						string remoteAddr = tdsSession->getRemoteAddr();
						LOG("[error]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
						tdsSession->abandonLen += pab->iAbandonBytes;
					}
					tdsSession->iALProto = pab->m_protocolType;
					onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession);
				}
			}
		}
	}
	else if (tdsSession->ioDevType == IO_DEV_TYPE::GW::rs485_gateway)
	{
		//检查是否是imei直接注册包,15位且都是数字，认为是imei
		if (handleFirstRegPkt(pData, iLen, tdsSession))//首包数据,按照tdsp注册包处理
		{
		}
		else
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			bool bRegPkt = false;
			if (!tdsSession->m_bAppDataRecved)//如果是第一包，尝试检查是不是rpc注册包
			{
				if (pab->PopPkt(IsValidPkt_TDSP))
				{
					onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession, true);
					LOG("Modbus网关注册数据包:" + str::bytesToHexStr(pData, iLen));
				}
			}

			while (pab->PopPkt(IsValidPkt_ModbusRTU))
			{
				if (pab->abandonData != "")
				{
					string remoteAddr = tdsSession->getRemoteAddr();
					LOG("[error]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
					tdsSession->abandonLen += pab->iAbandonBytes;
				}
				tdsSession->iALProto = pab->m_protocolType;
				onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession);
			}
		}
	}
	else if (tdsSession->ioDevType == IO_DEV_TYPE::DEV::leakDetect) {
		stream2pkt* pab = &tdsSession->m_alBuf;
		pab->PushStream((unsigned char*)pData, iLen);
		while (pab->PopPkt(IsValidPkt_LeakDetect))
		{
			if (pab->abandonData != "")
			{
				string remoteAddr = tdsSession->getRemoteAddr();
				LOG("[error]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
				tdsSession->abandonLen += pab->iAbandonBytes;
			}
			onRecvPkt_ioDev((unsigned char*)pab->pkt, pab->iPktLen, tdsSession);
		}
	}
}

void ioServer::onRecvPkt_ioDev(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool registerPkt)
{
	try
	{
		if (tdsSession->ioDevType == IO_DEV_TYPE::DEV::tdsp_device)
		{
			string sResp = str::fromBuff((char*)pData, iLen);

			//编解码转换
			if (rpcSrv.isGB2312Pkt(sResp))
			{
				tdsSession->m_charset = "gb2312";
				int ipos = 0; string errChar;
				if (!charCodec::isValidGB2312(sResp, ipos, errChar)) //硬件启用gb2312传输中文后。出bug的可能性很大。做一次有效性检测
				{
					LOG("[error][TDSP]GB2312编码数据包包含非法字符，无法解析\nGB2312字符范围A1A1-FEFE,ascII范围0-7F\n错误字符位置:" + str::fromInt(ipos) + ",错误字符:" + errChar + "\n" + str::bytesToHexStr(pData, iLen));
					return;
				}

				sResp = charCodec::ansi2Utf8(sResp);
			}

			//解析请求基本信息。将设备包中的ioAddr替换为addr。此处tdsp协议有不合理性，后续完善
			sResp = str::replace(sResp, "\"ioAddr\"", "\"addr\"");
			json jResp = json::parse(sResp);
			if (!jResp.contains("method"))
			{
				LOG("[error][TDSP]tdsp设备的协议数据包必须包含method字段\n" + sResp);
				return;
			}
			string method = jResp["method"].get<string>();
			json params;
			if (jResp.contains("params"))
				params = jResp["params"];
			json id = jResp["id"];
			json clientId = jResp["clientId"]; //tds edge模式使用
			tdsSession->lastMethodCalled = method;
			string charset = "utf8";
			if (jResp.contains("charset"))
			{
				charset = jResp["charset"].get<string>();
			}

			//多设备模式或者还没有设备在该session上上线，处理设备上线
			//获取当前session关联的设备
			ioDev* pIoDev = tdsSession->m_IoDev;
			//如果无关联设备或者是多关联模式
			if (!tdsSession->m_bSingleDevMode || tdsSession->m_IoDev == nullptr)
			{
				//获得该io地址的设备对象
				string strIoAddr = jResp["addr"].get<string>();
				if (strIoAddr == "")
				{
					LOG("[error]注册包devRegister中的ioAddr为空，无效");
					return;
				}

				pIoDev = ioSrv.handleDevOnline(strIoAddr, tdsSession);
			}

			pIoDev->m_charset = charset;
			pIoDev->onRecvPkt(jResp);
			LOG("[trace]TDSP响应:\r\n" + sResp + "\r\n");
		}
		else if (tdsSession->ioDevType == IO_DEV_TYPE::GW::rs485_gateway)
		{
			//4g模式下的modbus RTU over tcp 第一包必须发送注册包
			if (registerPkt)
			{
				string sResp = str::fromBuff((char*)pData, iLen);
				json jResp = json::parse(sResp);
				string method = jResp["method"].get<string>();
				string strIoAddr = jResp["ioAddr"].get<string>();
				if (method == "devRegister")
					ioSrv.handleDevOnline(strIoAddr, tdsSession);
			}
			else
			{
				if (tdsSession->m_IoDev)
				{
					tdsSession->m_IoDev->onRecvPkt((char*)pData, iLen);
				}
			}
		}
		else if (tdsSession->ioDevType == IO_DEV_TYPE::DEV::iq60_gateway) {
			onRecvIQ60Pkt(pData, iLen, tdsSession);
		}
		else if (tdsSession->ioDevType == IO_DEV_TYPE::DEV::leakDetect) {
			onRecvPkt_leakDetect(pData, iLen, tdsSession);
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("onRecvPkt_ioDev 处理异常" + errorType);
	}
}

void ioServer::onRecvPkt_leakDetect(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	int id = pData[6];
	string sId = str::fromInt(id);

	//设备上线看做是 给tdsSession->m_IoDev 赋值的过程
	ioDev* pIoDev = tdsSession->m_IoDev;
	if (tdsSession->m_IoDev == nullptr)
	{
		pIoDev = ioSrv.handleDevOnline(sId, tdsSession);
	}

	if (pIoDev)
	{
		ioDev* p = pIoDev;
		p->bindIOSession(tdsSession);
		p->setOnline();
		p->onRecvPkt((char*)pData,iLen);
	}
}


bool ioServer::handleFirstRegPkt(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	if (!tdsSession->m_bAppDataRecved)
	{
		if (iLen == 15 && str::isDigits((char*)pData, iLen))
		{
			string imei = str::fromBuff((char*)pData, iLen);
			LOG("收到首发注册包,15位IMEI格式,IMEI=" + imei);
			ioSrv.handleDevOnline(imei, tdsSession);
			return true;
		}
		else if (iLen > 4 && (str::fromBuff((char*)pData, 4) == "imei" || str::fromBuff((char*)pData, 4) == "IMEI"))
		{
			string imei = str::fromBuff((char*)pData, iLen);
			LOG("收到首发注册包,IMEI前缀格式,IMEI=" + imei);
			ioSrv.handleDevOnline(imei, tdsSession);
			return true;
		}
	}

	return false;
}

//onRecvData需要组包
bool ioServer::OnRecvAppLayerData(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt)
{
	tdsSession->statisOnRecv((char*)pData, iLen);

	DWORD dwDataLen = iLen;

	//处理来自于io设备的数据
	handleAppLayerData(pData, iLen, tdsSession, isPkt);

	tdsSession->m_bAppDataRecved = true;
	return true;
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


		json jSession;
		jSession["type"] = p->type;
		jSession["ip"] = p->ip;
		jSession["port"] = p->port;
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

		string ioAddrInSession = "";
		for (int i = 0; i < p->m_vecIoDev.size(); i++)
		{
			if (i > 0)
				ioAddrInSession += ";";
			ioAddrInSession += p->m_vecIoDev[i];
			ioAddrInSession += ",";
			ioAddrInSession += p->m_vecIoBindTag[i];
		}
		jSession["ioAddr"] = ioAddrInSession;

		string ioAddrInSessionHist = "";
		for (int i = 0; i < p->m_vecHistIoDev.size(); i++)
		{
			if (i > 0)
				ioAddrInSessionHist += ";";
			ioAddrInSessionHist += p->m_vecHistIoDev[i];
			ioAddrInSessionHist += ",";
			ioAddrInSessionHist += p->m_vecHistIoBindTag[i];
		}
		jSession["ioAddrHist"] = ioAddrInSessionHist;

		if (p->type == "video" && p->pTcpSession)
		{
			jSession["sendBytes"] = p->pTcpSession->iSendSucCount;
			jSession["sendFailBytes"] = p->pTcpSession->iSendFailCount;
		}
		jList.push_back(jSession);
	}

	rpcResp.result = jList.dump(2);
}
