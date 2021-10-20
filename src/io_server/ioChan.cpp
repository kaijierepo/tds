#include "pch.h"
#include "ioChan.h"
#include "prj.h"
#include "ioDev.h"
#include "mp.h"
#include "db.h"
#include "ioDev_iq60.h"


ioChannel::ioChannel()
{

}


ioChannel::~ioChannel()
{
}

bool ioChannel::loadConf(json& conf)
{
	m_level = "channel";
	m_devAddr = conf["addr"];
	if (conf["tag_bind"] != nullptr)
	{
		if (conf["tag_bind"].is_array())
		{
			json tagNodes = conf["tag_bind"];
			string tag;
			for (int i = 0; i < tagNodes.size(); i++)
			{
				tag += tagNodes[i];
				if (i < tagNodes.size() - 1)
					tag += ".";
			}
			m_strLinkMPTag = tag;
		}
		else
		{
			m_strLinkMPTag = conf["tag_bind"];
		}
	}
	
	if (conf["io"] != nullptr)
		m_io = conf["io"];
	if (conf["ioLabel"] != nullptr)
		m_ioLabel = conf["ioLabel"];
	if (conf["valType"] != nullptr)
		m_valType = conf["valType"];
	if (conf["valTypeLabel"] != nullptr)
		m_valTypeLabel = conf["valTypeLabel"];
	if (conf["name"] != nullptr)
		m_name = conf["name"];

	return true;
}

string ioChannel::GetCommLinkTag()
{
	ioAddress addr = m_pParent->getIOAddr();
	string str = addr.ToString();

	str += "-" + m_devAddr;

	return str;
}

bool ioChannel::match(string channelNo) {
	if (m_devAddr.find("#"))//mqtt channel wildcard
	{
		string str = m_devAddr;
		str = str::trim(str, "#");
		if (channelNo.find(str) == 0)
		{
			return true;
		}
	}
	else
	{
		if (channelNo == m_devAddr)
		{
			return true;
		}
	}
	return false;
}

void ioChannel::input(json jVal, SYSTEMTIME* dataTime, bool bPic) {
	SYSTEMTIME t;
	if (dataTime == NULL)
	{
		GetLocalTime(&t);
		dataTime = &t;
	}
	m_stLastUpdateTime = *dataTime;
	string tag = m_strLinkMPTag;
	str::trimPrefix(tag, prj.m_strName + ".");
	MP* pMP = (MP*)prj.GetMOByTag(tag);
	if (pMP && pMP->m_moType == MO_TYPE::mp)
	{
		pMP->input(jVal,dataTime);
	}
}

bool ioChannel::output(json jVal, json& jResp)
{
	ioDev* pDev = ioDev::m_pParent;

	if (pDev->m_devType == IO_DEV_TYPE::DEV::iq60_gateway)
	{
		ioDev_iq60* p = (ioDev_iq60*)pDev;
		if (p)
			p->writeChannel(m_devAddr,jVal, jResp);
	}


	return true;
}

bool ioChannel::IsValid()
{
	MP* pMP = (MP*)prj.GetMOByTag(m_strLinkMPTag);
	if (pMP && pMP->m_moType == MO_TYPE::mp)
		return true;
	else
		return false;
}
