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
		string ci = confItems[i];
		KV_INI_LINE oneLine;
		if (ci.find("#") == -1 && ci.find("=") == -1)
		{
			oneLine.type = BLANK;
			mapConf.push_back(oneLine);
			continue;
		}
		else
		{
			size_t pos = ci.find("#");
			if (pos != string::npos)
			{
				oneLine.note = ci.substr(pos + 1);
				ci = ci.substr(0, pos);
				oneLine.note = str::trim(oneLine.note, "\r");
			}

			pos = ci.find("=");
			if (pos != string::npos)
			{
				oneLine.key = ci.substr(0, pos);
				oneLine.val = ci.substr(pos + 1, ci.length() - pos - 1);

				oneLine.val = str::trim(oneLine.val, "\r");
				oneLine.key = str::trim(oneLine.key, "\t");//去除键值对中间的tab,如果路径内存在tab这个会被读为\t,然后导致创建文件夹失败.
				oneLine.val = str::trim(oneLine.val, "\t");
				oneLine.key = str::trim(oneLine.key, " ");
				oneLine.val = str::trim(oneLine.val, " ");
			}

			if (oneLine.key != "")
			{
				oneLine.type = CONF_ITEM;
				mapConf.push_back(oneLine);
				continue;
			}
			else
			{
				oneLine.type = NOTE;
				mapConf.push_back(oneLine);
				continue;
			}
		}
	}

	return true;
}

bool KV_INI::save(string path)
{
	string temp = "";
	for (auto& line : mapConf)
	{
		if (line.type == NOTE)
		{
			temp += ("#" + line.note + "\n");
		}
		else if (line.type == CONF_ITEM)
		{
			temp += (line.key+"="+line.val);
			if (line.note != "")
			{
				temp += ("\t\t\t\t\t\t#" + line.note);
			}
			temp += "\n";
		}
		else if (line.type== BLANK)
		{
			temp += "\n";
		}
	}

	m_strConf = temp;
	fs::writeFile(path, m_strConf);
	return true;
}

void KV_INI::setVal(string key, int val)
{
	string s = str::fromInt(val);
	setVal(key, s);
}

void KV_INI::setVal(string key, string val)
{
	bool exist = false;
	for (auto& line : mapConf) {
		if (line.type == CONF_ITEM) {
			if (line.key == key) {
				line.val = val;
				exist = true;
				break;
			}
		}
	}

	if (!exist) {
		KV_INI_LINE line;
		line.type = CONF_ITEM;
		line.key = key;
		line.val = val;
		mapConf.push_back(line);
	}
	//序列化成文件
	save(m_path);
}

int KV_INI::getValInt(string key, int defaultVal)
{
	bool bFind = false;
	int iResult = 0;
	for (auto& line : mapConf) {
		if (line.type == CONF_ITEM) {
			if (line.key == key) {
				iResult = atoi(line.val.c_str());
				bFind = true;
				break;
			}
		}
	}

	if (bFind)
	{
		return iResult;
	}
	else
		return defaultVal;
}

string KV_INI::getValStr(string key, string defaultVal)
{
	bool bFind = false;
	string strResult = "";
	for (auto& line : mapConf) {
		if (line.type == CONF_ITEM) {
			if (line.key == key) {
				strResult = line.val;
				bFind = true;
				break;
			}
		}
	}

	if (bFind)
	{
		return strResult;
	}
	else
		return defaultVal;
}
