#include "kvIni.h"
#include "common.h"

bool KV_INI::load(string path)
{
	m_path = path;
	//配置文件当中的值  如果有值，说明是命令行设置，命令行优先级最高
	string strConf;
	fs::readFile(path, strConf);
	vector<string> confItems;
	str::split(confItems, strConf, "\n");

	//去掉注释
	for (int i = 0; i < confItems.size(); i++)
	{
		string& ci = confItems[i];
		size_t pos = ci.find("#");
		if (pos != string::npos)
		{
			ci = ci.substr(0, pos);
		}
	}
	//解析
	vector<KV_CONF_ITEM> vecConf;
	for (int i = 0; i < confItems.size(); i++)
	{
		string& ci = confItems[i];
		KV_CONF_ITEM tci;
		size_t pos = ci.find("=");
		if (pos != string::npos)
		{
			tci.key = ci.substr(0, pos);
			tci.val = ci.substr(pos + 1, ci.length() - pos - 1);

			tci.val = str::trim(tci.val, "\r");
			tci.key = str::trim(tci.key, " ");
			tci.val = str::trim(tci.val, " ");

			mapConf[tci.key] = tci.val;
		}
	}
	return true;
}

bool KV_INI::save(string path)
{
	string s;
	for (auto& i : mapConf) {
		string item = i.first + "=" + i.second;
		s += item + "\r\n";
	}
	fs::writeFile(path, s);
	return true;
}

void KV_INI::setVal(string key, int val)
{
	string s = str::fromInt(val);
	mapConf[key] = s;
	save(m_path);
}

void KV_INI::setVal(string key, string val)
{
	mapConf[key] = val;
	save(m_path);
}

int KV_INI::getValInt(string key, int defaultVal)
{
	if (mapConf.find(key) == mapConf.end())
	{
		return defaultVal;
	}

	string sVal = mapConf[key];
	return atoi(sVal.c_str());
}

string KV_INI::getValStr(string key, string defaultVal)
{
	if (mapConf.find(key) == mapConf.end())
	{
		return defaultVal;
	}

	string sVal = mapConf[key];
	return sVal;
}
