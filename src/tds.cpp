/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu  

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include "tds.h"
#include "common.h"

i_tds* tds = nullptr;

string TAG::trimRoot(string tag, string root)
{
	if (root == "")
		return tag;

	tag = str::trimPrefix(tag, root);
	tag = str::trimPrefix(tag, ".");
	return tag;
}

string TAG::userTag2sysTag(string userTag, string userOrg)
{
	return TAG::addRoot(userTag, userOrg);
}

string TAG::sysTag2userTag(string sysTag, string userOrg)
{
	return TAG::trimRoot(sysTag, userOrg);
}


string TAG::addRoot(string tag, string root)
{
	if (root == "")
		return tag;

	//tag是相对于root的相对位号
	if (tag == "")
		return root;

	return root + "." + tag;
}

bool TAG::hasTag(json& tree, string tag)
{
	vector<string> nodeNames;
	str::split(nodeNames, tag, ".");
	json* node = &tree;
	for (int i = 0; i < nodeNames.size(); i++)
	{
		string name = nodeNames[i];


		//查找子节点中有没有是 指定name的节点。如果有node指向该节点，继续查找node的子节点中是否有下一个name
		if ((*node)["children"] == nullptr)
			return false;
		json& jChildren = (*node)["children"];
		bool bHaveChild = false;
		if (jChildren.is_array())//具体指定
		{
			for (int j = 0; j < jChildren.size(); j++)
			{
				json& child = jChildren[j];
				if (child["name"].get<string>() == name)
				{
					bHaveChild = true;
					node = &child;
				}
			}
		}
		else if (jChildren.is_string() && jChildren.get<string>() == "*") //通配符指定，所有子节点
		{
			return true;
		}

		if (!bHaveChild)return false;
	}

	return true;
}

size_t TAG::getMoLevel(string tag)
{
	tag = TAG::addRoot(tag, "root");
	return std::count(tag.begin(), tag.end(), '.');
}

json TAG::mapTree2List(json mapTree)
{
	for (auto& [k, v] : mapTree.items())
	{

	}

	return json();
}

//tagThis当前位号
//strTagExp相对与当前位号的相对位号表达式
string TAG::resolveTag(string strTagExp, string tagThis)
{
	string tagName = strTagExp;
	//this的解析，this后面可能带 .std 等后缀
	if (strTagExp.find("this") != string::npos)
	{
		tagName = str::replace(tagName, "this", tagThis);
	}
	//解析仅名字的情况，等效于 ./XXX（使用当前监测点的父监测对象组成完整名字）
	else if (strTagExp.find(".") == string::npos && strTagExp.find("*") == string::npos && tagThis != "")
	{
		string strTagContext; //父监测对象的tag
		size_t iPos = tagThis.rfind('.');
		if (iPos <= 0)return "";
		strTagContext = tagThis.substr(0, iPos);
		tagName = strTagContext + "." + strTagExp;
	}
	//使用相对位号的格式 ./或者../ ,./表示环境位号（父mo的位号）,../表示环境位号向上一级
	else if (strTagExp.find("./") != string::npos || strTagExp.find(".\\") != string::npos || strTagExp.find("..") != string::npos)
	{
		//替换../   ../必须也只能写前边
		string strTagContext; //父监测对象的tag
		size_t iPos = tagThis.rfind('.');
		if (iPos <= 0)return "";
		strTagContext = tagThis.substr(0, iPos);
		string tag = strTagContext;
		string rtag = strTagExp;
		//先规范化 替换\为/  替换\\为/  
		rtag = str::replace(rtag, "\\", "/");
		rtag = str::replace(rtag, "\\\\", "/");

		while (1) {
			size_t ipos = rtag.find("../");
			if (ipos != string::npos) {
				size_t dotPos = tag.rfind(".");
				if (dotPos != string::npos) {
					tag = tag.substr(0, dotPos);
				}
				else {
					return "";
				}
				rtag = rtag.substr(ipos + 3);
			}
			else {
				break;
			}
		}
		//替换./
		rtag = str::replace(rtag, "./", "");

		if (rtag.length() > 0)
			tag = tag + "." + rtag;
		tagName = tag;
	}
	//位号全名
	else
	{
		if (strTagExp.find(".") == string::npos)//仅指定name
		{
			tagName = TAG::addRoot(tagName, tagThis);
		}
		else
			tagName = strTagExp;
	}

	return tagName;
}

i_tds* getITDS() {
	HMODULE hMod = LoadLibrary("tds.dll");
	if (hMod)
	{
		fp_getTds pGetTds = (fp_getTds)GetProcAddress(hMod, "getTds");
		if (pGetTds)
		{
			return pGetTds();
		}
	}
	return NULL;
}