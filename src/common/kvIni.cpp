#include "kvIni.h"
#include "common.h"
#include <regex>

bool KV_INI::load(string path)
{
	m_path = path;
	//配置文件当中的值  如果有值，说明是命令行设置，命令行优先级最高
	fs::readFile(path, m_strConf);
	vector<string> confItems;
	str::split(confItems, m_strConf, "\n");

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
	fs::writeFile(path, m_strConf);
	return true;
}

void KV_INI::setVal(string key, int val)
{
	string s = str::fromInt(val);
	mapConf[key] = s;

	// 使用正则表达式以"key"和"="作为关键词匹配其等号后的值，并避免匹配到"#"或回车符号之后的内容
	std::regex pattern((key + "\\s*=\\s*([^\\s#\\n]+)"));
	std::smatch matches;
	//适配到就更改
	if (std::regex_search(m_strConf, matches, pattern)) {
		std::string matchedString = matches[1].str();
		std::string result = std::regex_replace(m_strConf, std::regex(matchedString), s);
		m_strConf = result;
	}
	else//找不到就加上
	{
		m_strConf = "\n" + m_strConf + key + "=" + s + "\n";
	}

	save(m_path);
}

void KV_INI::setVal(string key, string val)
{
	mapConf[key] = val;

	// 使用正则表达式以"key"和"="作为关键词匹配其等号后的值，并避免匹配到"#"或回车符号之后的内容
	std::regex pattern((key + "\\s*=\\s*([^\\s#\\n]+)"));
	std::smatch matches;
	if (std::regex_search(m_strConf, matches, pattern)) {
		string str= matches[0].str();
		std::string matchedString = matches[1].str();
		std::string result_1 = str::replace(str, matchedString, val);
		std::string result = str::replace(m_strConf, str, result_1);
		m_strConf = result;
	}
	else
	{
		m_strConf = "\n" +m_strConf + key + "=" + val;
	}

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
