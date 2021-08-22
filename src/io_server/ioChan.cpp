#include "pch.h"
#include "ioChan.h"
#include "prj.h"
#include "ioDev.h"
#include "mp.h"
#include "db.h"


ioChannel::ioChannel()
{

}


ioChannel::~ioChannel()
{
}

string ioChannel::GetCommLinkTag()
{
	ioAddress addr = m_pParent->getIOAddr();
	string str = addr.ToString();

	str += "-" + m_addr;

	return str;
}

bool ioChannel::match(string channelNo) {
	if (m_addr.find("#"))//mqtt channel wildcard
	{
		string str = m_addr;
		str = str::trim(str, "#");
		if (channelNo.find(str) == 0)
		{
			return true;
		}
	}
	else
	{
		if (channelNo == m_addr)
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

bool ioChannel::output(json jVal)
{
	return false;
}

bool ioChannel::IsValid()
{
	MP* pMP = (MP*)prj.GetMOByTag(m_strLinkMPTag);
	if (pMP && pMP->m_moType == MO_TYPE::mp)
		return true;
	else
		return false;
}



