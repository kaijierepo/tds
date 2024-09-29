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
			tci.key = str::trim(tci.key, "\t");//去除键值对中间的tab,如果路径内存在tab这个会被读为\t,然后导致创建文件夹失败.
			tci.val = str::trim(tci.val, "\t");
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

	std::regex pattern_1((key + "\\s*=\\s*\\n"));				//使用正则表达式以"key"和"="作为关键词匹配其等号后的值，此种情况为没有值,直接匹配到换行符的情况
	std::regex pattern_2((key + "\\s*=\\s*\\s#"));				//使用正则表达式以"key"和"="作为关键词匹配其等号后的值，此种情况为没有值,直接匹配到"#"的情况
	std::regex pattern_3((key + "\\s*=\\s*([^\\s#\\n]+)"));		//使用正则表达式以"key"和"="作为关键词匹配其等号后的值，并避免匹配到"#"或回车符号之后的内容
	std::smatch matches;
	if (std::regex_search(m_strConf, matches, pattern_1) || std::regex_search(m_strConf, matches, pattern_2))
	{
		string str = matches[0].str();
		string matchedString = "=" + s;
		std::string result_1 = str::replace(str, "=", matchedString);
		std::string result = str::replace(m_strConf, str, result_1);
		m_strConf = result;
	}
	else if (std::regex_search(m_strConf, matches, pattern_3))
	{
		string str = matches[0].str();
		std::string matchedString = matches[1].str();
		std::string result_1 = str::replace(str, matchedString, s);
		std::string result = str::replace(m_strConf, str, result_1);
		m_strConf = result;
	}
	else
	{
		m_strConf = m_strConf + "\n" + key + "=" + s;
	}

	save(m_path);
}

void KV_INI::setVal(string key, string val)
{
	mapConf[key] = val;

	std::regex pattern_1((key + "\\s*=\\s*\\n"));				//使用正则表达式以"key"和"="作为关键词匹配其等号后的值，此种情况为没有值,直接匹配到换行符的情况
	std::regex pattern_2((key + "\\s*=\\s*\\s#"));				//使用正则表达式以"key"和"="作为关键词匹配其等号后的值，此种情况为没有值,直接匹配到"#"的情况
	std::regex pattern_3((key + "\\s*=\\s*([^\\s#\\n]+)"));		//使用正则表达式以"key"和"="作为关键词匹配其等号后的值，并避免匹配到"#"或回车符号之后的内容
	std::smatch matches;
	if (std::regex_search(m_strConf, matches, pattern_1)  || std::regex_search(m_strConf, matches, pattern_2))
	{
		string str = matches[0].str();
		string matchedString = "=" + val;
		std::string result_1 = str::replace(str, "=", matchedString);
		std::string result = str::replace(m_strConf, str, result_1);
		m_strConf = result;
	}
	else if (std::regex_search(m_strConf, matches, pattern_3))
	{
		string str = matches[0].str();
		std::string matchedString = matches[1].str();
		std::string result_1 = str::replace(str, matchedString, val);
		std::string result = str::replace(m_strConf, str, result_1);
		m_strConf = result;
	}	
	else
	{
		m_strConf = "\n" + m_strConf + key + "=" + val;
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
