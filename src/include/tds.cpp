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

string TAG::trimPrefix(string s, string prefix)
{
	if (prefix == "")
		return s;

	while (1)
	{
		if (s.find(prefix) == 0)
		{
			s = s.substr(prefix.length(), s.length() - prefix.length());
		}
		else
		{
			break;
		}
	}

	return s;
}

string TAG::trimRoot(string tag, string root)
{
	if (root == "")
		return tag;

	tag = trimPrefix(tag, root);
	tag = trimPrefix(tag, ".");
	return tag;
}

string TAG::getParentTag(string tag)
{
	size_t pos = tag.rfind(".");
	if (pos >= 0) {
		tag = tag.substr(0, pos);
	}
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


int TAG::split(std::vector<std::string>& dst, const std::string& src, std::string separator)
{
	if (src.empty() || separator.empty())
		return 0;

	int nCount = 0;
	std::string temp;
	size_t pos = 0, offset = 0;

	// 分割第1~n-1个
	while ((pos = src.find(separator, offset)) != std::string::npos)
	{
		temp = src.substr(offset, pos - offset);
		if (temp.length() > 0) {
			dst.push_back(temp);
			nCount++;
		}
		else
		{
			dst.push_back("");
			nCount++;
		}
		offset = pos + separator.size();
	}

	// 分割第n个
	temp = src.substr(offset, src.length() - offset);
	if (temp.length() > 0) {
		dst.push_back(temp);
		nCount++;
	}

	return nCount;
}

bool TAG::hasTag(json& tree, string tag)
{
	vector<string> nodeNames;
	split(nodeNames, tag, ".");
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
string TAG::resolveTag(string strTagExp, string tagContext)
{
	string tagName = strTagExp;


	if (strTagExp.find("..") == 0) { //上一级位号
		string tagContextParent;
		size_t pos = tagContext.rfind(".");
		if (pos >= 0) {
			tagContextParent = tagContext.substr(0, pos);
		}


		tagName = trimPrefix(strTagExp, "..");

		if (tagContextParent != "") {
			tagName = tagContextParent + "." + tagName;
		}
	}
	else if (strTagExp.find(".") == 0) //绝对位号
	{
		tagName = trimPrefix(strTagExp, ".");
	}
	else {//相对于环境位号的位号
		tagName = TAG::addRoot(strTagExp, tagContext);
	}

	return tagName;
}

