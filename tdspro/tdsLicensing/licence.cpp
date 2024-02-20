#include "licence.h"
#include <Windows.h>
#include <string>
#include <list>
#include <iostream>
#include <functional>
#include "common.h"

using namespace std;
#define BUFFER_SIZE 128

namespace Licence {
	bool getDevcieInfo(const char* cmd, string& result) {
		bool ret = false;
		FILE* pipe = _popen(cmd, "r"); //打开管道，并执行命令 
		if (!pipe)
			return ret;

		bool isOk = false;
		while (!feof(pipe))
		{
			char buffer[BUFFER_SIZE] = { 0 };
			if (fgets(buffer, BUFFER_SIZE, pipe))
			{
				string getted = buffer;
				if (getted.find("UUID") == 0
					|| getted.find("ProcessorId") == 0
					|| getted.find("SerialNumber") == 0) // 能够正确获取信息
				{
					isOk = true;
					continue;
				}
				if (isOk == false
					|| strcmp("\r\n", buffer) == 0) //去掉windows无用的空行
				{
					continue;
				}
				ret = true;
				string s = str::trim(getted, "\r\n");
				s = str::trim(s);
				if (result == "")
					result = s;
				else
					result += "," + s;
			}
		}
		_pclose(pipe); // 关闭管道 
		return ret;
	}
	string getDeviceFingerPrint() {
		string sAll;
		string uuid;
		// 主板UUID存在，就使用主板UUID生成机器指纹
		if (getDevcieInfo("wmic csproduct get UUID", uuid))
		{
			sAll += "Mainboard UUID:" + uuid + "\r\n";
		}

		string processorid;
		if (getDevcieInfo("wmic cpu get processorid", processorid)) {
			sAll += "Processor ID:" + processorid + "\r\n";;
		}

		string biosSerial;
		if (getDevcieInfo("wmic bios get serialnumber", biosSerial)) {
			sAll += "BIOS Serial Num:" + biosSerial + "\r\n";;
		}

		string diskSerialNum;
		if (getDevcieInfo("wmic diskdrive get serialnumber", diskSerialNum)) {
			sAll += "Disk Serial Num:" + diskSerialNum + "\r\n";;
		}

		return sAll;
	}
	string getLicenceSerial() {
		string sAll;
		string uuid;
		// 主板UUID存在，就使用主板UUID生成机器指纹
		if (getDevcieInfo("wmic csproduct get UUID", uuid))
		{
			sAll += uuid;
		}

		string processorid;
		if (getDevcieInfo("wmic cpu get processorid", processorid)) {
			sAll += "," + processorid;
		}

		hash<string> hasher;
		size_t hash = hasher(sAll);
		string serial = str::format("%llu", hash);
		serial += "00000000";
		serial = serial.substr(0, 8);

		return serial;
	}
	string getFingerPrint()
	{
		return getLicenceSerial();
	}
	string getActivateCode(json j)
	{
		string licenceData = j.dump() + "TDS666";
		hash<string> hasher;
		size_t hash = hasher(licenceData);
		string code = str::format("%llu", hash);
		code = code + "000000";
		code = code.substr(0, 6);
		return code;
	}
	string getCompanyActivateCode(json j)
	{
		string licenceData = j.dump() + "TDS666";
		hash<string> hasher;
		size_t hash = hasher(licenceData);
		string code = str::format("%llu", hash);
		code = code + "000000";
		code = code.substr(0, 6);
		return code;
	}
	void createLicence()
	{
		json j;
		j["IO点"] = 10000;
		j["证书编号"] = getLicenceSerial();
		j["激活码"] = "";
		string s = j.dump(4);
		s = str::replace(s, "\n", "\r\n");
		string path = fs::appPath() + "/licence.json";
		if (!fs::fileExist(path))
		{
			fs::writeFile(path, s);
		}
	}
	void writeCodeToLicence()
	{
		string s;
		if (!fs::readFile(fs::appPath() + "/licence.json", s))
			return;

		json j = json::parse(s);
		j.erase("激活码");

		if (j.contains("证书编号"))
		{
			//根据证书编号和授权信息，生成激活码
			string code = getActivateCode(j);
			j["激活码"] = code;
			s = j.dump(4);
			s = str::replace(s, "\n", "\r\n");
			fs::writeFile(fs::appPath() + "/licence.json", s);
		}
		else if (j.contains("授权给")) {
			string code = getCompanyActivateCode(j);
			j["激活码"] = code;
			s = j.dump(4);
			s = str::replace(s, "\n", "\r\n");
			fs::writeFile(fs::appPath() + "/licence.json", s);
		}
		else {
			cout << charCodec::utf8_to_gb("必须输入证书编号") << endl;;
		}
		return;
	}

}

