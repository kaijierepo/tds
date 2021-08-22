#pragma once
#include "ioChan.h"

class ioChan_tuya : public ioChannel
{
	bool output(json jVal) override;
	string m_deviceID;
};

