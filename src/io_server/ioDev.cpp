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

#include "ioDev_genicam.h"


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
		p->m_devAddr = conf["addr"]["gateway_id"];
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
int ioDev::m_heartBeatInterval = 10;



ioDev::ioDev(void)
{
	m_bEnableIoLog = true;
	bEnableAcq = true;
	m_mngStatus = IODEV_MNG_STATUS::managed;
	m_pCommAddrInfo = NULL;
	m_pParent = NULL;
	m_bOnline = false;
	m_iSendDataFailCount = 0;
	memset(&m_stLastHeartbeatTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stLastSetClockTime, 0, sizeof(SYSTEMTIME));
	memset(&m_stEqpOnLineDateTime, 0, sizeof(SYSTEMTIME));
	GetLocalTime(&m_stEqpOffLineDateTime);
	m_pMO = NULL;
	m_pRecvCallback = NULL;
	m_pCallbackUser = NULL;
	pTdsSession = NULL;
}

ioDev::~ioDev(void)
{

}

bool ioDev::toJson(json& conf, string opt)
{
	//conf["name"] = "IQ60";
	//conf["ioAddr"] = getIOAddr().ToString();
	conf["addr"] = m_jDevAddr;
	conf["type"] = m_devType;
	conf["typeLabel"] = m_devTypeLabel;
	conf["level"] = m_level;
	conf["parentType"] = m_parentDevType;
	conf["online"] = m_bOnline;
	conf["manageStatus"] = m_mngStatus;

	if (m_level == "channel")
	{
		ioChannel* pC = (ioChannel*)this;
		conf["tag_bind"] = pC->m_strLinkMPTag;
		conf["io"] = pC->m_io;
		conf["ioLabel"] = pC->m_ioLabel;
		conf["valType"] = pC->m_valType;
		conf["valTypeLabel"] = pC->m_valTypeLabel;
		conf["name"] = pC->m_name;
	}
		

	if (m_channelType != "")
	{
		conf["channelType"] = m_channelType;
		conf["channelTypeLabel"] = m_channelTypeLabel;
	}


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

bool ioDev::loadConf(json& conf)
{
	m_devTypeLabel = conf["typeLabel"].get<string>();
	m_jDevAddr = conf["addr"];
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
					m_mapBatchDataLink[pdc->m_devAddr] = i["tag_bind"];
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

ioDev* ioDev::getIODev(ioAddress iopath)
{
	for (auto& it : m_vecChild)
	{
		if (it->getIOAddr() == iopath)
		{
			return it;
		}

		ioDev* p = it->getIODev(iopath);
		if (p)
			return p;
	}

	return nullptr;
}

ioDev* ioDev::getIODev(string ioAddr)
{
	ioAddress a;
	a.FromString(ioAddr);
	return getIODev(a);
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

ioAddress ioDev::getIOAddr()
{
	ioAddress addr;
	if (m_level == "channel")
	{
		//addr.chanAddr = m_addr;
		//addr.devAddr = m_pParent->m_addr;
		//if(m_pParent->)
	}
	addr.devAddr = m_devAddr;
	if (m_pParent)
		addr.gwAddr = m_pParent->m_devAddr;

	 if (m_devType == "modbus_rtu")
	{
		addr.proto = APP_LAYER_PROTO::MODBUS_RTU;
	}
	
	addr.gwType = GW_UNKNOWN;
	if (m_pParent)
	{
		if (m_pParent->m_devType == "can_gateway")
		{
			addr.gwType = GW_CAN_TRANSPARENT;
		}
		else if(m_pParent->m_devType == "modbus_gateway")
		{
			addr.gwType = GW_TRANSPARENT;
		}
	}

	return addr;
}

string ioDev::getIOAddrStr()
{
	string ioAddrStr = m_devAddr;
	ioDev* pParent = m_pParent;
	while (pParent)
	{
		ioAddrStr = pParent->m_devAddr + "/" + ioAddrStr;
		pParent = pParent->m_pParent;
	}
		
	return ioAddrStr;
}

void ioDev::CommLock()
{
	commSrv.CommLock(getIOAddr());
}

void ioDev::CommUnlock()
{
	commSrv.CommUnlock(getIOAddr());
}

bool ioDev::SendPkt(PKT_DATA& pkt)
{
	return sendData((char*)pkt.m_DataBuf, pkt.m_iDataBufLen);
}

bool ioDev::sendData(char* pData, int iLen)
{
	ioAddress addr = getIOAddr();
	return commSrv.SendData(pData, iLen, addr);
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
	return commSrv.IsAddrConnected(getIOAddr());
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

	memcpy(pRespData, resp.m_DataBuf, resp.m_iDataBufLen);
	iRespLen = resp.m_iDataBufLen;
	return true;
}

bool ioDev::CmdRequestSync(PKT_DATA& req, PKT_DATA& resp, int iRetryCount, string strLogMsgWhenSend)
{
	REQ_PARAM reqParam;
	if (iRetryCount > 0)
		reqParam.iRetryCount = iRetryCount;

	bool bRet = commSrv.RequestAndWaitResponse(&req, &resp, getIOAddr(), &reqParam);

	if (bRet)
		resp.UnPack();

	return bRet;
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

ioChannel* ioDev::GetDataChannel(string strChanID)
{
	for (auto i : m_mapDataChannel)
	{
		if(i.second->m_devAddr == strChanID) return i.second;
	}

	for (auto i : m_mapBatchDataLink)
	{
		string s = i.first;
		s = str::trim(s, "#");
		if (strChanID.find(s) == 0)
		{
			string wildCardVal = strChanID.substr(s.length(), strChanID.length() - s.length());
			ioChannel* p = new ioChannel();
			p->m_devAddr = strChanID;
			string bindTag = i.second;
			str::replace(bindTag, "*", wildCardVal);
			str::replace(bindTag, "/", ".");
			p->m_strLinkMPTag = bindTag;
			m_mapDataChannel[strChanID] = p;
			return p;
		}
	}
	return NULL;
}

ioChannel* ioDev::GetDataChannelByMPTag(string strMPTag)
{
	for (auto it : m_mapDataChannel)
	{
		if(it.second->m_strLinkMPTag == strMPTag) return it.second;
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
	//除了当前正在同步请求的命令，其他都做异步处理
	if (m_pCommAddrInfo->strInSyncCmdID == pd->GetCmdID()) //这条命令正在进行同步通讯，不能异步处理
	{
		return false;
	}

	if (m_pCommAddrInfo->strInSyncCmdID == "*")
	{
		return false;
	}

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

ioChannel* ioDev::getIOChan(string tag)
{
	for (auto& child : m_vecChild)
	{
		if (child->m_level == "channel")
		{
			ioChannel* pC = (ioChannel*)child;
			string strMP = pC->m_strLinkMPTag;
			str::trimPrefix(strMP, prj.m_strName + ".");
			if (strMP == tag)
			{
				return pC;
			}
			continue;
		}

		ioChannel* pC = child->getIOChan(tag);
		if (pC)
			return pC;
	}

	return nullptr;
}


CCanTransparentGateway::CCanTransparentGateway()
{
	m_devType = "can_gateway";
}
