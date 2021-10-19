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

	if (m_valType == TDS::VAL_TYPE::Float)
	{
		if(conf["unit"]!=nullptr)
			m_strUnit = conf["unit"].get<string>();
	}
	else if (m_valType == TDS::VAL_TYPE::json)
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

		
	return false;
}

bool MP::toJson(json& conf, json serializeOption)
{
	MO::toJson(conf, serializeOption);
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

	return true;
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
		m_curVal = dbVal * m_K + m_B; // linear calibration using K and B 
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
		db.Insert(getTag().c_str(), *dataTime, jVal,dataFile);
	}
}

bool MP::output(json jVal, json& jResp)
{
	ioChannel* pC = ioSrv.getIOChan(getTag());
	if (pC)
	{
		return pC->output(jVal, jResp);
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

json MP::getRTData()
{
	json j;
	if(m_lastUpdateTime.wYear == 0)
		j["time"] = "?";
	else
		j["time"] = timeopt::st2str(m_lastUpdateTime);
	j["tag"] = getTag();
	if(m_curVal.empty())
		j["val"] = "?";
	else
		j["val"] = m_curVal;
	j["unit"] = m_strUnit;
	j["valType"] = m_valType;
	return j;
}