#pragma once 
#include "tdscore.h"
#include "mo.h"
#include "json.hpp"

using namespace std;
class mo;
class mp : public mo
{
public:
	mp();
	~mp();

	bool loadConf(json& conf);
public:
	void inputVal(json jVal, SYSTEMTIME* dataTime=NULL, bool bPic = false);//bPic: whether or not the data element have a related picture
	bool outputVal(json jVal);
	bool IsCurValValid();
	string getMpTypeLabel();
	string getMpType();
	json getRTData();
	json getRT();
	json m_orgVal;
	json m_curVal;
	string m_valType;
	string m_physicalType;
	string m_strUnit;
	SYSTEMTIME m_lastUpdateTime;
	
	float m_K; 
	float m_B; 
};
