#include "xiaot.h"
#include "pch.h"
#include "prj.h"
#include "logger.h"
#include "mp.h"
#include "mo.h"

CXiaoT xiaot;


bool CXiaoT::init()
{
	string conf;
	if (!fs::readFile(tds->conf->projectConfPath + "\\xiaot\\xiaot.json", conf))
	{
		//LOG("project conf xiaot.json not find,use empty config!");
		return true;
	}

	try {
		json jXiaot = json::parse(conf.c_str());
		for (int i = 0; i < jXiaot.size(); i++)
		{
			json qaTemp = jXiaot.at(i);
			json qList = qaTemp[0];
			json aList = qaTemp[1];
			json a = aList[0];
			for (int j = 0; j < qList.size(); j++)
			{
				json q = qList[j];
				brain[q.get<string>()] = a.get<string>();
			}
		}
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
		return false;
	}
}


std::string CXiaoT::getReply(json msg)
{
	string text = msg["text"];
	string reply = "";

	//问答类处理
	for (auto& it : brain)
	{
		if (text.find(it.first) != string::npos)
		{
			reply = it.second;
		}
	}

	//控制类处理
	json val;
	if (reply == "")
	{
		string tag = "";
		if (text.find("打开") != string::npos)
		{
			val = true;
			tag = str::trim(text, "打开");	
		}
		else if (text.find("关闭") != string::npos)
		{
			val = false;
			tag = str::trim(text, "关闭");
		}

		if (!val.empty())
		{
			MP* pmp = prj.GetMPByName(tag);
			if (!pmp)
			{
				reply = "没找到需要控制的设备";
			}
			else
			{
				pmp->outputVal(val);
				reply = "好的";
			}
		}
	}
	


	if (reply == "")
	{	
		reply = "嗯嗯";
	}


	string from = msg["from"];
	str::replace(reply, "{{talker.from}}", from);
	json jReply;
	jReply["text"] = reply;
	jReply["to"] = from;
	return jReply.dump();
}