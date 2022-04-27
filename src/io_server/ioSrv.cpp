#include "pch.h"
#include "ioSrv.h"
#include "commSrv.h"
#include <thread>
#include "mo.h"
#include "mp.h"
#include "prj.h"
#include "logger.h"
#include "ioChan.h"
#include "ioDev_genicam.h"
#include "rpcHandler.h"
#include "ds.h"

ioServer ioSrv;

void IOThread()
{
	ioSrv.m_bWorkingThreadRunning = true;
	int statisUpdateInterval = 10;

	//加载设备配置缓存
	ioSrv.m_csThis.lock();
	for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
	{
		ioDev* pIoDev = ioSrv.m_vecChildDev[i];
		pIoDev->loadConfBuff();
		pIoDev->loadInfoBuff();
	}
	ioSrv.m_csThis.unlock();

	while (1)
	{
		Sleep(5);

		if (!ioSrv.m_bRunning)
			break;

		if (ioSrv.m_stopCycleAcq)
			continue;

		ioSrv.m_csThis.lock();
		for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
		{
			ioDev* pIoDev = ioSrv.m_vecChildDev[i];
			//空闲设备不轮询数据
			//所有的周期采集命令支持异步处理，doCycleTask不阻塞
			if (pIoDev->m_bRunning)
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
ioServer::ioServer()
{
	m_stopCycleAcq = false;
	m_devType = IO_DEV_TYPE::SERVER::tds;
}
ioServer::~ioServer()
{
}

bool ioServer::loadConf()
{
	string conf;
	if (!fs::readFile(tds->conf->projectConfPath + "/io.json", conf))
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
	opt["onlyConf"] = true;
	toJson(conf,opt);
	string sConf = conf.dump(3);
	if (fs::writeFile(tds->conf->projectConfPath + "/io.json",sConf))
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

		if(tdsSession->iALProto == APP_LAYER_PROTO::TDSRPC)
			pIoDev = ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::DEV::tdsp_device);
		else if(tdsSession->iALProto == APP_LAYER_PROTO::MODBUS_RTU)
			pIoDev = ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::GW::rs485_gateway);
		else if (tdsSession->iALProto == APP_LAYER_PROTO::IQ60)
			pIoDev = ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::DEV::iq60_gateway);
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

void ioServer::rpc_addDev(json& params,RPC_RESP& rpcResp)
{
	string type = params["type"].get<string>();
	if (!params.contains("nodeID"))
	{
		params["nodeID"] = common::guid();
	}

	ioDev* parentDev = this;
	if (params["parentID"] != nullptr) {
		string parentID = params["parentID"].get<string>();
		parentDev = getIODevByNodeID(parentID);
	}
	else
		parentDev = this;

	if (parentDev == NULL)
	{
		rpcResp.error = "device type not supported, type:" + type;
		return;
	}

	ioDev* pd = createIODev(type);
	if (pd)
	{
		pd->loadConf(params);
		parentDev->addChild(pd);
		saveConf();
		rpcResp.result = "\"ok\"";
		pd->toJson(params);
		rpcSrv.notify("devAdded", params);
	}
	else {
		rpcResp.error = "device type not supported, type:" + type;
	}
}

void ioServer::rpc_deleteDev(json& params, RPC_RESP& rpcResp)
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

void ioServer::rpc_modifyDev(json& params, RPC_RESP& rpcResp)
{
	string sNodeId = params["nodeID"].get<string>();
	bool bFinded = false;
	ioDev* p = getIODevByNodeID(sNodeId);

	if (p)
	{
		p->loadConf(params);
		saveConf();
		p->toJson(params);
		rpcSrv.notify("devModified", params);
		rpcResp.result = params.dump(2);
	}
	else {
		rpcResp.error = "can not find device of specified NodeID:" + sNodeId;
	}
}

void ioServer::rpc_disposeDev(json& params, RPC_RESP& rpcResp)
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

ioDev* ioServer::getIODev(string ioAddr)
{
	std::shared_lock<shared_mutex> lock(m_csThis); //读锁
	return ioDev::getIODev(ioAddr);
}

ioDev* ioServer::getIODevByTag(string tag)
{
	std::shared_lock<shared_mutex> lock(m_csThis); //读锁
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		//备用的设备允许和相同的位号绑定，但是实际没有效果
		//因为在实际工程当中，可能会删除一台在线的设备变成备用。绑定关系没改
		//然后将另外一台设备和相同的位号绑定。此处不让那个备用的绑定影响启用的设备。
		if (p->m_dispositionMode == DEV_DISPOSITION_MODE::spare)
			continue;
		if (p->m_strTagBind == tag)
		{
			return p;
		}
	}
	return nullptr;
}

void ioServer::updateTag2IOAddrBinding()
{
	m_csThis.lock_shared();
	for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
	{
		ioDev* p = ioSrv.m_vecChildDev[i];
		string ioAddr = p->getIOAddrStr();
		if (p->m_strTagBind != "")
		{
			MO* pmo = prj.GetMOByTag(p->m_strTagBind);
			if (pmo)
			{
				pmo->m_strIoAddrBind = ioAddr;
			}
		}
	}
	m_csThis.unlock_shared();
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

	//io服务 665 TDSP
	m_tcpSrv_tdsp = new tcpSrv();
	m_tcpSrv_tdsp->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	if (m_tcpSrv_tdsp->run(&ds, tds->conf->ioServerPort))
	{
		LOG("[keyinfo][IO服务   ] 端口:" + str::fromInt(tds->conf->ioServerPort) + " 设备通信协议 TDSP");
	}
	else
	{
		LOG("[error][IO服务   ] 启动失败 端口:" + str::fromInt(tds->conf->ioServerPort));
	}


	//io服务 664 Modbus over TCP
	m_tcpSrv_rtu = new tcpSrv();
	m_tcpSrv_rtu->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	int ioSrvPort_rtu = 664;
	if (m_tcpSrv_rtu->run(&ds, ioSrvPort_rtu))
	{
		LOG("[keyinfo][IO服务   ] 端口:" + str::fromInt(ioSrvPort_rtu) + " 设备通信协议 modbus RTU over TCP");
	}
	else
	{
		LOG("[error][IO服务   ] 启动失败 端口:" + str::fromInt(ioSrvPort_rtu));
	}

	//io服务 663 IQ60
	m_tcpSrv_iq60 = new tcpSrv();
	m_tcpSrv_iq60->keepAliveTimeout = tds->conf->tcpKeepAliveIO;
	int ioSrvPort_iq60 = 663;
	if (m_tcpSrv_iq60->run(&ds, ioSrvPort_iq60))
	{
		LOG("[keyinfo][IO服务   ] 端口:" + str::fromInt(ioSrvPort_iq60) + " 设备通信协议 IQ60物云通信协议");
	}
	else
	{
		LOG("[error][IO服务   ] 启动失败 端口:" + str::fromInt(ioSrvPort_iq60));
	}

	
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
	p->m_bOnline = true;
	logger.logInternal("[ioDev]空闲设备上线，ioAddr=" + p->getIOAddrStr());
	ioSrv.addChild(p);


	json j;
	p->toJson(j);
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

