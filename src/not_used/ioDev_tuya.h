#ifndef TDS_NOT_USED_IODEV_TUYA_H
#define TDS_NOT_USED_IODEV_TUYA_H

#include "ioDev.h"
#include "tcpClt.h"
class ioDev_tuya : public ioDev
{
public:
	ioDev_tuya();
	bool getCurrentVal();
	virtual void output(string chanAddr, json jVal, json& rlt,json& err,bool sync) override;
};


#endif /* TDS_NOT_USED_IODEV_TUYA_H */
