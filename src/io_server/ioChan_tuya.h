#pragma once
#include "ioChan.h"

class ioChan_tuya : public ioChannel
{
	bool output(json jVal, json& jResp) override;
	string m_deviceID;
};
