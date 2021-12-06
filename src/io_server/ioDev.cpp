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
#include "ioDev_iq60.h"
#include "ioDev_tdsp.h"

#include "ioDev_genicam.h"
#include "mp.h"

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

ioDev* createIODev(json conf)
{
	ioDev* p = NULL;
	if (conf["type"] == "mqtt-broker")
	{
		p = new ioDev_mqttBroker();
		string ip = conf["addr"]["ip"];
		string port = conf["addr"]["port"];
		p->m_devAddr = ip + ":" + port;
	}
	else if (conf["type"] == "tuya-iot-project")
	{
		p = new ioGW_tuyaProject();
		p->m_devAddr = conf["addr"]["client_id"];
		p->m_secret = conf["addr"]["secret"];
	}
	else if (conf["type"] == "tuya.switch")
	{
		p = new ioDev_tuya();
		p->m_devAddr = conf["addr"]["device_id"];
	}
	else if (conf["type"] == "iq60-gateway")
	{
		ioDev_iq60* piq60 = new ioDev_iq60();
		p = piq60;
		p->m_devAddr = conf["addr"]["id"];
	}
	else if (conf["type"] == IO_DEV_TYPE::GW::rs485_gateway)
	{
		ioGW_rs485* pRs485 = new ioGW_rs485();
		p = pRs485;
		if (conf["activeMode"] != nullptr && conf["activeMode"].get<bool>() == true)
		{
			p->m_devAddr = conf["addr"]["ip"].get<string>() + ":" + str::fromInt(conf["addr"]["port"].get<int>());
		}
		else
		{
			p->m_devAddr = conf["addr"]["ip"].get<string>();
		}
	}
	else if (conf["type"] == IO_DEV_TYPE::DEV::modbus_rtu_slave)
	{
		ioDev_ModbusSlave* pRtuSlave = new ioDev_ModbusSlave();
		p = pRtuSlave;
		p->m_devAddr = conf["addr"].get<string>();
	}
	else if (conf["type"] == IO_DEV_TYPE::DEV::tdsp_device)
	{
		ioDev_tdsp* ptdsp = new ioDev_tdsp();
		p = ptdsp;
		p->m_jDevAddr = conf["addr"];
	}
	if (p)
	{
		p->m_devType = conf["type"];
		p->m_level = conf["level"];
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
	m_bEnableIoLog = true;
	bEnableAcq = true;
	m_bRunning = true; //是否启动了自动工作 （采集线程是否启动）
	bEnableAcq = true;
	m_mngStatus = IODEV_MNG_STATUS::managed;
	m_pCommAddrInfo = NULL;
	m_pParent = NULL;
	m_bOnline = false;
	m_iSendDataFailCount = 0;
	memset(&m_stLastHeartbeatTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stLastSetClockTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stEqpOnLineDateTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stLastAcqTime, 0, sizeof(SYSTEMTIME));
	GetLocalTime(&m_stEqpOffLineDateTime);
	m_pMO = NULL;
	m_pRecvCallback = NULL;
	m_pCallbackUser = NULL;
	pTdsSession = NULL;
	m_fAcqInterval = 1;
	pIOSession = NULL;
}

ioDev::~ioDev(void)
{
}

void ioDev::stop()
{
	m_bRunning = false;
	for (auto i : m_vecChild)
	{
		i->stop();
	}
	if (m_bWorkingThreadRunning)
	{
		m_signalWorkThreadExit.wait();
	}
}

bool ioDev::toJson(json& conf, string opt)
{
	conf["ioAddr"] = getIOAddrStr();
	conf["addr"] = m_jDevAddr;
	conf["type"] = m_devType;
	conf["typeLabel"] = m_devTypeLabel;
	conf["level"] = m_level;
	conf["parentType"] = m_parentDevType;
	conf["online"] = m_bOnline;
	conf["connected"] = m_bConnected;
	conf["manageStatus"] = m_mngStatus;
	if (m_fAcqInterval != 0)
		conf["acqInterval"] = m_fAcqInterval;
	conf["enableAcq"] = bEnableAcq;

	if (m_strTagBind != "")
		conf["tagBind"] = m_strTagBind;

	json children = json::array();
	for (auto& i : m_vecChild)
	{
		json j;
		i->toJson(j, opt);
		children.push_back(j);
	}
	conf["children"] = children;
	return true;
}

bool ioDev::getStatus(json& status, string opt)
{
	return false;
}


bool ioDev::getChanStatus(json& statusList)
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* p = m_vecChild[i];
		p->getChanStatus(statusList);
	}

	return true;
}

bool ioDev::loadConf(json& conf)
{
	m_devTypeLabel = conf["typeLabel"].get<string>();
	m_jDevAddr = conf["addr"];
	if (conf["acqInterval"] != nullptr)
	{
		m_fAcqInterval = conf["acqInterval"].get<float>();
	}

	if (conf["enableAcq"] != nullptr)
	{
		bEnableAcq = conf["enableAcq"].get<bool>();
	}

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


	if (conf["children"] != nullptr)
	{
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
				if (pdc->m_devAddr!="" && isBatchLink(pdc->m_devAddr)) //datachannel instance of the batch data link will be created dynamicly when the channel data is received
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


ioDev* ioDev::getIODev(string ioAddr)
{
	for (auto& it : m_vecChild)
	{
		if (it->getIOAddrStr() == ioAddr)
		{
			return it;
		}

		ioDev* p = it->getIODev(ioAddr);
		if (p)
			return p;
	}

	return nullptr;
}

ioDev* ioDev::getIODev(json& ioAddr)
{
	for (auto& it : m_vecChild)
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
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* p = m_vecChild[i];
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
	while (pParent)
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
	else {
		devAddr = m_jDevAddr.get<string>();
	}
	
	return devAddr;
}

void ioDev::CommLock()
{
	
}

void ioDev::CommUnlock()
{
	
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

int ioDev::CalcTimePassSecond(SYSTEMTIME* stLast)
{
	SYSTEMTIME stNow;
	GetLocalTime(&stNow);
	DWORD l = timeopt::SysTime2Unix(*stLast);
	DWORD n = timeopt::SysTime2Unix(stNow);

	return (n - l) / 1000;
}

void ioDev::DoCycleTask()
{
	PKT_DATA req, resp;
	if (CalcTimePassSecond(&m_stLastHeartbeatTime) > ioDev::m_heartBeatInterval&& ioDev::m_heartBeatInterval > 0)
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

bool ioDev::addChild(ioDev* p)
{
	m_vecChild.push_back(p);
	p->m_pParent = this;
	if (p->m_level == "channel")
	{
		m_mapDataChannel[p->m_devAddr] = (ioChannel*)p;
	}
	return true;
}

void ioDev::deleteChild(ioDev* p)
{
	for (int i=0;i<m_vecChild.size();i++)
	{
		ioDev* pTemp = m_vecChild.at(i);
		if (pTemp == p)
		{
			m_vecChild.erase(m_vecChild.begin() + i);
			break;
		}
	}
}

void ioDev::deleteDescendant(ioDev* p)
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* pTemp = m_vecChild.at(i);
		pTemp->deleteDescendant(p);
		if (pTemp == p)
		{
			m_vecChild.erase(m_vecChild.begin() + i);
			break;
		}
	}
}


ioDev* ioDev::getChild(string devAddr)
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* p = m_vecChild.at(i);
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
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		if (m_vecChild.at(i)->m_devAddr == strID)
		{
			m_vecChild.at(i)->OnRecvData(dataTime, pData, iLen);
		}
	}
}

ioChannel* ioDev::getChanByTag(string tag)
{
	for (auto& child : m_vecChild)
	{
		if (child->m_level == "channel")
		{
			ioChannel* pC = (ioChannel*)child;
			string strMP = pC->m_strTagBind;
			str::trimPrefix(strMP, prj.m_strName + ".");
			if (strMP == tag)
			{
				return pC;
			}
			continue;
		}

		ioChannel* pC = child->getChanByTag(tag);
		if (pC)
			return pC;
	}

	return nullptr;
}

void ioDev::setIOSession(shared_ptr<TDS_SESSION> ioSession)
{
	std::unique_lock<mutex> lock(m_csIOSession);
	pIOSession = ioSession;
}


CCanTransparentGateway::CCanTransparentGateway()
{
	m_devType = "can_gateway";
}


void ioDev::statisOnRecv(char* recvData, int len, string addr)
{
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
