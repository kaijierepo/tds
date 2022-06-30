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
	bool toJson(json& conf, json opt = nullptr) override;
	bool getChanStatus(json& statusList) override;
	bool match(string channelNo);

	virtual void input(json jVal, SYSTEMTIME* dataTime=NULL, bool bPic=false);
	virtual bool output(json jVal, json& rlt,json& err,bool sync = false); //sync指定为同步输出,该函数将阻塞

	json m_curVal;
	unsigned short m_regOffset;
	string m_regType; //modbus寄存器类型
	string m_storageFmt;

	string m_ioType;
	string m_ioTypeLabel;
	string m_valType;
	string m_valTypeLabel;

	SYSTEMTIME m_stLastUpdateTime;
};
