#include "pch.h"
#include "mp.h"
#include "mo.h"
#include "common.h"
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
#ifdef ENABLE_FFMPEG
	m_videoCodec = NULL;
#endif
}

MP::~MP()
{
}


bool MP::loadConf(json& conf)
{
	MO::loadConf(conf);
	m_valType = conf["valType"].get<string>();
	if (m_valType == TDS::VAL_TYPE::real)
	{
		if(conf["unit"]!=nullptr)
			m_strUnit = conf["unit"].get<string>();
	}
	else if (m_valType == TDS::VAL_TYPE::json)
	{
		if(conf["mpType"]!=nullptr)
		m_mpType = conf["mpType"].get<string>();
	}

	if (conf["saveInterval"] != nullptr)
	{
		json jsi = conf["saveInterval"];
		m_saveInterval.hour = jsi["hour"].get<int>();
		m_saveInterval.minute = jsi["minute"].get<int>();
		m_saveInterval.second = jsi["second"].get<int>();
	}
		
	return false;
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
	

	//save to db
	int timespan = getSaveInterval();
	if (timeopt::CalcTimePassSecond(m_lastSaveTime) > timespan)
	{
		GetLocalTime(&m_lastSaveTime);
		db.INSERT(getTag().c_str(), *dataTime, jVal,dataFile);
	}
}

bool MP::output(json jVal)
{
	ioChannel* pC = ioSrv.getIOChan(getTag());
	if (pC)
	{
		return pC->output(jVal);
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
	else if (m_valType == TDS::VAL_TYPE::real)
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
	else if (m_valType == TDS::VAL_TYPE::real)
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

json MP::getRT()
{
	json j;
	j["time"] = timeopt::st2str(m_lastUpdateTime);
	j["name"] = m_strName;
	j["val"] = m_curVal;
	j["type"] = m_moType;
	return j;
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