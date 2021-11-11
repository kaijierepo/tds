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

	bool loadConf(json& conf) override;
	bool toJson(json& conf, string opt = "") override;

	bool match(string channelNo);

	virtual void input(json jVal, SYSTEMTIME* dataTime=NULL, bool bPic=false);
	virtual bool output(json jVal, json& jResp);
	bool IsValid();

	string m_regType; //modbus寄存器类型
	string m_storageFmt;

	string m_ioType;
	string m_ioTypeLabel;
	string m_valType;
	string m_valTypeLabel;

	string m_strLinkMPTag;
	SYSTEMTIME m_stLastUpdateTime;
};
