#pragma once
#include "mo.h"
#include "json.hpp"

class amo : public mo
{
public:
	amo();
	virtual ~amo();

	void GetRTStatusJson(json& j);

	SYSTEMTIME m_enterTime;
	string    m_strTagName;
	string    m_strAMOType;
};