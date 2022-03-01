#include "pch.h"
#include "ioDev.h"
#include "ioChan.h"
#include "commSrv.h"
#include "db.h"
#include "prj.h"
#include "ioGW_tuyaProject.h"
#include "ioDev_modbusSlave.h"
#include "ioDev_mqttBroker.h"
#include "ioDev_tuya.h"
#include "ioGW_rs485.h"
#include "ioChan.h"
#include "ioGW_localSerial.h"
#include "ioDev_tdsp.h"
#include "ioDev_genicam.h"
#include "mp.h"

#include "logger.h"
#include "ioSrv.h"

vector<std::shared_ptr<TDS_SESSION>> commpktSessions;
void sendToCommLog(string s)
{
	for (int i = 0; i < commpktSessions.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = commpktSessions[i];
		if (!session->isConnected())
		{
			commpktSessions.erase(commpktSessions.begin() + i);
			i--;
			continue;
		}


		session->send((char*)s.c_str(), s.length());
	}
}


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
ioDev* createIODev(json conf)
{
    ioDev* p = NULL;
	string type = conf["type"].get<string>();
	if (mapDevCreateFunc.find(type) != mapDevCreateFunc.end())
	{
		fp_createDev func_create = mapDevCreateFunc[type];
		p = func_create();
	}
	else if (type == "mqtt-broker")
	{
		p = new ioDev_mqttBroker();
	}
	else if (type == "tuya-iot-project")
	{
		p = new ioGW_tuyaProject();
	}
	else if (type == "tuya.switch")
	{
		p = new ioDev_tuya();
	}
	else if (type == IO_DEV_TYPE::GW::rs485_gateway)
	{
		p = new ioGW_rs485();
	}
	else if (type == IO_DEV_TYPE::DEV::modbus_rtu_slave)
	{
		p = new ioDev_ModbusSlave();
	}
	else if (type == IO_DEV_TYPE::DEV::tdsp_device)
	{
		p = new ioDev_tdsp();;
	}
	else if (type == IO_DEV_TYPE::GW::local_serial)
	{
		p = new ioGW_LocalSerial();
	}
	else if (type == "genicam")
	{
#ifdef ENABLE_GENICAM
		p = new ioDev_genicam();
#endif
	}

	if (p)
	{
		p->m_confNodeId = common::guid();
		if (conf.contains("addr"))
		{
			p->m_jDevAddr = conf["addr"];
			p->m_devAddr = p->getDevAddrStr();
		}
	}

	return p;
}

ioDev* createIODev(string type)
{
	json j;
	j["type"] = type;
	return createIODev(j);
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
	m_bEnableIoLog = true;
	bEnableAcq = true;
	m_bRunning = true; //是否启动了自动工作 （采集线程是否启动）
	bEnableAcq = true;
	m_dispositionMode = DEV_DISPOSITION_MODE::managed;
	m_pCommAddrInfo = NULL;
	m_pParent = NULL;
	m_bOnline = false;
	m_iSendDataFailCount = 0;
	memset(&m_stLastHeartbeatTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stLastSetClockTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stEqpOnLineDateTime, 0, sizeof(SYSTEMTIME));
	timeopt::setAsTimeOrg(m_stLastChanDataTime);
	timeopt::setAsTimeOrg(m_stLastAcqTime);
	timeopt::setAsTimeOrg(m_stLastAlarmStatusTime);
	GetLocalTime(&m_stEqpOffLineDateTime);
	GetLocalTime(&m_stLastActiveTime);
	m_pMO = NULL;
	m_pRecvCallback = NULL;
	m_pCallbackUser = NULL;
	pTdsSession = NULL;
	m_fAcqInterval = 30;
	pIOSession = NULL;
}

ioDev::~ioDev(void)
{
}

void ioDev::stop()
{
	m_bRunning = false;
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
// opt.recursive = true
// opt.onlyConf = false
bool ioDev::toJson(json& conf, json opt)
{
	//配置数据
	conf["addrMode"] = m_addrMode;
	conf["addr"] = m_jDevAddr;
	conf["type"] = m_devType;
	conf["typeLabel"] = m_devTypeLabel;
	conf["level"] = m_level;
	conf["manageStatus"] = m_dispositionMode;
	if (m_fAcqInterval != 0)
		conf["acqInterval"] = m_fAcqInterval;
	conf["enableAcq"] = bEnableAcq;
	if (m_strTagBind != "")
		conf["tagBind"] = m_strTagBind;
	if (m_strChanTemplate != "")
		conf["chanTemplate"] = m_strChanTemplate;
	conf["nodeID"] = m_confNodeId;

	
	
	if (opt.contains("onlyConf") && opt["onlyConf"].get<bool>() == true)
	{
		
	}
	else
	{
		//运行时数据
		conf["online"] = m_bOnline;
		conf["connected"] = m_bConnected;
		if (pIOSession != nullptr)
		{
			conf["remoteIP"] = pIOSession->getRemoteAddr();
		}
		conf["ioAddr"] = getIOAddrStr();

		//详细信息
		conf["parentType"] = m_parentDevType;
	}


	//显式指定不递归才不递归
	if (opt != nullptr && opt["recursive"] != nullptr && opt["recursive"].get<bool>() == false)
	{

	}
	else//默认递归
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

	if (m_channels.size() > 0)
	{
		json channels = json::array();
		for (auto& i : m_channels)
		{
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
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		p->getChanStatus(statusList);
	}

	return true;
}

bool ioDev::loadConf(json& conf)
{
	if (conf.contains("addrMode"))
	{
		m_addrMode = conf["addrMode"].get<string>();
	}

	if (conf.contains("addr"))
	{
		m_jDevAddr = conf["addr"];
	}

	if (conf["acqInterval"] != nullptr)
	{
		m_fAcqInterval = conf["acqInterval"].get<float>();
	}

	if (conf["enableAcq"] != nullptr)
	{
		bEnableAcq = conf["enableAcq"].get<bool>();
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
		if (conf["tagBind"].is_array())
		{
			json tagNodes = conf["tagBind"];
			string tag;
			for (int i = 0; i < tagNodes.size(); i++)
			{
				tag += tagNodes[i];
				if (i < tagNodes.size() - 1)
					tag += ".";
			}
			m_strTagBind = tag;
		}
		else
		{
			m_strTagBind = conf["tagBind"];
		}

		m_strTagBind = str::trimPrefix(m_strTagBind, prj.m_strName + ".");

		//启用设备，才更新绑定的mo中的 关联io地址信息。 备用的不更新。
		//否则备用的绑定地址和启用的相同时，可能会错误的使用备用设备的信息
		if (m_dispositionMode == DEV_DISPOSITION_MODE::managed)
		{
			MO* pmo = prj.GetMOByTag(m_strTagBind);
			if (pmo)
			{
				if (pmo->m_moType == MO_TYPE::mp && this->m_level == IO_DEV_LEVEL::channel)
				{
					MP* pmp = (MP*)pmo;
					ioChannel* pChan = (ioChannel*)this;
					pmp->m_ioType = pChan->m_ioType;
					pmp->m_ioTypeLabel = pChan->m_ioTypeLabel;
				}
				pmo->m_strIoAddrBind = getIOAddrStr();
			}
		}
	}

	if (conf["children"] != nullptr)
	{
		deleteChildren();
		json childDev = conf["children"];

		for (auto i : childDev)
		{
			ioDev* pChild = nullptr;
			if (i["level"] == "channel")
			{
				ioChannel* pdc = nullptr;
				pdc = new ioChannel();
				pdc->m_jDevAddr = i["addr"];
				if (i["addr"].is_string())
					pdc->m_devAddr = i["addr"].get<string>();
				pChild = pdc;
				pdc->loadConf(i);
				//批量映射配置.主要用于mqtt的场景，当mqtt的路径结构和MOTree的树结构一致时
				if (pdc->m_devAddr != "" && isBatchLink(pdc->m_devAddr)) //datachannel instance of the batch data link will be created dynamicly when the channel data is received
				{
					m_mapBatchDataLink[pdc->m_devAddr] = i["tagBind"];
				}
			}
			else if (i["level"] == "device")
			{
				pChild = createIODev(i);
				pChild->loadConf(i);
			}

			if (pChild)
			{
				addChild(pChild);
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
		}
	}
		
	return true;
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
	std::shared_lock<shared_mutex> lock(m_csThis); //读锁
	for (int i = 0; i < m_vecChildDev.size(); i++)
	{
		ioDev* p = m_vecChildDev[i];
		if (p->m_confNodeId == nodeID)
		{
			delete p;
			m_vecChildDev.erase(m_vecChildDev.begin() + i);
			return true;
		}

		if (p->deleteIODevByNodeID(nodeID))
			return true;
	}
	return false;
}

ioDev* ioDev::getIODev(string ioAddr)
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
			if (it->getDevAddrStr() == nodeName)
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

bool ioDev::IsConnected()
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

bool ioDev::CmdRequestSync(char* pReqData, int iReqLen, char* pRespData, int& iRespLen)
{
	PKT_DATA req(pReqData,iReqLen), resp;

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

ioChannel* ioDev::getChan(string addr)
{
	for (auto i : m_mapDataChannel)
	{
		if(i.second->m_devAddr == addr) return i.second;
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
			str::replace(bindTag, "*", wildCardVal);
			str::replace(bindTag, "/", ".");
			p->m_strTagBind = bindTag;
			m_mapDataChannel[addr] = p;
			return p;
		}
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

bool ioDev::addChannel(ioChannel* p)
{
	m_csThis.lock();
	p->m_pParent = this;
	m_channels.push_back(p);
	m_mapDataChannel[p->m_devAddr] = p;
	m_csThis.unlock();
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
		if (p->m_devAddr == devAddr)
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

bool ioDev::NotNeedGateway()
{
	if (str::isIp(m_devAddr))
		return true;

	return false;
}

void ioDev::setRecvCallback(void* pUser, fp_ioAddrRecvCallback callback)
{
	LOG("ioAddr=" + getIOAddrStr() + ",设置接收回调" + str::fromInt((DWORD)pUser));
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
		string strMP = pC->m_strTagBind;
		str::trimPrefix(strMP, prj.m_strName + ".");
		if (strMP == tag)
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

void ioDev::setIOSession(shared_ptr<TDS_SESSION> ioSession)
{
	std::unique_lock<mutex> lock(m_csIOSession);

	if (pIOSession != ioSession && ioSession != nullptr && pIOSession!= nullptr)
	{
		string ioAddr = getIOAddrStr();
		string devInfo = "ioAddr=" + getIOAddrStr() + ",tag=" + m_strTagBind;
		LOG("[warn][ioDev]老连接未断开，设备在新连接上线。设备:" + devInfo + ",老连接:" + pIOSession->getRemoteAddr() + ",新连接:" + ioSession->getRemoteAddr());
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
	j["data"] = str::fromBytes(recvData, len);
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
	j["data"] = str::fromBytes(sendData, len);
	string s = j.dump();

	sendToCommLog(s);
}
