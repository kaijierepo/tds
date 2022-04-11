#pragma once
#include "httplib.h"
#include "json.hpp"
using json = nlohmann::json;

class RProxy {
public:
	RProxy();
	void run();

	json m_jConf;
};

extern RProxy* rpProxy;