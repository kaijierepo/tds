#pragma once
#include <string>
using namespace std;
#include "json.hpp"
using json = nlohmann::json;
using namespace std;
#include "licence.h"
#include "common.h"


enum LICENCE_TYPE {
	NONE,
	AUTH_PT_COUNT,  //按点数授权。永久版。需要激活码
	AUTH_TIME,      //按时间授权，试用版。无需激活码
};

class LicenceMng {
public:
	LicenceMng();
	bool checkLicence();
	void refreshStatus(); //刷新当前的io点数，或者剩余体验时间
	size_t m_authPtCount; //授权的点数
	size_t m_confPtCount; //配置的点数
	bool m_validLicence; //是否有有效证书
	string m_company; //企业一次性授权
	bool m_bCanUse;  //是否可以使用。无效证书或者点数超限将5分钟倒计时退出
	int m_leftTrialTime;
	string m_licenceStatus;
	TIME m_tryTimeLimit;
	bool m_hasTimeLimit;
	LICENCE_TYPE m_licenceType;
};

extern LicenceMng licenceMng;