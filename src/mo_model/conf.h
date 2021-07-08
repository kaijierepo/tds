#pragma once
#include <string>
using namespace std;
#include "json.hpp"
#include "tds.h"

class tdsConfig : public iTDSConf
{
public:
	tdsConfig();
	void loadConf();


	json jsonConf;
};

