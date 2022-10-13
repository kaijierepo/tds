#pragma once
#include "obj.h"
#include "json.hpp"

class amo : public OBJ
{
public:
	amo();
	virtual ~amo();

	void GetRTStatusJson(json& j);

	SYSTEMTIME m_enterTime;
	string    m_strTagName;
	string    m_strAMOType;
};