#include "licenceMng.h"
#include <Windows.h>
#include <string>
#include <list>
#include <iostream>
#include <functional>
#include "common.h"
#include "tds.h"

using namespace Licence;
using namespace std;
#define BUFFER_SIZE 128

void test()
{
	string deviceFingerPrint = getDeviceFingerPrint();
	cout << deviceFingerPrint << endl;
}

LicenceMng licenceMng;

void g_createLicence()
{
	createLicence();
}

void checkLicenceThread() {
	//不为tds不启动授权管理
	if ("tds" != fs::appName())
		return;

	timeopt::sleepMilli(3000);
	createLicence();

	string title;
	licenceMng.checkLicence();
	licenceMng.refreshStatus();


	printf(charCodec::tds_to_gb(licenceMng.m_licenceStatus + "\r\n").c_str());
	

	while(licenceMng.m_leftTrialTime > 0)
	{
		timeopt::sleepMilli(1000);
		licenceMng.refreshStatus();
		if (!licenceMng.m_bCanUse) {
			licenceMng.m_leftTrialTime -= 1;
		}
		else {
			licenceMng.m_leftTrialTime = 60*60;
		}
		json j;
		j["valid"] = licenceMng.m_bCanUse;
		j["info"] = licenceMng.m_licenceStatus;
		j["leftTrialTime"] = licenceMng.m_leftTrialTime;
		tds->rpcServer->setLicenceStatus(j);
	}

	tds->stop();
	//LOG("[warn]试用版运行时间到，退出程序");
	exit(0);
}

LicenceMng::LicenceMng()
{
	m_authPtCount = 0;
	m_validLicence = false;
	m_bCanUse = false;
	m_leftTrialTime = 60 * 60;
	m_licenceType = NONE;
	m_hasTimeLimit = false;
	thread t(checkLicenceThread);
	t.detach();
}


bool LicenceMng::checkLicence()
{
	//读取证书
	string s;
	if (!fs::readFile(fs::appPath() + "/licence.json", s))
	{
		cout << charCodec::gb_to_utf8("没有找到授权证书licence.json") << endl;
		return false;
	}
		

	json j = json::parse(s);
	string licenceNo = j["证书编号"];
	j.erase("证书编号");

	string codeToCheck;
	if (j.contains("激活码"))
	{
		codeToCheck = j["激活码"].get<string>();

		if (j.contains("试用期限")) {
			string time = j["试用期限"];
			m_hasTimeLimit = true;
			m_tryTimeLimit = timeopt::str2st(time);
		}

		if (codeToCheck == "")
		{
			cout << charCodec::utf8_to_gb("证书无效,缺少激活码") << endl;
			return false;
		}
	}
	else
	{
		cout << charCodec::utf8_to_gb("证书无效,缺少激活码") << endl;
		return false;
	}
	j.erase("激活码");


	if (j.contains("授权给")) {
		m_company = j["授权给"];
	}

	//企业一次性授权许可，可以自由复制，没有证书编号
	if (m_company != "") {
		string code = getCompanyActivateCode(j);

		if (code == codeToCheck)
		{
			cout << charCodec::utf8_to_gb("激活码有效") << endl;
			m_validLicence = true;
			return true;
		}
	}
	//按点位授权，需要每台设备授权
	else {
		m_authPtCount = j["IO点"].get<long>();
		//使用机器指纹和授权信息组合license信息
		string serial = getFingerPrint();
		j["证书编号"] = serial;
		string code = getActivateCode(j);

		cout << charCodec::utf8_to_gb("设备编号:") << serial << endl;

		if (code == codeToCheck)
		{
			cout << charCodec::utf8_to_gb("证书编号:")<< licenceNo <<charCodec::utf8_to_gb(",激活码有效") << endl;
			m_validLicence = true;
			return true;
		}
	}

	cout << charCodec::utf8_to_gb("证书编号:") << licenceNo << charCodec::utf8_to_gb(",激活码无效") << endl;
	m_validLicence = false;
	return false;
}


void LicenceMng::refreshStatus()
{
	m_confPtCount = tds->ioServer->getBindedChanCount();

	string title = "IO点数:" + str::fromInt(m_confPtCount);
	if (m_validLicence) {
		if (m_company == "") {
			title += "|专业版," + str::fromInt(licenceMng.m_authPtCount) + "点";

			if (m_hasTimeLimit) {
				title += ",试用期限:" + m_tryTimeLimit.toDateStr();
			}


			if (m_confPtCount <= licenceMng.m_authPtCount && !m_hasTimeLimit) {
				m_bCanUse = true;
			}
			else if (m_confPtCount <= licenceMng.m_authPtCount && m_hasTimeLimit && timeopt::CalcTimePassSecond(m_tryTimeLimit) < 0) {
				m_bCanUse = true;
			}
			else
			{
				string s = str::format("|点数超限,服务将在%d分%d秒后退出", m_leftTrialTime / 60, m_leftTrialTime % 60);
				title += s;
				m_bCanUse = false;
			}
		}
		else {
			title += "|企业版,授权给:" + m_company;
			m_bCanUse = true;
		}
	}
	else
	{
		title += "|试用版,50点";
		if (m_confPtCount <= 50) {
			m_bCanUse = true;
		}
		else
		{
			string s = str::format("|点数超限,服务将在%d分%d秒后退出", m_leftTrialTime / 60, m_leftTrialTime % 60);
			title += s;
			m_bCanUse = false;
		}
	}


	
	m_licenceStatus = title;
	string t = tds->m_sTitle + title;
	SetConsoleTitleW(charCodec::utf8_to_utf16(t).c_str());
}

