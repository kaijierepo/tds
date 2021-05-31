#pragma once
#include "ioDev.h"
#include "tcpClt.h"

class ioGW_tuyaProject : public ioDev
{
public:
	bool run() override;

	bool outputVal(json jVal, string chanAddr) override;
	bool inputVal(json jVal, string chanAddr) override;

	string m_accessToken;
	string m_refreshToken;
	string m_sign;
};

