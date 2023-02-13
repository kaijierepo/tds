#include "pch.h"
#include "mp.h"
#include "obj.h"
#include "common.h"
#include "prj.h"
#include "as.h"
#include "rpcHandler.h"
#include "db.h"
#include "logger.h"
#include "ioSrv.h"
#include "ioChan.h"
#include "masterDs.h"
#include "httplib.h"
#include "json.hpp"

MP::MP()
{
	m_type = "mp";
	timeopt::setAsTimeOrg(m_stDataLastUpdate);
	timeopt::setAsTimeOrg(m_lastSaveTime);
	//timeopt::setAsTimeOrg(m_stLastSampleTime);
	m_K = 1;
	m_B = 0;
	m_bIsStreaming = false;
	m_deadZone = 0;
	//m_bDownSample = 0;
	//m_downSampleInterval = 2000;
	m_srcStreamFetch = "ondemand";
	m_bServeStream = false;
}

MP::~MP()
{
}

//string格式主要是人容易阅读,书写的字符串格式. 一般默认为 Hjson格式
json MP::strVal2Val(string sdv)
{
	json jVal;
	if (sdv == "" || sdv == "?")
	{
		//保持无效值
		//m_curVal.empty()==true
	}
	else
	{
		if (m_valType == VAL_TYPE::boolean)
		{
			if (sdv == "1" || sdv == "true" || sdv == "开")
				jVal = true;
			else if (sdv == "0" || sdv == "false" || sdv == "关")
				jVal = false;
		}
		else if (m_valType == VAL_TYPE::integer)
		{
			jVal = atoi(sdv.c_str());
		}
		else if (m_valType == VAL_TYPE::Float)
		{
			jVal = atof(sdv.c_str());
		}
		else if (m_valType == VAL_TYPE::str)
		{
			jVal = sdv;
		}
	}

	return jVal;
}


bool MP::loadConf(json& conf)
{
	OBJ::loadConf(conf);
	if(conf["valType"]!=nullptr)
		m_valType = conf["valType"].get<string>();

	m_valTypeLabel = getValTypeLabel(m_valType);

	if (conf["alarmMp"] != nullptr)
		m_alarmMp = conf["alarmMp"].get<bool>();
	else
		m_alarmMp = false;

	//不仅仅float类型可以使用单位. 整形也可以使用单位。例如： 3次   5个 等等 
	if (conf["unit"] != nullptr)
		m_strUnit = conf["unit"].get<string>();

	if (conf["decimalDigits"] != nullptr)
		m_decimalDigits = conf["decimalDigits"].get<int>();
	else
		m_decimalDigits = -1;

	if (conf["ioType"] != nullptr)
	{
		m_ioType = conf["ioType"].get<string>();
		//m_ioTypeLabel = getIOTypeLabel(m_ioType);
	}

	if (conf["alarmLimit"] != nullptr)
	{
		json jAL = conf["alarmLimit"];
		if (jAL["enableHigh"].is_boolean()){
			m_alarmLimit.enableHigh = jAL["enableHigh"].get<bool>();
		}
		if (jAL["high"].is_number()) {
			m_alarmLimit.high = jAL["high"].get<float>();
		}
		if (jAL["enableLow"].is_boolean()) {
			m_alarmLimit.enableLow = jAL["enableLow"].get<bool>();
		}
		if (jAL["low"].is_number()) {
			m_alarmLimit.low = jAL["low"].get<float>();
		}
	}

	if (conf["validRange"] != nullptr)
	{
		json j = conf["validRange"];
		if (j["enable"].is_boolean()) {
			m_validRange.enable = j["enable"].get<bool>();
		}
		if (j["min"].is_number()) {
			m_validRange.min = j["min"].get<double>();
		}
		if (j["max"].is_number()) {
			m_validRange.max = j["max"].get<double>();
		}
	}

	if (m_valType == VAL_TYPE::json)
	{
		if(conf["mpType"]!=nullptr)
		m_mpType = conf["mpType"].get<string>();
	}

	if (conf["saveMode"] != nullptr)
	{
		m_saveMode = conf["saveMode"].get<string>();
	}
	else
	{
		m_saveMode = DATA_SAVE_MODE::always;
	}

	if (conf["saveInterval"] != nullptr)
	{
		json jsi = conf["saveInterval"];
		m_saveInterval.hour = jsi["hour"].get<int>();
		m_saveInterval.minute = jsi["minute"].get<int>();
		m_saveInterval.second = jsi["second"].get<int>();

		//周期模式但是周期设为0，相当于全部保存。此配置无效，默认修改为5分钟
		if (m_saveMode == DATA_SAVE_MODE::cyclic || m_saveMode == DATA_SAVE_MODE::cyclic_onchange) {
			if (getSaveInterval() == 0) {
				m_saveInterval.minute = 5;
			}
		}
	}

	if (conf["k"] != nullptr)
	{
		m_K = conf["k"].get<double>();
	}
	if (conf["b"] != nullptr)
	{
		m_B = conf["b"].get<double>();
	}

	if (conf["deadZone"].is_number()) {
		m_deadZone = conf["deadZone"].get<double>();
	}

	if (conf["defaultVal"] != nullptr)
	{
		//兼容一些错误书写,支持强转
		if (conf["defaultVal"].is_string())
		{
			m_defaultVal = strVal2Val(conf["defaultVal"].get<string>());
		}
		else
		{
			m_defaultVal = strVal2Val(conf["defaultVal"].dump());
		}

		if (!m_defaultVal.empty())
		{
			m_curVal = m_defaultVal;
			timeopt::now(&m_stDataLastUpdate);
		}
	}


	if (m_valType == VAL_TYPE::integer) {
		if (conf["isEnum"].is_boolean()) {
			m_isEnum = conf["isEnum"].get<bool>();
		}

		if (conf["enum"].is_array()) {
			mapEnumVal.clear();
			json& jEnum = conf["enum"];
			for (int i = 0; i < jEnum.size(); i++) {
				json& jItem = jEnum[i];
				mapEnumVal[jItem[0].get<int>()] = jItem[1].get<string>();
			}
		}
	}

	if (conf["expression"].is_string()) {
		m_expression = conf["expression"];
	}


	if (conf["mediaSrcType"] != nullptr) {
		m_mediaSrcType = conf["mediaSrcType"].get<string>();
	}

	if (conf["mediaUrl"] != nullptr) {
		m_mediaUrl = conf["mediaUrl"].get<string>();
	}

	if (m_mediaSrcType == "ezviz") {
		prj.m_enableEzviz = true;
	}


	auto kv = conf.find("serialNo");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_serialNo = item.get<string>();
		}
	}

	 kv = conf.find("appKey");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_appKey = item.get<string>();
		}
	}

	 kv = conf.find("secret");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_string()) {
			m_secret = item.get<string>();
		}
	}

	kv = conf.find("serveStream");
	if (kv != conf.end()) {
		json& item = kv.value();
		if (item.is_boolean()) {
			m_bServeStream = item.get<bool>();
		}
	}


	//状态数据
	// 	val应该通过 loadStatus加载，不知道为何这里有这段代码。暂时注释。观察一段时间后删除
	//if(conf.contains("val"))
	//	m_curVal = conf["val"];
	if (conf.contains("time"))
	{
		string s = conf["time"].get<string>();
		if(s != "" && s != "-")
			m_stDataLastUpdate = timeopt::str2st(s);
	}

	//if (conf["downSample"].is_boolean()) {
	//	m_bDownSample = conf["downSample"].get<bool>();
	//}

	//if (conf["downSampleInterval"].is_number_integer()) {
	//	m_downSampleInterval = conf["downSampleInterval"].get<int>();
	//}

	if (conf["srcStreamFetch"].is_string()) {
		m_srcStreamFetch = conf["srcStreamFetch"];
	}

	return false;
}

bool MP::toJson(json& conf, json serializeOption)
{	
	OBJ_QUERIER q = parseQuerier(serializeOption);
	return toJson(conf, q);
}

bool MP::toJson(json& conf, OBJ_QUERIER q)
{
	if (!OBJ::toJson(conf, q))
		return false;

	if (q.getConf)
	{
		//确定是否请求了该类型的监测点
		MP* p = (MP*)this;
		conf["valType"] = p->m_valType;
		if (p->m_ioType != "")
			conf["ioType"] = p->m_ioType;
		if (p->m_valType == "json")
			conf["mpType"] = p->m_mpType;
		if (p->m_alarmMp)
			conf["alarmMp"] = true;
		conf["saveMode"] = p->m_saveMode;
		if (p->m_saveMode == DATA_SAVE_MODE::cyclic || p->m_saveMode == DATA_SAVE_MODE::cyclic_onchange)
		{
			json saveInterval;
			saveInterval["hour"] = p->m_saveInterval.hour;
			saveInterval["minute"] = p->m_saveInterval.minute;
			saveInterval["second"] = p->m_saveInterval.second;
			conf["saveInterval"] = saveInterval;
		}
		if (p->m_strUnit != "")
			conf["unit"] = p->m_strUnit;
		//KB  不等于默认值则保存
		if (fabs(p->m_K - 1) > 0.000001 || fabs(p->m_B - 0) > 0.000001)
		{
			conf["k"] = p->m_K;
			conf["b"] = p->m_B;
		}

		if (p->m_saveMode == DATA_SAVE_MODE::cyclic_onchange || p->m_saveMode == DATA_SAVE_MODE::onchange) {
			conf["deadZone"] = m_deadZone;
		}

		//默认值 
		if (p->m_defaultVal != nullptr)
			conf["defaultVal"] = p->m_defaultVal;
		//报警限
		json alarmLimit;
		if (m_alarmLimit.enableHigh)
		{
			alarmLimit["enableHigh"] = m_alarmLimit.enableHigh;
			alarmLimit["high"] = m_alarmLimit.high;
		}
		if (m_alarmLimit.enableLow)
		{
			alarmLimit["enableLow"] = m_alarmLimit.enableLow;
			alarmLimit["low"] = m_alarmLimit.low;
		}
		if (alarmLimit != nullptr)
			conf["alarmLimit"] = alarmLimit;
		//有效值范围
		if (m_validRange.enable)
		{
			json validRange;
			validRange["enable"] = m_validRange.enable;
			validRange["min"] = m_validRange.min;
			validRange["max"] = m_validRange.max;
			conf["validRange"] = validRange;
		}
		//有效小数位
		if (m_decimalDigits >= 0)
			conf["decimalDigits"] = m_decimalDigits;
		if (p->m_valType == "video") {
			conf["mediaSrcType"] = m_mediaSrcType;
			conf["mediaUrl"] = m_mediaUrl;

			if (m_mediaSrcType == "ezviz") {
				conf["serialNo"] = m_serialNo;
				conf["appKey"] = m_appKey;
				conf["secret"] = m_secret;
			}
		}
		//枚举值
		if (m_valType == VAL_TYPE::integer) {
			if (m_isEnum) {
				conf["isEnum"] = true;
				if (mapEnumVal.size() > 0) {
					json jEnum = json::array();
					for (auto& i : mapEnumVal) {
						json item = json::array();
						item.push_back(i.first);
						item.push_back(i.second);
						jEnum.push_back(item);
					}
					conf["enum"] = jEnum;
				}
			}
			else {
				conf["isEnum"] = false;
			}
		}
		//降采样
		//if (m_bDownSample) {
		//	conf["downSample"] = m_bDownSample;
		//	conf["downSampleInterval"] = m_downSampleInterval;
		//}
		
		conf["srcStreamFetch"] = m_srcStreamFetch;
		if (m_expression != "") {
			conf["expression"] = m_expression;
		}

		if (m_bServeStream) {
			conf["serveStream"] = m_bServeStream;
		}
	}
	

	if (q.getStatus)
	{
		conf["val"] = m_curVal;
		if (timeopt::isValidTime(m_stDataLastUpdate))
			conf["time"] = timeopt::st2str(m_stDataLastUpdate);
		else
			conf["time"] = "-";

		if (m_isEnum && m_curVal.is_number_integer()) {
			conf["enumVal"] = mapEnumVal[m_curVal.get<int>()];
		}
	}

	if (q.getStatusDesc) {
		conf["valDesc"] = getValDesc(q.getUnit);
	}
	

	return true;
}


string MP::getValDesc(bool getUnit) {
	string valDesc;
	if (m_curVal.is_number_float()) {
		if (m_decimalDigits >= 0) {
			string formatter = "%." + str::fromInt(m_decimalDigits) + "f";
			valDesc = str::format(formatter.c_str(), m_curVal.get<float>());
		}
		else {
			valDesc = m_curVal.dump();
		}
	}
	else if (m_curVal.is_number_integer()) {
		if (m_isEnum) {
			valDesc = mapEnumVal[m_curVal.get<int>()];
		}
		else
			valDesc = m_curVal.dump();
	}
	else if (m_curVal.is_boolean()) {
		//根据监控点名称自动生成值描述
		if (m_curVal != nullptr) {
			if (m_name.find("开关") >= 0) {
				valDesc = m_curVal.get<bool>() ? "开" : "关";
			}
			else {
				valDesc = m_curVal.dump();
			}
		}
		else {
			valDesc = "-";
		}
	}
	else if (m_curVal.is_string()) {
		valDesc = m_curVal.get<string>();
	}
	else {
		valDesc = "-";
	}
	if (getUnit) {
		valDesc += m_strUnit;
	}
	return valDesc;
}

bool MP::loadStatus(OBJ* pSrc, TIME* dataTime , bool saveDB)
{
	string tag = getTag();
	MP* ptmp = pSrc->GetMPByTag(tag);
	if (ptmp) {
		//常量类型无需加载状态
		if (ptmp->m_ioType != "c") {
			m_lastVal = m_curVal;
			m_curVal = ptmp->m_curVal;
			if (dataTime != nullptr)
				m_stDataLastUpdate = *dataTime;
			else
				m_stDataLastUpdate = ptmp->m_stDataLastUpdate;

			m_mpStatus = ptmp->m_mpStatus;

			if (saveDB && needSaveToDB()) {
				saveToDB();
			}
		}
	}
	else
		return false;
}

void MP::calcAlarm()
{
	if (m_curVal.is_number_float())
	{
		double dbCurVal = m_curVal.get<double>();
		//计算报警
		if (m_alarmLimit.enableHigh)
		{
			if (dbCurVal > m_alarmLimit.high)
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overHighLimit;
				ai.level = ALARM_LEVEL::alarm;
				ai.strAlarmDesc = str::format("报警值%f,上限值%f", dbCurVal, m_alarmLimit.high);
				almSrv.Update(ai);
			}
			else
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overHighLimit;
				ai.level = ALARM_LEVEL::normal;
				almSrv.Update(ai);
			}
		}
		if (m_alarmLimit.enableLow)
		{
			if (dbCurVal < m_alarmLimit.low)
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overLowLimit;
				ai.level = ALARM_LEVEL::alarm;
				ai.strAlarmDesc = str::format("报警值%f,下限值%f", dbCurVal, m_alarmLimit.low);
				almSrv.Update(ai);
			}
			else
			{
				ALARM_INFO ai;
				ai.tag = getTag();
				ai.type = ALARM_TYPE::overLowLimit;
				ai.level = ALARM_LEVEL::normal;
				almSrv.Update(ai);
			}
		}
	}
}



void MP::input(json jVal, TIME* dataTime, json dataFile)
{
	TIME t;
	if (dataTime == NULL)
	{
		timeopt::now(&t);
		dataTime = &t;
	}

	if (memcmp(&dataTime, &m_stDataLastUpdate, sizeof(TIME)) == 0)
		return;

	//数字类型进行kb处理和上下限处理
	if (jVal.is_number())
	{
		if (m_valType == "int") {
			int iVal = jVal.get<int>();
			m_orgVal = iVal;
			int iCurVal = iVal * m_K + m_B;
			jVal = iCurVal;
			if (m_validRange.enable)
			{
				if (iCurVal < m_validRange.min || iCurVal > m_validRange.max)
				{
					jVal = nullptr;
				}
			}
		}
		else {
			double dbVal = jVal.get<double>();
			m_orgVal = dbVal;
			//dbVal*m_k可能会把一些超过double精度的非精确字段移到前面,而产生误差.默认保留10位小数精度
			double dbCurVal = dbVal * m_K + m_B; // linear calibration using K and B 
			string sVal;
			if (m_decimalDigits >= 0)
			{
				string formatter = "%." + str::fromInt(m_decimalDigits) + "f";
				sVal = str::format(formatter.c_str(), dbCurVal);
			}
			else
				sVal = str::format("%.10f", dbCurVal);
			dbCurVal = atof(sVal.c_str());
			jVal = dbCurVal;

			if (m_validRange.enable)
			{
				if (dbCurVal < m_validRange.min || dbCurVal > m_validRange.max)
				{
					jVal = nullptr;
				}
			}
		}
	}

	updateVal(jVal, dataTime, dataFile);
}

//监控点组不算
OBJ* MP::getParentMo()
{
	OBJ* p = m_pParentMO;
	while (p) {
		if (p->m_type == MO_TYPE::mo || p->m_type == MO_TYPE::customMo) {
			return p;
		}

		p = p->m_pParentMO;
	}
	return nullptr;
}

void MP::updateVal(json& jVal, TIME* dataTime, json dataFile)
{
	TIME t;
	if (dataTime == NULL)
	{
		timeopt::now(&t);
		dataTime = &t;
	}

	if (memcmp(&dataTime, &m_stDataLastUpdate, sizeof(TIME)) == 0)
		return;

	m_stDataLastUpdate = *dataTime;
	OBJ* pParentMo = getParentMo();
	if (pParentMo)
		pParentMo->m_stDataLastUpdate = *dataTime;

	//save to rt memory
	m_lastVal = m_curVal;
	m_curVal = jVal;
	calcAlarm();
	if (this->m_valType == "json")
	{
		jVal["type"] = this->m_valType;
		jVal["mpType"] = this->m_mpType;
	}


	//特殊的属性监测点
	if (m_name == "经度")
	{
		m_pParentMO->m_longitudeDyn = m_curVal;
	}
	else if (m_name == "纬度")
	{
		m_pParentMO->m_latitudeDyn = m_curVal;
	}


	m_pParentMO->m_bOnline = true;


	if (m_alarmMp) //是一个报警监控点，更新报警
	{
		ALARM_INFO ai;
		ai.type = m_name;
		if (m_curVal.get<bool>() == true)
			ai.level = ALARM_LEVEL::alarm;
		else
			ai.level = ALARM_LEVEL::normal;
		ai.tag = getTag();
		ai.typeLabel = m_name;
		almSrv.Update(ai);
	}
}

bool MP::needSaveToDB()
{
	bool bNeedSave = false;
	if (m_curVal == nullptr)
		return bNeedSave;


	//save to db
	if (m_saveMode.find("cyclic") != string::npos)
	{
		int timespan = getSaveInterval();
		if (timeopt::CalcTimeDiffSecond(m_stDataLastUpdate,m_lastSaveTime) > timespan)
		{
			bNeedSave = true;
		}
	}

	if (m_saveMode.find("onchange") != string::npos)
	{
		if (m_curVal.is_number() && m_lastVal.is_number()) {
			double last = m_lastVal.get<double>();
			double cur = m_curVal.get<double>();
			double diff = fabs(last - cur);
			if (diff > m_deadZone && diff >0.00001) {
				bNeedSave = true;
			}
		}
		else if (m_curVal.is_boolean() && m_lastVal.is_boolean()) {
			bool last = m_lastVal.get<bool>();
			bool cur = m_curVal.get<bool>();
			if (last != cur) {
				bNeedSave = true;
			}
		}
	}

	if (m_saveMode == "always")
	{
		bNeedSave = true;
	}

	return bNeedSave;
}


void MP::saveToDB() {
	m_dbFileLock.lock();
	timeopt::now(&m_lastSaveTime);
	db.Insert(getTag().c_str(), m_stDataLastUpdate, m_curVal);
	m_dbFileLock.unlock();
}

//该接口保证rlt或者err一定会有一个值返回
void MP::output(json jVal, json& rlt, json& err,bool sync)
{
	//方案1：当前值变为nullptr,直到采集到新的数据值,才能确认当前值
	//m_curVal = nullptr;
	//方案2. 值不变。采集到新的数据值或者收到输出反馈，才变成新的值
	//do nothing

	OBJ* pOwnerChlidTds = getOwnerChildTds();

	if (pOwnerChlidTds) {
		if (pMasterDs) {
			string childTdsTag = pOwnerChlidTds->getTag();
			string tag = getTag();

			tag = TAG::trimRoot(tag, childTdsTag);
			json params;
			params["tag"] = tag;
			params["val"] = jVal;

			json childRlt, childErr;
			pMasterDs->callChildTds(childTdsTag, "output", params, childRlt, childErr, sync);
			if (sync) {
				if (childRlt != nullptr) {
					rlt = params;
					m_curVal = jVal;
					timeopt::now(&m_stDataLastUpdate);
				}
				else if(childErr != nullptr) {
					err = childErr;
				}
				else {
					LOG("[error]严重错误 mp.cpp %d\n", __LINE__);
				}
			}
		}
		else {
			err = "error: master data service is not started";
		}
	}
	else {
		ioChannel* pC = ioSrv.getChanByTag(getTag());
		if (pC)
		{
			LOG("[warn][数据输出  ]发送请求;位号:%s,值:%s,通道:%s,等待响应:%d", getTag().c_str(), jVal.dump().c_str(), pC->getIOAddrStr().c_str(),sync?1:0);
			pC->output(jVal, rlt, err, sync);
			ASSERT(rlt != nullptr && err != nullptr);
			if (sync) {
				if (rlt != nullptr) {
					LOG("[warn][数据输出  ]输出成功,位号:%s,值:%s,通道:%s", getTag().c_str(), jVal.dump().c_str(), pC->getIOAddrStr().c_str());
				}
				if(err!=nullptr)
				{
					LOG("[warn][数据输出  ]输出失败,位号:%s,值:%s,通道:%s,失败信息:%s", getTag().c_str(), jVal.dump().c_str(), pC->getIOAddrStr().c_str(),err.dump().c_str());
				}
			}
		}
		else
		{
			err = makeRPCError(RPC_ERROR_CODE::MO_outputFail, "未找到绑定的通道");
		}
	}
}


bool MP::IsCurValValid()
{
	return !m_curVal.empty();
}

string MP::getMpTypeLabel()
{
	string typeLabel;
	if (m_valType == VAL_TYPE::boolean)
	{
		typeLabel = m_name;
	}
	else if (m_valType == VAL_TYPE::Float)
	{
		typeLabel = m_name;
	}
	else if (m_valType == VAL_TYPE::json)
	{
		typeLabel = m_mpType;
	}
	else
	{
		typeLabel = getValTypeLabel(m_valType);
	}

	return typeLabel;
}



string MP::getMpType()
{
	string mpType;
	// as a convention , a real type MP's name is named by data type.
	if (m_valType == VAL_TYPE::boolean)
	{
		mpType = m_name;
	}
	else if (m_valType == VAL_TYPE::Float)
	{
		mpType = m_name;
	}
	else if (m_valType == VAL_TYPE::video)
	{
		mpType = "视频";
	}
	else if (m_valType == VAL_TYPE::json)
	{
		mpType = m_mpType;
	}
	else
	{
		mpType = "未知类型";
	}

	return mpType;
}

int MP::getSaveInterval()
{
	int si = m_saveInterval.hour * 60 * 3600 + m_saveInterval.minute * 60 + m_saveInterval.second;
	return si;
}

json MP::getRTData(string root, bool bValOnly)
{
	json j;
	if(m_stDataLastUpdate.wYear == 0 || m_stDataLastUpdate.wYear == 1970)
		j["time"] = "?";
	else
		j["time"] = timeopt::st2str(m_stDataLastUpdate);
	j["tag"] = getTag(root);
	if(m_curVal.empty())
		j["val"] = "?";
	else
		j["val"] = m_curVal;

	if (!bValOnly)
	{
		j["unit"] = m_strUnit;
		j["valType"] = m_valType;
		j["valTypeLabel"] = m_valTypeLabel;
		j["ioType"] = m_ioType;
		j["ioTypeLabel"] = m_ioTypeLabel;
		if (m_decimalDigits >= 0)
			j["decimalDigits"] = m_decimalDigits;

		if (m_validRange.enable)
		{
			j["min"] = m_validRange.min;
			j["max"] = m_validRange.max;
		}
	}

	return j;
}

void MP::zlm_closeStreamSrc(string tag)
{
	string key = "__defaultVhost__/stream_" + prj.getTdsId() + "/" + tag;
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string streamServerUrl = "http://127.0.0.1:" + sPort;
	httplib::Client cli(streamServerUrl);
	httplib::Headers headers;
	httplib::Params params = {
		{ "key", key }
	};

	string uri = "/index/api/delStreamProxy";
	auto res = cli.Get(uri, params, headers);
	LOG("[ZLMediaServer]Rest Api,Get " + streamServerUrl + uri + ",proxyKey=" + key);
	if (res != nullptr) {
		LOG("[ZLMediaServer] Status:%d,Response Body:%s", res->status, res->body.c_str());
	}
	else {
		LOG("[error]zlm stream server 未响应," + uri);
	}
}


void MP::zlm_openStreamSrc()
{
	string tag = getTag();
	//string tagPinyin;
	//str::hanZi2Pinyin(tag,tagPinyin);
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string streamServerUrl = "http://127.0.0.1:" + sPort;
	//tag = httplib::detail::encode_url(charCodec::utf8toAnsi(tag));
	//码流的app字段加入tdsID的原因
	//使用frp码流转发时，frp转发http请求需要根据第一级路径来确定需要转发给哪个子服务
	string app = "stream_" + prj.getTdsId();
	httplib::Client cli(streamServerUrl);
	httplib::Headers headers;
	httplib::Params params = {
		{ "vhost", "__defaultVhost__" },
		{"app",app},
		{"stream",tag},
		{"url",m_mediaUrl},
		{"enable_hls","0"},
		{"enable_ts","0"},
		{"enable_mp4","0"}
	};

	string uri = "/index/api/addStreamProxy";
	auto res = cli.Get(uri, params, headers);
	LOG("[ZLMediaServer]Rest Api,Get " + streamServerUrl + uri + ",app=" + app + ",stream=" + tag + ",媒体源=" + m_mediaUrl);
	if (res != nullptr) {
		json jResp = json::parse(res->body);
		json jData = jResp["data"];
		if (jData != nullptr && jData["key"] != nullptr) {
			m_mpStatus.m_pullingSrcUrl = m_mediaUrl;
		}
		LOG("[ZLMediaServer] Status:%d,Response Body:%s", res->status, res->body.c_str());
	}
	else {
		LOG("[error]zlm stream server 未响应," + uri);
	}
}