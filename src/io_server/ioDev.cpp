#include "pch.h"
#include "ioDev.h"
#include "ioChan.h"
#include "ioDev_genicam.h"
#include "ioProto/proto_rtu.hpp"
#include "logger.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "webSrv.h"


bool isBatchLink(string addr)
{
	if (addr.find("#") != string::npos)
	{
		return true;
	}
	else
	{
		return false;
	}
}


std::map<string, fp_createDev> mapDevCreateFunc;
ioDev* createIODev(string type)
{
	if (type == "")
		return nullptr;

    ioDev* p = NULL;
	
	if (mapDevCreateFunc.find(type) != mapDevCreateFunc.end())
	{
		fp_createDev func_create = mapDevCreateFunc[type];
		p = func_create();
	}
	else if (type == "genicam")
	{
#ifdef ENABLE_GENICAM
		p = new ioDev_genicam();
#endif
	}
	else
	{
		LOG("[warn]不支持设备类型:" + type + ",您可以联系厂家获取支持该类型设备的专业版");
	}

	if (p)
	{
		p->m_confNodeId = common::guid();
	}

	return p;
}


void ioDev::AutoDataLink(MO* mo) {

}


bool ioDev::IsGateway()
{
	if (m_devType == "can_gateway" ||
		m_devType == "modbus_gateway")
	{
		return true;
	}

	return false;
}

bool ioDev::m_bAsynAcqMode = false;
int ioDev::m_heartBeatInterval = 3;



ioDev::ioDev(void)
{
	m_bWorkingThreadRunning = false;
	m_bIsWaitingResp = false;
	m_bEnableIoLog = true;
	m_bEnableAcq = true;
	m_bRunning = true; //是否启动了自动工作 （采集线程是否启动）
	m_bEnableAcq = true;
	m_dispositionMode = DEV_DISPOSITION_MODE::managed;
	m_pCommAddrInfo = NULL;
	m_pParent = NULL;
	m_bOnline = false;
	m_iSendDataFailCount = 0;
	memset(&m_stLastHeartbeatTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stLastSetClockTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stOnlineTime, 0, sizeof(SYSTEMTIME));
	GetLocalTime(&m_stOfflineTime);
	timeopt::setAsTimeOrg(m_stLastChanDataTime);
	timeopt::setAsTimeOrg(m_stLastAcqTime);
	timeopt::setAsTimeOrg(m_stLastAlarmStatusTime);
	timeopt::setAsTimeOrg(m_stLastReqSendTime);
	GetLocalTime(&m_stLastActiveTime);
	m_pMO = NULL;
	m_pRecvCallback = NULL;
	m_pCallbackUser = NULL;
	pSessionClientBridge = NULL;
	m_fAcqInterval = 30;
	pIOSession = NULL;
	m_onlineInfoQueried = false;
}

ioDev::~ioDev(void)
{
	if (pIOSession)
	{
		pIOSession->m_IoDev = nullptr;
		ioSrv.handleDevOnlineAsyn(pIOSession->m_ioAddr, pIOSession);
	}
}

bool ioDev::run()
{
	m_bRunning = true;
	if (m_addrMode == DEV_ADDR_MODE::tcpServer)
	{
		string ip;
		if (m_jDevAddr.contains("ip"))
		{
			ip = m_jDevAddr["ip"].get<string>();
		}
		else
		{
			LOG("[error]IODev启动失败,设备地址模式=tcpServer,没有找到ip配置");
			return false;
		}


		int port;
		if (m_jDevAddr.contains("port"))
		{
			port = m_jDevAddr["port"].get<int>();
		}
		else
		{
			LOG("[error]IODev启动失败,设备地址模式=tcpServer,没有找到port配置");
			return false;
		}
		m_tcpClt.run(&ioSrv, ip, port);
		LOG("[IO设备]连接TCP服务模式设备,设备类型:%s,设备地址:%s", m_devType.c_str(), getDevAddrStr().c_str());
	}
	return true;
}

void ioDev::stop()
{
	m_bRunning = false;
	m_tcpClt.stop();

	for (auto i : m_vecChildDev)
	{
		i->stop();
	}
	if (m_bWorkingThreadRunning)
	{
		m_signalWorkThreadExit.wait();
	}
}

// 默认选项
// opt.getChan = true
// opt.getStatus = false
bool ioDev::toJson(json& conf, json opt)
{
	shared_lock<shared_mutex> lock(m_csThis);
	DEV_QUERIER querier = parseQueryOpt(opt);

	//配置数据 - 保存在配置文件中
	if (querier.getConf) {
		conf["addr"] = m_jDevAddr;
		conf["type"] = m_devType;
		conf["typeLabel"] = m_devTypeLabel;
		conf["level"] = m_level;
		conf["manageStatus"] = m_dispositionMode;
		if (m_fAcqInterval != 0)
			conf["acqInterval"] = m_fAcqInterval;
		conf["enableAcq"] = m_bEnableAcq;
		if (m_strTagBind != "")
			conf["tagBind"] = m_strTagBind;
		if (m_strChanTemplate != "")
			conf["chanTemplate"] = m_strChanTemplate;
		conf["nodeID"] = m_confNodeId;
	}

	//运行时数据 - 与实际硬件设备关联的状态信息，硬件上送的数据
	if(querier.getStatus)
	{
		if (m_charset != "")
			conf["charset"] = m_charset;
		
		conf["online"] = m_bOnline;
		conf["connected"] = m_bConnected;
		if (pIOSession != nullptr)
		{
			conf["remoteIP"] = pIOSession->getRemoteAddr();
		}

		//详细信息
		conf["alarmUpdateTime"] = timeopt::st2str(m_stLastAlarmStatusTime);

		if (m_jAlarmStatus != nullptr) {
			conf["chanUpdateTime"] = timeopt::st2str(m_stLastChanDataTime);
			conf["alarmStatus"] = m_jAlarmStatus;
		}


		//动态配置 - 动态生成的配置信息 不保存在配置文件中，仅为方便接口调用者使用
		conf["ioAddr"] = getIOAddrStr();
		conf["addrMode"] = m_addrMode;
		conf["enableAlarm"] = tds->conf->enableGlobalAlarm;
	} 

	//升级状态信息数据
	if (querier.getUpgradeInfo) {
		json jui;
		jui = m_upgradeInfo.toJson();
		conf["upgradeInfo"] = jui;
	}

	if(querier.getChild)
	{
		if (m_vecChildDev.size() > 0)
		{
			json children = json::array();
			for (auto& i : m_vecChildDev)
			{
				json j;
				i->toJson(j, opt);
				children.push_back(j);
			}
			conf["children"] = children;
		}
	}

	if (querier.getChan && m_channels.size() > 0)
	{
		json channels = json::array();
		for (auto& i : m_channels)
		{
			//空闲通道作为状态数据，必须指定获取状态才返回
			if (!querier.getStatus && i->m_dispositionMode == DEV_DISPOSITION_MODE::spare) {
				continue;
			}
			json j;
			i->toJson(j, opt);
			channels.push_back(j);
		}
		conf["channels"] = channels;
	}
	
	return true;
}

bool ioDev::getStatus(json& status, string opt)
{
	return false;
}


bool ioDev::getChanStatus(json& statusList)
{
	std::shared_lock<shared_mutex> lock(m_csThis);
	for (int i = 0; i < m_channels.size(); i++)
	{
		ioDev* p = m_channels[i];
		p->getChanStatus(statusList);
	}

	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		p->getChanStatus(statusList);
	}

	return true;
}

bool ioDev::loadConf(json& conf)
{
	std::unique_lock<shared_mutex> lock(m_csThis);
	if (conf.contains("addr"))
	{
		m_jDevAddr = conf["addr"];

		if (m_jDevAddr.is_object())
		{
			if (m_jDevAddr["id"] != nullptr)
				m_addrMode = DEV_ADDR_MODE::deviceID;
			else if (m_jDevAddr.contains("port"))
				m_addrMode = DEV_ADDR_MODE::tcpServer;
			else
				m_addrMode = DEV_ADDR_MODE::tcpClient;
		}
	}

	if (conf["acqInterval"] != nullptr)
	{
		m_fAcqInterval = conf["acqInterval"].get<float>();
	}

	if (conf["enableAcq"] != nullptr)
	{
		m_bEnableAcq = conf["enableAcq"].get<bool>();
	}

	if (conf["manageStatus"] != nullptr)
	{
		m_dispositionMode = conf["manageStatus"].get<string>();
	}

	if (conf["chanTemplate"] != nullptr)
	{
		m_strChanTemplate = conf["chanTemplate"].get<string>();
	}

	if (conf["nodeID"] != nullptr)
	{
		m_confNodeId = conf["nodeID"].get<string>();
	}
	if (m_confNodeId == "") //该操作主要用于升级没有nodeId的配置
		m_confNodeId = common::guid();

	if (conf["tagBind"] != nullptr)
	{
		m_strTagBind = conf["tagBind"];
		
		//启用设备，才更新绑定的mo中的 关联io地址信息。 备用的不更新。
		//否则备用的绑定地址和启用的相同时，可能会错误的使用备用设备的信息
		if (m_dispositionMode == DEV_DISPOSITION_MODE::managed && m_strTagBind != "")
		{
			json tagBinding = json::array();
			json binding;
			binding["ioAddr"] = getIOAddrStr();
			binding["tag"] = m_strTagBind;
			tagBinding.push_back(binding);
			tds->callAsyn("updateTagBinding", tagBinding.dump());
		}
	}

	if (conf["children"] != nullptr)
	{
		deleteChildren();
		json childDev = conf["children"];

		for (auto i : childDev)
		{
			ioDev* pChild = nullptr;
			if (i["level"] == "device")
			{
				pChild = createIODev(i["type"].get<string>());
				pChild->loadConf(i);
				pChild->m_pParent = this;
				m_vecChildDev.push_back(pChild);
			}
		}
	}


	if (conf["channels"] != nullptr)
	{
		deleteAllChannels();
		
		json childDev = conf["channels"];

		for (auto i : childDev)
		{
			ioChannel* pdc = nullptr;
			pdc = new ioChannel();
			pdc->m_jDevAddr = i["addr"];
			if (i["addr"].is_string())
				pdc->m_devAddr = i["addr"].get<string>();
			pdc->loadConf(i);
			//批量映射配置.主要用于mqtt的场景，当mqtt的路径结构和MOTree的树结构一致时
			if (pdc->m_devAddr != "" && isBatchLink(pdc->m_devAddr)) //datachannel instance of the batch data link will be created dynamicly when the channel data is received
			{
				m_mapBatchDataLink[pdc->m_devAddr] = i["tagBind"];
			}
			addChannel(pdc);

			if (m_strTagBind != "") {
				pdc->m_strTagBind = str::trimPrefix(pdc->m_strTagBind, m_strTagBind);
				pdc->m_strTagBind = str::trimPrefix(pdc->m_strTagBind, ".");
			}
		}
	}
		
	return true;
}

void ioDev::addChannel(ioChannel* pdc)
{
	pdc->m_pParent = this;
	m_channels.push_back(pdc);
	m_mapDataChannel[pdc->getDevAddrStr()] = pdc;
}

bool ioDev::connect()
{
	return false;
}

bool ioDev::disconnect()
{
	return false;
}

string ioDev::getDesc()
{
	
	return "";
}

void ioDev::triggerCycleAcq()
{
	timeopt::setAsTimeOrg(m_stLastAcqTime);
}

DEV_QUERIER ioDev::parseQueryOpt(json& opt)
{
	DEV_QUERIER q;
	if (opt.contains("getStatus")) {
		q.getStatus = opt["getStatus"].get<bool>();
	};
	if (opt.contains("getChan")) {
		q.getChan = opt["getChan"].get<bool>();
	}
	if (opt.contains("getChild")) {
		q.getChild = opt["getChild"].get<bool>();
	}
	if (opt.contains("getConf")) {
		q.getConf = opt["getConf"].get<bool>();
	}
	if (opt.contains("getDetail")) {
		q.getDetail = opt["getDetail"].get<bool>();
	}
	if (opt.contains("getUpgradeInfo")) {
		q.getUpgradeInfo = opt["getUpgradeInfo"].get<bool>();
	}
	return q;
}

ioDev* ioDev::getIODevByNodeID(string nodeID)
{
	std::shared_lock<shared_mutex> lock(m_csThis); //读锁
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_confNodeId == nodeID)
		{
			return p;
		}

		ioDev* ptmp = p->getIODevByNodeID(nodeID);
		if (ptmp)
			return ptmp;
	}
	return nullptr;
}

bool ioDev::deleteIODevByNodeID(string nodeID)
{
	std::unique_lock<shared_mutex> lock(m_csThis);
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_confNodeId == nodeID)
		{
			m_vecChildDev.erase(m_vecChildDev.begin() + i);
			delete p;
			return true;
		}

		if (p->deleteIODevByNodeID(nodeID))
			return true;
	}
	return false;
}

ioDev* ioDev::getIODev(string ioAddr,bool bChn)
{
	vector<string> vecNodeName;
	str::split(vecNodeName, ioAddr, "/");

	ioDev* treeNode = NULL;
	vector<ioDev*>* vecChildNode = &m_vecChildDev;
	bool findDev = false;
	//根据节点的名字，在树型结构上一层层往下找
	for (int i = 0; i < vecNodeName.size(); i++)
	{
		string nodeName = vecNodeName[i];

		bool findNode = false;
		for (auto& it : *vecChildNode)
		{
			string nodeNameTmp = it->getDevAddrStr();
			if (bChn) {
				str::hanZi2Pinyin(nodeNameTmp, nodeNameTmp);
				str::hanZi2Pinyin(nodeName, nodeName);
			}

			if (nodeNameTmp == nodeName)
			{
				findNode = true;
				treeNode = it;
				if (i == vecNodeName.size() - 1)//找到了最后一个节点
				{
					findDev = true;
				}
				break;
			}
		}

		if (findNode)
		{
			vecChildNode = &treeNode->m_vecChildDev;
		}
		else
		{
			break;
		}
	}

	if (findDev)
		return treeNode;

	return nullptr;
}

ioDev* ioDev::getIODev(json& ioAddr)
{
	for (auto& it : m_vecChildDev)
	{
		if (it->m_jDevAddr == ioAddr)
		{
			return it;
		}

		ioDev* p = it->getIODev(ioAddr);
		if (p)
			return p;
	}
	return nullptr;
}

vector<ioDev*> ioDev::getChildren(string devType)
{
	vector<ioDev*> ary;
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_devType == devType)
		{
			ary.push_back(p);
		}

		vector<ioDev*> aryChild = p->getChildren(devType);
		ary.insert(ary.end(), aryChild.begin(), aryChild.end());
	}
	return ary;
}


json ioDev::getAddr()
{
	json j;
	j = m_devAddr;
	return j;
}

string ioDev::getIOAddrStr()
{
	string devAddr = getDevAddrStr();
	ioDev* pParent = m_pParent;
	while (pParent && pParent->m_devType != IO_DEV_TYPE::SERVER::tds)
	{
		devAddr = pParent->getDevAddrStr() + "/" + devAddr;
		pParent = pParent->m_pParent;
	}
		
	return devAddr;
}

string ioDev::getDevAddrStr()
{
	string devAddr;
	if (m_jDevAddr.is_object())
	{
		if (m_jDevAddr["id"] != nullptr)
		{
			devAddr = m_jDevAddr["id"].get<string>();
		}
		else if (m_jDevAddr["ip"] != nullptr)
		{
			devAddr = m_jDevAddr["ip"].get<string>();
			if (m_jDevAddr["port"] != nullptr)
			{
				int remotePort = m_jDevAddr["port"].get<int>();
				devAddr += ":" + str::fromInt(remotePort);
			}
		}
		else if (m_jDevAddr.contains("regOffset") && m_jDevAddr.contains("regType"))
		{
			string regTypeAddr;
			string regType = m_jDevAddr["regType"].get<string>();
			if (regType == MODBUS_REG_TYPE::coil)
				regTypeAddr = "C";
			else if (regType == MODBUS_REG_TYPE::discreteInput)
				regTypeAddr = "DI";
			else if (regType == MODBUS_REG_TYPE::holdingRegister)
				regTypeAddr = "HR";
			else if (regType == MODBUS_REG_TYPE::inputRegister)
				regTypeAddr = "IR";

			devAddr = regTypeAddr + "/" + str::fromInt(m_jDevAddr["regOffset"].get<int>());
		}
	}
	else if(m_jDevAddr.is_string()){
		devAddr = m_jDevAddr.get<string>();
	}
	else
	{
		devAddr = "";
	}
	return devAddr;
}


ioDev* ioDev::getIODevByTag(string tag)
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

		ioDev* pTmp = p->getIODevByTag(tag);
		if (pTmp)
		{
			return pTmp;
		}
	}
	return nullptr;
}

bool ioDev::CommLock(int dwTimeoutMS)
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

void ioDev::CommUnlock()
{
	m_csCommLock.unlock();
}

bool ioDev::SendPkt(PKT_DATA& pkt)
{
	return sendData((char*)pkt.data, pkt.len);
}

bool ioDev::sendData(char* pData, int iLen)
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

bool ioDev::sendStr(string& str)
{
	return sendData((char*)str.c_str(), str.length());
}

bool ioDev::SendHeartbeatPkt()
{
	return false;
}

bool ioDev::onRecvPkt(json jPkt)
{
	return false;
}

void notifyDevOnline(json jNotify)
{
	setThreadName("notify dev online thread");
	string ioAddr = jNotify["ioAddr"];
	rpcSrv.notify("devOnline", jNotify);
}

void notifyDevOffline(json jNotify)
{
	setThreadName("notify dev offline thread");
	string ioAddr = jNotify["ioAddr"];
	rpcSrv.notify("devOffline", jNotify);
}

void ioDev::setOnline()
{
	//[问题]观察到有pIOSession已经为空，也就是说链接已经断开。却还有缓存数据没有处理，导致处理后设置为上线的问题
	//该问题需优化
	if (m_bOnline == false)
	{
		m_bOnline = true;
		GetLocalTime(&m_stOnlineTime);
		m_onlineInfoQueried = false;
		json jNotify;
		jNotify["ioAddr"] = getIOAddrStr();
		jNotify["nodeID"] = m_confNodeId;
		if (m_strTagBind != "")
			jNotify["tag"] = m_strTagBind;
		thread t(notifyDevOnline, jNotify);
		t.detach();
	}

	//if (pIOSession == nullptr)
	//{
	//	LOG("[error]IO设备链接已断开，但仍在进行该设备的数据接收处理,ioAddr=" + getIOAddrStr());
	//}
}

void ioDev::setOffline()
{
	if (m_bOnline)
	{
		m_bOnline = false;
		json jNotify;
		jNotify["ioAddr"] = getIOAddrStr();
		jNotify["nodeID"] = m_confNodeId;
		if (m_strTagBind != "")
			jNotify["tag"] = m_strTagBind;
		thread t(notifyDevOffline, jNotify);
		t.detach();
	}
}

bool ioDev::isConnected()
{
	return false;
}

int ioDev::GetAcqInterval()
{
	return 0;
}

void ioDev::OnRequestTimeout(int cmd1, int cmd2)
{

}


void ioDev::DoCycleTask()
{
	PKT_DATA req, resp;
	if (timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > ioDev::m_heartBeatInterval&& ioDev::m_heartBeatInterval > 0)
	{
		SendHeartbeatPkt();
		GetLocalTime(&m_stLastHeartbeatTime);
	}
}

void ioDev::checkAcqReqTimeout()
{
	if (timeopt::CalcTimePassSecond(m_stLastReqSendTime) > 5)
	{
	}	
}

bool ioDev::CmdRequestSync(char* pReqData, int iReqLen, char* pRespData, int& iRespLen)
{
	PKT_DATA req((unsigned char*)pReqData,iReqLen), resp;

	if (!CmdRequestSync(req, resp))
	{
		return false;
	}

	memcpy(pRespData, resp.data, resp.len);
	iRespLen = resp.len;
	return true;
}

bool ioDev::CmdRequestSync(PKT_DATA& req, PKT_DATA& resp, int iRetryCount, string strLogMsgWhenSend)
{
	//REQ_PARAM reqParam;
	//if (iRetryCount > 0)
	//	reqParam.iRetryCount = iRetryCount;

	//bool bRet = commSrv.RequestAndWaitResponse(&req, &resp, getIOAddr(), &reqParam);

	//if (bRet)
	//	resp.UnPack();

	//return bRet;
	return false;
}

bool ioDev::OnRecvData(char* pData, int iLen)
{
	PKT_DATA pkt;
	if (!pkt.UnPack(pData, iLen))
		return false;

	bool bRetu = false;


	return false;
}

bool ioDev::OnRecvData(SYSTEMTIME dataTime, char* pData, int iLen)
{
	return true;
}

ioChannel* ioDev::getChanByDevAddr(string addr)
{
	for (auto i : m_mapDataChannel)
	{
		if (i.second->getDevAddrStr() == addr) return i.second;
	}

	for (auto i : m_mapBatchDataLink)
	{
		string s = i.first;
		s = str::trim(s, "#");
		if (addr.find(s) == 0)
		{
			string wildCardVal = addr.substr(s.length(), addr.length() - s.length());
			ioChannel* p = new ioChannel();
			p->m_devAddr = addr;
			string bindTag = i.second;
			bindTag = str::replace(bindTag, "*", wildCardVal);
			bindTag = str::replace(bindTag, "/", ".");
			p->m_strTagBind = bindTag;
			m_mapDataChannel[addr] = p;
			return p;
		}
	}
	return NULL;
}

ioChannel* ioDev::getChanByIOAddr(string addr)
{
	for (int i = 0; i < m_channels.size(); i++)
	{
		ioChannel* pC = m_channels[i];
		if (pC->getIOAddrStr() == addr)
			return pC;
	}

	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* pChild = m_vecChildDev[i];
		ioChannel* pC = pChild->getChanByIOAddr(addr);
		if (pC)
			return pC;
	}

	return NULL;
}

ioChannel* ioDev::GetDataChannelByMPTag(string strMPTag)
{
	for (auto it : m_mapDataChannel)
	{
		if(it.second->m_strTagBind == strMPTag) return it.second;
	}
	return NULL;
}

void ioDev::saveConfBuff()
{
	string path = tds->db->getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/conf.json";
	fs::createFolderOfPath(path);
	string data = m_jConf.dump(4);
	fs::writeFile(path, data);
}

bool ioDev::loadConfBuff()
{
	string path = tds->db->getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/conf.json";
	string s;
	if (!fs::readFile(path, s))
		return false;
	try
	{
		m_jConf = json::parse(s);
	}
	catch (std::exception& e)
	{
		return false;
	}
	
	return true;
}

void ioDev::saveInfoBuff()
{
	string path = tds->db->getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/info.json";
	fs::createFolderOfPath(path);
	string data = m_jInfo.dump(4);
	fs::writeFile(path, data);
}

bool ioDev::loadInfoBuff()
{
	string path = tds->db->getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/info.json";
	string s;
	if (!fs::readFile(path, s))
		return false;
	try
	{
		m_jInfo = json::parse(s);
	}
	catch (std::exception& e)
	{
		return false;
	}

	return true;
}

void ioDev::saveStatusBuff()
{
	string path = tds->db->getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/status.json";
	fs::createFolderOfPath(path);
	json status;
	status["alarms"] = m_jAlarmStatus;
	status["channels"] = m_jAcq;
	string data = status.dump(4);
	fs::writeFile(path, data);
}

bool ioDev::loadStatusBuff()
{
	string path = tds->db->getPath_dbRoot() + "/devices/" + getIOAddrStr() + "/status.json";
	string s;
	if (!fs::readFile(path, s))
		return false;
	try
	{
		json status = json::parse(s);
		m_jAlarmStatus = status["alarms"];
		m_jAcq = status["channels"];
	}
	catch (std::exception& e)
	{
		return false;
	}

	return true;
}

bool ioDev::addChild(ioDev* p)
{
	m_csThis.lock();
	p->m_pParent = this;
	if (p->m_level == "channel")
	{
		m_channels.push_back((ioChannel*)p);
		m_mapDataChannel[p->m_devAddr] = (ioChannel*)p;
	}
	else
	{
		m_vecChildDev.push_back(p);
	}
	m_csThis.unlock();
	return true;
}

void ioDev::deleteChildren()
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		delete m_vecChildDev[i];
	}
	m_vecChildDev.clear();
}

void ioDev::deleteAllChannels()
{
	m_mapDataChannel.clear();
	for (int i = 0; i < m_channels.size(); i++)
	{
		delete m_channels[i];
	}
	m_channels.clear();
}

void ioDev::deleteChild(ioDev* p)
{
	for (int i=0;i<m_vecChildDev.size();i++)
	{
		ioDev* pTemp = m_vecChildDev.at(i);
		if (pTemp == p)
		{
			m_vecChildDev.erase(m_vecChildDev.begin() + i);
			break;
		}
	}
}

void ioDev::deleteDescendant(ioDev* p)
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* pTemp = m_vecChildDev.at(i);
		pTemp->deleteDescendant(p);
		if (pTemp == p)
		{
			m_vecChildDev.erase(m_vecChildDev.begin() + i);
			break;
		}
	}
}


ioDev* ioDev::getChild(string devAddr)
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev.at(i);
		if (p->getDevAddrStr() == devAddr)
			return p;
	}
	return nullptr;
}

bool ioDev::IsAsynPacket(PKT_DATA* pd)
{
	////除了当前正在同步请求的命令，其他都做异步处理
	//if (m_pCommAddrInfo->strInSyncCmdID == pd->GetCmdID()) //这条命令正在进行同步通讯，不能异步处理
	//{
	//	return false;
	//}

	//if (m_pCommAddrInfo->strInSyncCmdID == "*")
	//{
	//	return false;
	//}

	return true;
}

bool ioDev::startUploadFirmware(string firmwareFileName)
{
	return false;
}

bool ioDev::NotNeedGateway()
{
	if (str::isIp(m_devAddr))
		return true;

	return false;
}

void ioDev::setRecvCallback(void* pUser, fp_ioAddrRecvCallback callback)
{
	//LOG("ioAddr=" + getIOAddrStr() + ",设置接收回调" + str::fromInt((DWORD)pUser));
	m_pCallbackUser = pUser; 
	m_pRecvCallback = callback; 
	if (m_pCallbackUser == nullptr)
		m_bInUse = false;
	else
		m_bInUse = true;
}

string ioDev::GetCommIP()
{
	if (m_devAddr.find('.') !=  string::npos || m_devAddr.find("COM") != string::npos) //如果自己配置了IP，那么该ip为该设备的ip或者是该设备网关的ip
	{
		return m_devAddr;
	}
	else //没有配置ip，使用父网关采集设备的ip
	{
		if (m_pParent && m_pParent->IsGateway())
		{
			return m_pParent->m_devAddr;
		}
	}

	return "";
}

void ioDev::SendToChild(SYSTEMTIME dataTime, char* pData, int iLen, string strID)
{
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		if (m_vecChildDev.at(i)->m_devAddr == strID)
		{
			m_vecChildDev.at(i)->OnRecvData(dataTime, pData, iLen);
		}
	}
}

ioChannel* ioDev::getChanByTag(string tag)
{
	for (auto& child : m_channels)
	{
		ioChannel* pC = (ioChannel*)child;
		if (pC->m_strTagBind == tag || tag == pC->m_pParent->m_strTagBind + "." + pC->m_strTagBind)
		{
			return pC;
		}
	}


	for (auto& child : m_vecChildDev)
	{
		ioChannel* pC = child->getChanByTag(tag);
		if (pC)
			return pC;
	}

	return nullptr;
}

void ioDev::bindIOSession(shared_ptr<TDS_SESSION> ioSession)
{
	std::unique_lock<mutex> lock(m_csIOSession);
	
	//已绑定
	if (pIOSession == ioSession)
		return;

	//1个tcp链接对应1个io设备的场景
	if(ioSession!=nullptr)
		ioSession->m_IoDev = this;

	if (pIOSession != ioSession && ioSession != nullptr && isConnected())
	{
		string ioAddr = getIOAddrStr();
		string devInfo = "ioAddr=" + getIOAddrStr() + ",tag=" + m_strTagBind;
		LOG("[warn][ioDev]老连接未断开，设备在新连接上线。设备:" + devInfo + ",老连接:" + pIOSession->getRemoteAddr() + ",新连接:" + ioSession->getRemoteAddr());
		

		//解除原有session对该io设备的绑定
		pIOSession->m_IoDev = nullptr;
		//1个tcp链接对应 多个 io设备的场景
		//应用层数据包包含地址信息时，同一个tcp链接可以用于多个设备通信。
		//从老的连接里面把ioAddr映射删除，防止老连接断开造成设备掉线。 容错机制
		for (int i = 0; i < pIOSession->m_vecIoDev.size(); i++)
		{
			string temp = pIOSession->m_vecIoDev[i];
			if (temp == ioAddr)
			{
				pIOSession->m_vecIoDev.erase(pIOSession->m_vecIoDev.begin() + i);
				pIOSession->m_vecIoBindTag.erase(pIOSession->m_vecIoBindTag.begin() + i);
				LOG("[ioDev]删除" + pIOSession->getRemoteAddr() + "中对" + devInfo + "的映射");
				break;
			}
		}
	}


	//新的有效连接
	if (ioSession != nullptr && ioSession != pIOSession)
	{
		triggerCycleAcq();
	}


	pIOSession = ioSession;

	if (ioSession == nullptr)
		return;

	bool bExist = false;
	string ioAddr = this->getIOAddrStr();
	for (int i = 0; i < ioSession->m_vecIoDev.size(); i++)
	{
		string tmp = ioSession->m_vecIoDev[i];
		if (tmp == ioAddr)
			bExist = true;
	}
	if (!bExist)
	{
		ioSession->m_vecIoDev.push_back(ioAddr);
		ioSession->m_vecIoBindTag.push_back(this->m_strTagBind);
		ioSession->m_vecHistIoDev.push_back(ioAddr);
		ioSession->m_vecHistIoBindTag.push_back(this->m_strTagBind);
	}
}


CCanTransparentGateway::CCanTransparentGateway()
{
	m_devType = "can_gateway";
}


void ioDev::statisOnRecv(char* recvData, int len, string addr)
{
	if (commpktSessions.size() == 0)
		return;

	json j;
	SYSTEMTIME st;
	GetLocalTime(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["ioAddr"] = addr;
	j["type"] = "接收";
	j["len"] = len;
	j["data"] = str::bytesToHexStr(recvData, len);
	string s = j.dump();

	sendToCommLog(s);
}


void ioDev::statisOnSend(char* sendData, int len, string addr)
{
	if (commpktSessions.size() == 0)
		return;

	json j;
	SYSTEMTIME st;
	GetLocalTime(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["ioAddr"] = addr;
	j["type"] = "发送";
	j["len"] = len;
	j["data"] = str::bytesToHexStr(sendData, len);
	string s = j.dump();

	sendToCommLog(s);
}


int UPGRADE_INFO::calcPktNum(int pl)
{
	int pn = fileLen / pl;
	if (fileLen % pl)
		pn++;
	return pn;
}

bool UPGRADE_INFO::loadFirmwareFile(string fn, int pl)
{
	if (fileData) delete fileData;
	fileLen = 0;
	fileName = fn;
	binPath = fs::appPath() + "/files/firmware/" + fileName;
	if (fs::readFile(binPath, fileData, fileLen)) {
		pktLen = pl;
		pktNum = calcPktNum(pktLen);
		currentPktNo = 0;
		fileCrc = common::N_CRC16(fileData, fileLen);
		vector<string> infoList;
		str::split(infoList, fileName, "_");
		if (infoList.size() < 2) {
			return false;
		}
		version = infoList[1];
		devType = infoList[0];
		return true;
	}
	return false;
}