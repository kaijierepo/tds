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
	if (conf["storageFmt"] != nullptr)
	{
		m_storageFmt = conf["storageFmt"].get<string>();
	}
	if (conf["ioType"] != nullptr)
		m_ioType = conf["ioType"];

	m_ioTypeLabel = IO_TYPE_LABEL.at(m_ioType);
	if (conf["valType"] != nullptr)
		m_valType = conf["valType"];
	if (conf["valTypeLabel"] != nullptr)
		m_valTypeLabel = VAL_TYPE_LABEL.at(m_valType);
	if (conf["name"] != nullptr)
		m_name = conf["name"];

	//先加载ioType. 在ioDev::loadConf中需要赋值给绑定的位号
	ioDev::loadConf(conf);
	if (conf["addr"] != nullptr && conf["addr"].is_object())
	{
		if (conf["addr"]["regType"] != nullptr)
			m_regType = conf["addr"]["regType"].get<string>();
		if (conf["addr"]["regAddr"] != nullptr)
			m_devAddr = str::fromInt(conf["addr"]["regAddr"].get<int>());
	}
	return true;
}

bool ioChannel::toJson(json& conf, string opt)
{
	ioDev::toJson(conf, opt);

	conf["storageFmt"] = m_storageFmt;

	conf["tagBind"] = m_strTagBind;
	conf["ioType"] = m_ioType;
	conf["ioTypeLabel"] = m_ioTypeLabel;
	conf["valType"] = m_valType;
	conf["valTypeLabel"] = m_valTypeLabel;
	conf["name"] = m_name;

	if (m_channelType != "")
	{
		conf["channelType"] = m_channelType;
		conf["channelTypeLabel"] = m_channelTypeLabel;
	}

	return false;
}

bool ioChannel::getChanStatus(json& statusList)
{
	json j;
	toJson(j);
	j["val"] = m_curVal;
	if (timeopt::isValidTime(m_stLastUpdateTime))
	{
		j["time"] = timeopt::st2str(m_stLastUpdateTime);
	}
	else
		j["time"] = "?";
	
	statusList.push_back(j);
	return true;
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
	//更新通道值
	SYSTEMTIME t;
	if (dataTime == NULL)
	{
		GetLocalTime(&t);
		dataTime = &t;
	}
	m_stLastUpdateTime = *dataTime;
	m_curVal = jVal;

	//更新绑定位号值
	string tag = TAG::trimRoot(m_strTagBind);
	MP* pMP = (MP*)prj.GetMOByTag(tag);
	if (pMP && pMP->m_moType == MO_TYPE::mp)
	{
		pMP->input(jVal,dataTime);
	}
}


//ioChannel的输出统一由父设备实现，因为通道的特性是由设备决定的，什么设备决定了有什么通道
//例如Modbus设备就有以寄存器为特征的通道
bool ioChannel::output(json jVal, json& jResp, bool sync)
{
	ioDev* pDev = ioDev::m_pParent;
	return pDev->output(m_devAddr,jVal, jResp,sync);
}

bool ioChannel::IsValid()
{
	MP* pMP = (MP*)prj.GetMOByTag(m_strTagBind);
	if (pMP && pMP->m_moType == MO_TYPE::mp)
		return true;
	else
		return false;
}
