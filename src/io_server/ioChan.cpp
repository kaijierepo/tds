#include "pch.h"
#include "ioChan.h"
#include "ioDev.h"


ioChannel::ioChannel()
{
	m_level = "channel";
	m_ioType = CHAN_IO_TYPE::I;
	m_k = 1;
	m_b = 0;
}


ioChannel::~ioChannel()
{
}

string ioChannel::storageFmt2valType(string fmt) {
	if (m_k < 1) {
		return VAL_TYPE::Float;
	}
	else {
		if (fmt.find("16") != string::npos)return VAL_TYPE::integer;
		else if (fmt.find("32") != string::npos)return VAL_TYPE::integer;
		else if (fmt.find("64") != string::npos)return VAL_TYPE::integer;
		else if (fmt.find("float") != string::npos || fmt.find("Float") != string::npos)return VAL_TYPE::Float;
		else if (fmt.find("Double") != string::npos || fmt.find("Double") != string::npos)return VAL_TYPE::Float;
		else {
			return "";
		}
	}
}

bool ioChannel::loadConf(json& conf)
{
	m_level = "channel";

	auto kv = conf.find("downSample");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bDownSample = item.get<bool>();
		}
	}

	kv = conf.find("downSampleInterval");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_number_integer()) {
			m_iDownSampleInterval = item.get<int>();
		}
	}

	if (conf["addr"] != nullptr && conf["addr"].is_object())
	{
		if (conf["addr"]["regType"] != nullptr)
			m_regType = conf["addr"]["regType"].get<string>();
		if (conf["addr"]["regOffset"] != nullptr)
			m_regOffset = conf["addr"]["regOffset"].get<int>();
	}


	if (conf["fmt"] != nullptr) {
		m_fmt = conf["fmt"].get<string>();
	}

	if (conf["byteOrder"] != nullptr) {
		m_byteOrder = conf["byteOrder"].get<string>();
	}
		
	
	if (conf["ioType"] != nullptr)
		m_ioType = conf["ioType"];

	if(m_ioType!="")
		m_ioTypeLabel = getIOTypeLabel(m_ioType);

	//先加载k再计算valType,转换函数内部会利用k设置来判断
	if (conf["k"] != nullptr)
		m_k = conf["k"].get<double>();
	m_valType = storageFmt2valType(m_fmt);

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

	json jDevAddr;
	if (m_jDevAddr.is_object()) {
		for (auto& i : m_jDevAddr.items()) {
			if (i.value() != nullptr) {
				jDevAddr[i.key()] = i.value();
			}
		}
	}
	else {
		jDevAddr = m_jDevAddr;
	}

	conf["addr"] = jDevAddr;

	if (querier.getConf) {
		conf["nodeID"] = m_confNodeId;
		conf["tagBind"] = m_strTagBind;
		conf["ioType"] = m_ioType;
		conf["valType"] = m_valType;
		conf["name"] = m_name;

		conf["k"] = m_k;

		//optional fields
		if (m_fmt != "")
			conf["fmt"] = m_fmt;

		if (m_byteOrder != "")
			conf["byteOrder"] = m_byteOrder;

		if (m_channelType != "")
		{
			conf["channelType"] = m_channelType;
			conf["channelTypeLabel"] = m_channelTypeLabel;
		}

		if (m_bDownSample)
		{
			conf["downSample"] = true;
			conf["downSampleInterval"] = m_iDownSampleInterval;
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
	//如果有数据流订阅者，直接推送
	if (m_vecDeStreamSub.size() > 0) {
		json jDe;
		jDe["val"] = jVal;
		string s = jDe.dump();

		for (int i = 0; i < m_vecDeStreamSub.size(); i++) {
			shared_ptr<TDS_SESSION> p = m_vecDeStreamSub[i];
			p->send(s.data(), s.length());
		}
	}

	//是否启动降采样，如果启用了降采样
	//降采样功能放在ioChan而不放在mp中的设计原因
	//1.降采样属于采集功能范畴，io设备管理就是采集的配置
	//2.降采样应该尽可能早的处理，减少性能消耗
	//3.一般在配置设备，了解设备参数属性的时候，才知道该设备是否高频监控点，是否需要降采样。
	//  而在监控点配置时，并不知道什么设备的什么通道会来绑定，因此并不知道是否需要配置降采样
	if (m_bDownSample) {
		long long pass = timeopt::CalcTimePassMilliSecond(m_lastDownSampleTime);
		if (pass < m_iDownSampleInterval)
			return;
		m_lastDownSampleTime = timeopt::now();
	}


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
	m_curOrgVal = jVal;
	if (m_curOrgVal.is_number()) {
		double val = 0;
		double valOrg = m_curOrgVal.get<float>();
		val = valOrg * m_k + m_b;
		m_curVal = val;
	}
	else {
		m_curVal = m_curOrgVal;
	}

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
