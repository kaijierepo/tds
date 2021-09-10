#pragma once
#include <string>
using namespace std;
#include "json.hpp"
#include "tds.h"

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

class tdsConfig : public iTDSConf
{
public:
	tdsConfig();
	void loadConf();


	json jsonConf;
};

