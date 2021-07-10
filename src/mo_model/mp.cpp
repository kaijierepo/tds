#include "pch.h"
#include "mp.h"
#include "mo.h"
#include "common.h"
#include "prj.h"
#include "as.h"
#include "tdspSrv.h"
#include "db.h"
#include "logger.h"
#include "ioSrv.h"
#include "ioChan.h"

MP::MP()
{
	m_moType = "mp";
	m_physicalType = PHYSICAL_TYPE::unknown;
	memset(&m_lastUpdateTime, 0, sizeof(m_lastUpdateTime));
	m_K = 1;
	m_B = 0;
	m_videoCodec = NULL;
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
	db.INSERT(getTag().c_str(), *dataTime, jVal);
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

json MP::getRTData()
{
	json j;
	j["time"] = timeopt::st2str(m_lastUpdateTime);
	j["tag"] = getTag();
	j["val"] = m_curVal;
	j["unit"] = m_strUnit;
	j["val_type"] = m_valType;
	return j;
}