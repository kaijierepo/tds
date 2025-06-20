#pragma once
#include "ioDev.h"
#include "tcpClt.h"
class ioDev_tuya : public ioDev
{
public:
	ioDev_tuya();
	bool getCurrentVal();
	virtual void output(string chanAddr, json jVal, json& rlt,json& err,bool sync) override;
};

