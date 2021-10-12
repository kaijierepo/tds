#include "pch.h"
#include "amo.h"
#include "common.hpp"

amo::amo()
{
	m_moType = "active_obj";
}

amo::~amo()
{
}

void amo::GetRTStatusJson(json& j)
{
	j["name"] = m_strName;
	j["area"] = m_strTagName;
	j["mo_type"] = m_strAMOType;
	j["enter_time"]= timeopt::st2str(m_enterTime);
}
