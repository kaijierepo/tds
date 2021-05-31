#pragma once
#include "ioChan.h"

class ioChan_tuya : public ioChannel
{
	bool outputVal(json jVal) override;
	string m_deviceID;
};

