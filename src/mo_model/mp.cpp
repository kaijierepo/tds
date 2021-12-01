#include "pch.h"
#include "mp.h"
#include "mo.h"
#include "common.hpp"
#include "prj.h"
#include "as.h"
#include "rpcHandler.h"
#include "db.h"
#include "logger.h"
#include "ioSrv.h"
#include "ioChan.h"

MP::MP()
{
	m_moType = "mp";
	//m_physicalType = PHYSICAL_TYPE::unknown;
	timeopt::setAsTimeOrg(m_lastUpdateTime);
	timeopt::setAsTimeOrg(m_lastSaveTime);
	m_K = 1;
	m_B = 0;

}

MP::~MP()
{
}


bool MP::loadConf(json& conf)
{
	MO::loadConf(conf);
	if(conf["valType"]!=nullptr)
		m_valType = conf["valType"].get<string>();

	m_valTypeLabel = VAL_TYPE_LABEL.at(m_valType);

	//不仅仅float类型可以使用单位. 整形也可以使用单位。例如： 3次   5个 等等 
	if (conf["unit"] != nullptr)
		m_strUnit = conf["unit"].get<string>();

	if (conf["decimalDigits"] != nullptr)
		m_decimalDigits = conf["decimalDigits"].get<string>();
	else
		m_decimalDigits = "自动";

	if (conf["alarmLimit"] != nullptr)
	{
		m_alarmLimit.enableHigh = conf["alarmLimit"]["enableHigh"].get<bool>();
		m_alarmLimit.high = conf["alarmLimit"]["high"].get<float>();
		m_alarmLimit.enableLow = conf["alarmLimit"]["enableLow"].get<bool>();
		m_alarmLimit.low = conf["alarmLimit"]["low"].get<float>();
	}

	if (conf["validRange"] != nullptr)
	{
		m_validRange.enable = conf["validRange"]["enable"].get<bool>();
		m_validRange.min = conf["validRange"]["min"].get<double>();
		m_validRange.max = conf["validRange"]["max"].get<double>();
	}

	if (m_valType == TDS::VAL_TYPE::json)
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
		m_saveMode = "never";
	}

	if (conf["saveInterval"] != nullptr)
	{
		json jsi = conf["saveInterval"];
		m_saveInterval.hour = jsi["hour"].get<int>();
		m_saveInterval.minute = jsi["minute"].get<int>();
		m_saveInterval.second = jsi["second"].get<int>();
	}

	if (conf["k"] != nullptr)
	{
		m_K = conf["k"].get<double>();
	}
	if (conf["b"] != nullptr)
	{
		m_B = conf["b"].get<double>();
	}

	if (conf["defaultVal"] != nullptr)
	{
		string sdv = conf["defaultVal"].get<string>();
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
					m_defaultVal = true;
				else if (sdv == "0" || sdv == "false" || sdv == "关")
					m_defaultVal = false;
			}
			else if (m_valType == VAL_TYPE::integer)
			{
				m_defaultVal = atoi(sdv.c_str());
			}
			else if (m_valType == VAL_TYPE::Float)
			{
				m_defaultVal = atof(sdv.c_str());
			}
			else if (m_valType == VAL_TYPE::str)
			{
				m_defaultVal = sdv;
			}
		}

		if (!m_defaultVal.empty())
		{
			m_curVal = m_defaultVal;
			GetLocalTime(&m_lastUpdateTime);
		}
	}

		
	return false;
}

bool MP::toJson(json& conf, json serializeOption)
{
	if (!MO::toJson(conf, serializeOption))
		return false;
	MP* p = (MP*)this;
	bool bIncluded = false;
	//确定是否请求了该类型的监测点
	if (serializeOption["valType"] != nullptr)
	{
		json& vt = serializeOption["valType"];
		for (int i = 0; i < vt.size(); i++)
		{
			json& jType = vt[i];
			if (jType.get<string>() == p->m_valType)
			{
				bIncluded = true;
				break;
			}
		}
	}
	else
	{
		bIncluded = true;
	}
	if (!bIncluded)
		return false;

	conf["valType"] = p->m_valType;
	if (p->m_valType == "json")
		conf["mpType"] = p->m_mpType;
	json saveInterval;
	saveInterval["hour"] = p->m_saveInterval.hour;
	saveInterval["minute"] = p->m_saveInterval.minute;
	saveInterval["second"] = p->m_saveInterval.second;
	conf["saveMode"] = p->m_saveMode;
	conf["saveInterval"] = saveInterval;
	conf["unit"] = p->m_strUnit;
	conf["k"] = p->m_K;
	conf["b"] = p->m_B;
	conf["defaultVal"] = p->m_defaultVal.dump();

	json alarmLimit;
	alarmLimit["enableHigh"] = m_alarmLimit.enableHigh;
	alarmLimit["enableLow"] = m_alarmLimit.enableLow;
	alarmLimit["high"] = m_alarmLimit.high;
	alarmLimit["low"] = m_alarmLimit.low;
	conf["alarmLimit"] = alarmLimit;

	json validRange;
	validRange["enable"] = m_validRange.enable;
	validRange["min"] = m_validRange.min;
	validRange["max"] = m_validRange.max;
	conf["validRange"] = validRange;

	return true;
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


void MP::input(json jVal, SYSTEMTIME* dataTime, json dataFile)
{
	SYSTEMTIME t;
	if (dataTime == NULL)
	{
		GetLocalTime(&t);
		dataTime = &t;
	}
		
	if (memcmp(&dataTime, &m_lastUpdateTime, sizeof(SYSTEMTIME)) == 0)
		return;
	m_lastUpdateTime = *dataTime;

	//save to rt memory
	bool bValChange = false;
	m_lastVal = m_curVal;
	if (jVal.is_number())
	{
		double dbVal = jVal.get<double>();
		m_orgVal = dbVal;
		double dbCurVal = dbVal * m_K + m_B; // linear calibration using K and B 
		m_curVal = dbCurVal;


		if (m_validRange.enable)
		{
			if (dbCurVal < m_validRange.min || dbCurVal > m_validRange.max)
			{
				m_curVal = nullptr;
			}
		}

		calcAlarm();
	}
	else if (jVal.is_boolean())
	{
		m_curVal = jVal;
	}
	else if (jVal.is_string())
	{
		m_curVal = jVal;
	}
	else
	{
		m_curVal = jVal;
		jVal["type"]= this->m_valType;
		if (this->m_valType == "json")
		{
			jVal["mpType"] = this->m_mpType;
		}
	}
	
	//notify to tds client
	json rtList;
	json pt = getRTData();
	rtList.push_back(pt);
	tdsSrv.notify("rt", rtList);

	//save to db
	bool bNeedSave = false;
	if (m_saveMode == "cyclic")
	{
		int timespan = getSaveInterval();
		if (timeopt::CalcTimePassSecond(m_lastSaveTime) > timespan)
		{
			bNeedSave = true;
		}
	}
	else if (m_saveMode == "onchange")
	{
		if (m_lastVal != m_curVal)
			bNeedSave = true;
	}
	else if(m_saveMode == "always")
	{
		bNeedSave = true;
	}


	if (bNeedSave)
	{
		GetLocalTime(&m_lastSaveTime);
		db.Insert(getTag().c_str(), *dataTime, m_curVal,dataFile);
	}

	
}

bool MP::output(json jVal, json& jResp, bool sync)
{
	//当前值变为nullptr,直到采集到新的数据值,才能确认当前值
	m_curVal = nullptr;

	ioChannel* pC = ioSrv.getChanByTag(getTag());
	if (pC)
	{
		return pC->output(jVal, jResp,sync);
	}
	else
		return false;
}


bool MP::IsCurValValid()
{
	return !m_curVal.empty();
}

string MP::getMpTypeLabel()
{
	string typeLabel;
	if (m_valType == TDS::VAL_TYPE::boolean)
	{
		typeLabel = m_strName;
	}
	else if (m_valType == TDS::VAL_TYPE::Float)
	{
		typeLabel = m_strName;
	}
	else if (m_valType == TDS::VAL_TYPE::json)
	{
		typeLabel = m_mpType;
	}
	else
	{
		typeLabel = TDS::VAL_TYPE_LABEL.at(m_valType);
	}

	return typeLabel;
}

string MP::getMpType()
{
	string mpType;
	// as a convention , a real type MP's name is named by data type.
	if (m_valType == TDS::VAL_TYPE::boolean)
	{
		mpType = m_strName;
	}
	else if (m_valType == TDS::VAL_TYPE::Float)
	{
		mpType = m_strName;
	}
	else if (m_valType == TDS::VAL_TYPE::video)
	{
		mpType = "视频";
	}
	else if (m_valType == TDS::VAL_TYPE::json)
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

json MP::getRTData(string root)
{
	json j;
	if(m_lastUpdateTime.wYear == 0 || m_lastUpdateTime.wYear == 1970)
		j["time"] = "?";
	else
		j["time"] = timeopt::st2str(m_lastUpdateTime);
	j["tag"] = getTag(root);
	if(m_curVal.empty())
		j["val"] = "?";
	else
		j["val"] = m_curVal;
	j["unit"] = m_strUnit;
	j["valType"] = m_valType;
	j["valTypeLabel"] = m_valTypeLabel;
	j["ioType"] = m_ioType;
	j["ioTypeLabel"] = m_ioTypeLabel;
	j["decimalDigits"] = m_decimalDigits;

	if (m_validRange.enable)
	{
		j["min"] = m_validRange.min;
		j["max"] = m_validRange.max;
	}
	return j;
}