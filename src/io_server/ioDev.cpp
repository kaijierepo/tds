#include "pch.h"
#include "ioDev.h"
#include "ioChan.h"
#include "commSrv.h"
#include "db.h"
#include "prj.h"


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
	bEnableAcq = true;
	m_mngStatus = DMS_UNCONF;
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
	conf["addr"] = m_addr;
	conf["type"] = m_devType;
	conf["level"] = m_level;
	if(m_devTypeLabel!= "")
		conf["type_label"] = m_devTypeLabel;
	json children;
	for (auto& i : m_vecChild)
	{
		json j;
		i->toJson(j, opt);
		children.push_back(j);
	}
	if(!children.empty())
		conf["children"] = children;
	return true;
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

vector<ioDev*> ioDev::getIODevices(string devType)
{
	vector<ioDev*> ary;
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* p = m_vecChild[i];
		if (p->m_devType == devType)
		{
			ary.push_back(p);
		}

		vector<ioDev*> aryChild = p->getIODevices(devType);
		ary.insert(ary.end(), aryChild.begin(), aryChild.end());
	}
	return ary;
}


ioAddress ioDev::getIOAddr()
{
	ioAddress addr;
	addr.devAddr = m_addr;
	if (m_pParent)
		addr.gwAddr = m_pParent->m_addr;

	 if (m_devType == "modbus_rtu")
	{
		addr.proto = APP_LAYER_PROTO_TYPE::PROTOCOL_MODBUS_RTU;
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
		if(i.second->m_addr == strChanID) return i.second;
	}

	for (auto i : m_mapBatchDataLink)
	{
		string s = i.first;
		s = str::trim(s, "#");
		if (strChanID.find(s) == 0)
		{
			string wildCardVal = strChanID.substr(s.length(), strChanID.length() - s.length());
			ioChannel* p = new ioChannel();
			p->m_addr = strChanID;
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

ioDev* ioDev::getChild(ioAddress& iopath)
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* p = m_vecChild.at(i);
		if (p->getIOAddr() == iopath)
			return p;
	}
	return NULL;
}

ioDev* ioDev::getChild(string addr)
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		ioDev* p = m_vecChild.at(i);
		if (p->m_addr == addr)
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
	if (str::isIp(m_addr))
		return true;

	return false;
}

string ioDev::GetCommIP()
{
	if (m_addr.find('.') !=  string::npos || m_addr.find("COM") != string::npos) //如果自己配置了IP，那么该ip为该设备的ip或者是该设备网关的ip
	{
		return m_addr;
	}
	else //没有配置ip，使用父网关采集设备的ip
	{
		if (m_pParent && m_pParent->IsGateway())
		{
			return m_pParent->m_addr;
		}
	}

	return "";
}

void ioDev::SendToChild(SYSTEMTIME dataTime, char* pData, int iLen, string strID)
{
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		if (m_vecChild.at(i)->m_addr == strID)
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


