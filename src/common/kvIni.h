/*
key value Ini配置文件
仅支持Key,value和#注释的最简配置文件

设计思路：
 * 为何不使用json作为配置文件？ 参考https://www.lucidchart.com/techblog/2018/07/16/why-json-isnt-a-good-configuration-language/
	json作为配置文件的缺点
	缺乏注释，
	过于严格，
	低信噪比（多余字符很无用，如键的引号其实是多余的，花括号将整个文档包围起来等）
	不支持多行字符串（在字符串中换行，必须使用“\n”进行转义，想要一个字符串在文件中另起一行显示，那就彻底没办法了）
	中文引号和英文引号容易打错，造成解析出错

 * 在够用的情况下，ini是最方便的配置语言，去掉section让他变得更简单
*/


#pragma once
#include <string>
#include <vector>
using namespace std;

enum LINE_TYPE
{
	NOTE = 0,
	CONF_ITEM = 1,
	BLANK = 2
};

struct KV_INI_LINE {
	LINE_TYPE type;
	string note = "";
	string key = "";
	string val = "";
};

struct KV_CONF_ITEM
{
	string key;
	string val;
};


class KV_INI {
public:
	KV_INI() {};
	bool load(string path);
	bool save(string path);
	void setVal(string key, int val);
	void setVal(string key, string val);
	int getValInt(string key, int defaultVal);
	string getValStr(string key, string defaultVal);
	vector<KV_INI_LINE> mapConf;
	string m_path;//ini路径
	string m_strConf;//ini所有内容
};

