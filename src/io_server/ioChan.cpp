#include "pch.h"
#include "ioChan.h"
#include "ioDev.h"


ioChannel::ioChannel()
{
	m_level = "channel";
	m_ioType = CHAN_IO_TYPE::I;
}


ioChannel::~ioChannel()
{
}


bool ioChannel::loadConf(json& conf)
{
	m_level = "channel";


	if (conf["addr"] != nullptr && conf["addr"].is_object())
	{
		if (conf["addr"]["regType"] != nullptr)
			m_regType = conf["addr"]["regType"].get<string>();
		if (conf["addr"]["regOffset"] != nullptr)
			m_regOffset = conf["addr"]["regOffset"].get<int>();
		if (conf["addr"]["storageFmt"] != nullptr)
			m_storageFmt = conf["addr"]["storageFmt"].get<string>();
	}

	
	if (conf["ioType"] != nullptr)
		m_ioType = conf["ioType"];

	if(m_ioType!="")
		m_ioTypeLabel = getIOTypeLabel(m_ioType);

	if (conf["valType"] != nullptr)
		m_valType = conf["valType"];
	if (m_valType != "")
		m_valTypeLabel = getValTypeLabel(m_valType);

	if (conf["name"] != nullptr)
		m_name = conf["name"];

	//先加载ioType. 在ioDev::loadConf中需要赋值给绑定的位号
	ioDev::loadConf(conf);
	
	return true;
}

bool ioChannel::toJson(json& conf, json opt)
{
	DEV_QUERIER querier = parseQueryOpt(opt);

	conf["addr"] = m_jDevAddr;

	if (querier.getConf) {
		conf["nodeID"] = m_confNodeId;
		conf["tagBind"] = m_strTagBind;
		conf["ioType"] = m_ioType;
		conf["valType"] = m_valType;
		conf["name"] = m_name;

		//optional fields
		if (m_storageFmt != "")
			conf["storageFmt"] = m_storageFmt;

		if (m_channelType != "")
		{
			conf["channelType"] = m_channelType;
			conf["channelTypeLabel"] = m_channelTypeLabel;
		}
	}


	if(querier.getStatus)
	{
		conf["ioTypeLabel"] = m_ioTypeLabel;
		conf["valTypeLabel"] = m_valTypeLabel;
		conf["val"] = m_curVal;
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

bool ioChannel::getChanVal(json& valList)
{
	json j;
	string tag = m_strTagBind;
	if (tag != "" && timeopt::isValidTime(m_stLastUpdateTime)) {
		if (m_pParent->m_strTagBind != "") {
			tag = m_pParent->m_strTagBind + "." + tag;
		}
		j["time"] = timeopt::st2str(m_stLastUpdateTime);
		j["tag"] = tag;
		j["val"] = m_curVal;
		valList.push_back(j);
	}

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

void ioChannel::input(json jVal, TIME* dataTime, bool bPic) {
	string tagBind;
	input(jVal, tagBind, dataTime, bPic);

	//更新绑定位号值
	json param;
	param["tag"] = tagBind;
	param["val"] = m_curVal;
	param["time"] = timeopt::st2str(m_stLastUpdateTime);
	tds->callAsyn("input", param.dump());
}

void ioChannel::input(json jVal, string& tagBind, TIME* dataTime, bool bPic)
{
	//更新通道值
	TIME t;
	if (dataTime == NULL)
	{
		timeopt::now(&t);
		dataTime = &t;
	}
	m_stLastUpdateTime = *dataTime;
	m_curVal = jVal;

	//获得绑定的位号。如果父节点有关联位号。并且位号没有包含父节点位号，拼接父节点位号
	tagBind = m_strTagBind;
	if (m_pParent->m_strTagBind != "") {
		if (tagBind.find(m_pParent->m_strTagBind) == string::npos) {
			tagBind = m_pParent->m_strTagBind + "." + tagBind;
		}
	}
}


//ioChannel的输出统一由父设备实现，因为通道的特性是由设备决定的，什么设备决定了有什么通道
//例如Modbus设备就有以寄存器为特征的通道
void ioChannel::output(json jVal, json& rlt,json& err, bool sync)
{
	ioDev* pDev = ioDev::m_pParent;
	pDev->output(getDevAddrStr(),jVal, rlt,err,sync);
}
