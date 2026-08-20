#ifndef TDS_NOT_USED_IOGW_TUYAPROJECT_H
#define TDS_NOT_USED_IOGW_TUYAPROJECT_H

#include "ioDev.h"
#include "tcpClt.h"

class ioGW_tuyaProject : public ioDev
{
public:
	ioGW_tuyaProject();
	bool run() override;

	bool outputVal(json jVal, string chanAddr) override;
	bool inputVal(json jVal, string chanAddr) override;

	string m_accessToken;
	string m_refreshToken;
	string m_sign;
};


#endif /* TDS_NOT_USED_IOGW_TUYAPROJECT_H */
