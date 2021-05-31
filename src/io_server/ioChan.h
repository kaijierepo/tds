#pragma once
#include "tdscore.h"
#include "json.hpp"
#include "ioDev.h"
using json = nlohmann::json;

class ioChannel : public ioDev
{
public:
	ioChannel();
	~ioChannel();

	bool match(string channelNo);

	string GetCommLinkTag();
	virtual void inputVal(json jVal, SYSTEMTIME* dataTime=NULL, bool bPic=false);
	virtual bool outputVal(json jVal);
	bool IsValid();

	string m_strLinkMPTag;
	SYSTEMTIME m_stLastUpdateTime;
};
