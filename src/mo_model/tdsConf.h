#pragma once
#include <string>
using namespace std;
#include "json.hpp"
using json = nlohmann::json;
using namespace std;

//设计考虑。 不使用json作为配置文件 参考https://www.lucidchart.com/techblog/2018/07/16/why-json-isnt-a-good-configuration-language/
/*
json作为配置文件的缺点
缺乏注释，
过于严格，
低信噪比（多余字符很无用，如键的引号其实是多余的，花括号将整个文档包围起来等）
不支持多行字符串（在字符串中换行，必须使用“\n”进行转义，想要一个字符串在文件中另起一行显示，那就彻底没办法了）
中文引号和英文引号容易打错，造成解析出错

在够用的情况下，ini是最方便的配置语言，去掉section让他变得更简单
*/

/*
运行参数可以通过命令行参数或者配置文件指定。

优先使用命令行
其次使用配置文件
否则使用默认值
*/

struct TDS_CONF_ITEM {
	string key;
	string val;
};

class TDS_INI {
public:
	TDS_INI() {};
	bool load(string path);
	int getValInt(string key, int defaultVal);
	string getValStr(string key, string defaultVal);
	map<string, string> mapConf;
};

class tdsConfig : public iTDSConf
{
public:
	tdsConfig();
	void generateDefaultConfFile(string m);
	string defaultConf_httpServer();
	string defaultConf_tds();
	string defaultConf_tdb();
	string defaultConf_rphttp();
	void loadConf_httpServer(vector<TDS_CONF_ITEM>& vecConf);
	void loadConf_tds(vector<TDS_CONF_ITEM>& vecConf);
	void loadConf_rphttp(vector<TDS_CONF_ITEM>& vecConf);
	void loadConf();
	json toJson();
	
	bool checkKey(string toCheck, string key);
	string normalizationKey(string key);

	TDS_INI tdsIni;

	int getInt(string key, int iDef) override;
	string getStr(string key, string sDef) override;

	json jsonConf;
	string m_confFileName;
};

