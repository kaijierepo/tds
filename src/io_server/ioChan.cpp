#include "pch.h"
#include "ioChan.h"
#include "ioDev.h"


ioChannel::ioChannel()
{
	m_level = "channel";
	m_ioType = CHAN_IO_TYPE::I;
	m_k = 1;
	m_b = 0;
	m_bDownSample = false;
	m_iDownSampleInterval = 1000;
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

	if(m_fmt!="") 
		m_valType = storageFmt2valType(m_fmt);
	else if (conf["valType"] != nullptr)
		m_valType = conf["valType"];


	if (m_valType != "")
		m_valTypeLabel = getValTypeLabel(m_valType);

	if (conf["name"] != nullptr)
		m_name = conf["name"];

	//先加载ioType. 在ioDev::loadConf中需要赋值给绑定的位号
	ioDev::loadConf(conf);
	
	return true;
}

bool ioChannel::toJson(json& conf, DEV_QUERIER querier)
{
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

		if (m_bDownSample)
		{
			conf["downSample"] = true;
			conf["downSampleInterval"] = m_iDownSampleInterval;
		}
	}


	if (querier.getStatus)
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
	DEV_QUERIER query;
	toJson(j, query);
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
	if (tag != "" && timeopt::isValidTime(m_stLastUpdateTime) && m_curVal!=nullptr) {
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

void ioChannel::input(yyjson_val* jVal, TIME* dataTime, bool bPic) {
	string tagBind;
	input(jVal, tagBind, dataTime, bPic);

	//更新绑定位号值
	json param;
	param["tag"] = tagBind;
	param["val"] = m_curVal;
	param["time"] = timeopt::st2str(m_stLastUpdateTime);
	tds->callAsyn("input", param);
}

void ioChannel::input(json jVal, TIME* dataTime, bool bPic) {
	string tagBind;
	input(jVal, tagBind, dataTime, bPic);

	//更新绑定位号值
	json param;
	param["tag"] = tagBind;
	param["val"] = m_curVal;
	param["time"] = timeopt::st2str(m_stLastUpdateTime);
	tds->callAsyn("input", param);
}

void ioChannel::input(json jVal, string& tagBind, TIME* dataTime, bool bPic)
{
	//更新通道值. 经过一次kb转换，推送的数据流应当是经过转换后的值
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
		double valOrg = m_curOrgVal.get<double>();
		val = valOrg * m_k + m_b;
		m_curVal = val;
	}
	else {
		m_curVal = m_curOrgVal;
	}

	//如果有数据流订阅者，直接推送
	m_csStreamPuller.lock();
	if (m_vecStreamPuller.size() > 0) {
		json jDe;
		jDe["val"] = m_curVal;
		string s = jDe.dump() + "\n\n"; //数据流都要加，便于分帧

		for (size_t i = 0; i < m_vecStreamPuller.size(); i++) {
			shared_ptr<TDS_SESSION> p = m_vecStreamPuller[i];
			size_t iSend = p->send((unsigned char*)s.data(), s.length());
			if (iSend <= 0) {
				m_vecStreamPuller.erase(m_vecStreamPuller.begin() + i);
				i--;
				continue;
			}
		}
	}
	m_csStreamPuller.unlock();

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

	tagBind = getTagBind();
}

void ioChannel::input(yyjson_val* jVal, string& tagBind, TIME* dataTime, bool bPic)
{
	//更新通道值. 经过一次kb转换，推送的数据流应当是经过转换后的值
	TIME t;
	if (dataTime == NULL)
	{
		timeopt::now(&t);
		dataTime = &t;
	}
	m_stLastUpdateTime = *dataTime;
	if (yyjson_is_real(jVal)) {
		double valOrg = yyjson_get_real(jVal);
		m_curOrgVal = valOrg;
		double val = valOrg * m_k + m_b;
		m_curVal = val;
	}
	else if (yyjson_is_int(jVal)){
		int valOrg = yyjson_get_int(jVal);
		m_curOrgVal = valOrg;
		int val = valOrg * m_k + m_b;
		m_curVal = val;
	}
	else if (yyjson_is_bool(jVal)) {
		bool valOrg = yyjson_get_bool(jVal);
		m_curOrgVal = valOrg;
		m_curVal = valOrg;
	}

	//如果有数据流订阅者，直接推送
	m_csStreamPuller.lock();
	if (m_vecStreamPuller.size() > 0) {
		json jDe;
		jDe["val"] = m_curVal;
		string s = jDe.dump() + "\n\n"; //数据流都要加，便于分帧

		for (size_t i = 0; i < m_vecStreamPuller.size(); i++) {
			shared_ptr<TDS_SESSION> p = m_vecStreamPuller[i];
			size_t iSend = p->send((unsigned char*)s.data(), s.length());
			if (iSend <= 0) {
				m_vecStreamPuller.erase(m_vecStreamPuller.begin() + i);
				i--;
				continue;
			}
		}
	}
	m_csStreamPuller.unlock();

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

	tagBind = getTagBind();
}


//ioChannel的输出统一由父设备实现，因为通道的特性是由设备决定的，什么设备决定了有什么通道
//例如Modbus设备就有以寄存器为特征的通道
void ioChannel::output(json jVal, json& rlt,json& err, bool sync)
{
	//输出时乘上反向倍率
	if (jVal.is_number_integer()) {
		int iVal = jVal.get<int>();
		double dbK_reverse = 1 / m_k;
		iVal = iVal * dbK_reverse;
		jVal = iVal;
	}
	else if (jVal.is_number()) {
		double dbVal = jVal.get<double>();
		double dbK_reverse = 1 / m_k;
		dbVal = dbVal * dbK_reverse;
		jVal = dbVal;
	}


	ioDev* pDev = ioDev::m_pParent;
	pDev->output(this,jVal, rlt,err,sync);
}
