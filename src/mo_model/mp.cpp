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
	m_physicalType = PHYSICAL_TYPE::unknown;
	GetLocalTime(&m_lastUpdateTime);
	GetLocalTime(&m_lastSaveTime);
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
	m_valType = conf["val_type"].get<string>();
	if (m_valType == TDS::DATA_TYPE::real)
	{
		m_physicalType = conf["physical_type"].get<string>();
		if(conf["unit"]!=nullptr)
			m_strUnit = conf["unit"].get<string>();
	}
	else if (m_valType == TDS::DATA_TYPE::json)
	{
		m_customValType = conf["custom_val_type"].get<string>();
	}

	if (conf["save_interval"] != nullptr)
	{
		json jsi = conf["save_interval"];
		m_saveInterval.hour = jsi["hour"].get<int>();
		m_saveInterval.minute = jsi["minute"].get<int>();
		m_saveInterval.second = jsi["second"].get<int>();
	}
		
	return false;
}


void MP::inputVal(json jVal, SYSTEMTIME* dataTime, bool bPic)
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
			jVal["custom_type"] = this->m_customValType;
		}
	}
	

	//save to db
	int timespan = getSaveInterval();
	if (timeopt::CalcTimePassSecond(m_lastSaveTime) > timespan)
	{
		GetLocalTime(&m_lastSaveTime);
		db.INSERT(getTag().c_str(), *dataTime, jVal);
	}
}

bool MP::outputVal(json jVal)
{
	ioChannel* pC = ioSrv.getIOChan(getTag());
	if (pC)
	{
		return pC->outputVal(jVal);
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
	if (m_valType == TDS::DATA_TYPE::switching)
	{
		typeLabel = m_strName;
	}
	else if (m_valType == TDS::DATA_TYPE::real)
	{
		typeLabel = m_strName;
	}
	else if (m_valType == TDS::DATA_TYPE::json)
	{
		typeLabel = m_customValType;
	}
	else
	{
		typeLabel = TDS::DATA_TYPE_LABEL.at(m_valType);
	}

	return typeLabel;
}

string MP::getMpType()
{
	string mpType;
	// as a convention , a real type MP's name is named by data type.
	if (m_valType == TDS::DATA_TYPE::switching)
	{
		mpType = TDS::DATA_TYPE::switching + "." + m_strName;
	}
	else if (m_valType == TDS::DATA_TYPE::real)
	{
		mpType = TDS::DATA_TYPE::real + "." + m_strName;
	}
	else if (m_valType == TDS::DATA_TYPE::json)
	{
		mpType = m_customValType;
	}
	else
	{
		mpType = m_valType;
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
	j["val_type"] = m_valType;
	return j;
}