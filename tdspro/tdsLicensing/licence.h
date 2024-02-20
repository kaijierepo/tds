#pragma once
#include <string>
using namespace std;
#include "json.hpp"
using json = nlohmann::json;
using namespace std;

namespace Licence {
	bool getDevcieInfo(const char* cmd, string& result);
	string getDeviceFingerPrint();
	string getLicenceSerial();
	string getFingerPrint();
	string getActivateCode(json j);
	string getCompanyActivateCode(json j);
	void createLicence();
	void writeCodeToLicence();
};

