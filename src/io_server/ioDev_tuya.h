#pragma once
#include "ioDev.h"
#include "tcpClt.h"
class ioDev_tuya : public ioDev
{
public:
	bool getCurrentVal();
	virtual bool output(string chanAddr, json jVal, json& chanResp,bool sync) override;
};

