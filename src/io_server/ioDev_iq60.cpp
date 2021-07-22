#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_iq60.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
using namespace httplib;

map<string, ioDev_iq60*> g_mapIQ60;


void onRecvIQ60Pkt(char* pData, int iLen)
{
	char* p = new char[iLen + 1];
	memset(p, 0, iLen + 1);
	memcpy(p, pData, iLen);
	string pkt = p;
	delete p;

	try {
		json jpkt = json::parse(pkt);

		if (jpkt.is_array()&&jpkt.size()>=1)
		{
			//转发给对应设备
			string id = jpkt[0];
			if (g_mapIQ60.find(id) != g_mapIQ60.end())
			{
				ioDev_iq60* p = g_mapIQ60[id];
				p->onRecvPkt(jpkt);
			}	
		}
	}
	catch(std::exception& e)
	{
		string errorType = e.what();
		string log = "pkt from iq60,json parse error. " + errorType;
		LOG(log);
	}
}


ioDev_iq60::ioDev_iq60()
{
	
}

ioDev_iq60::~ioDev_iq60()
{
	g_mapIQ60.erase(m_addr);
}

bool ioDev_iq60::onRecvPkt(json jPkt)
{
	json jLast  = jPkt[jPkt.size() - 1];


	if (jLast.is_string())
	{
		string cmd = jLast.get<string>();
		/*
		*r  【读】数据
		请求：
			[版本,验证TOKEN,物云名,r指令,点1,点2,点3]
			[2,"IQK","C1201020756","r","AI9","BO1"]
		返回：
			[物云名,[点1,值,时间戳,状态],[点2,值,时间戳,状态],r指令]
			["C1201020756",["AI9",48.8,1540697466,0],["BO1",1,1540697466,0],"r"]
			状态0:在线  状态1:断线
		*/
		if (cmd == "r")
		{


		}
		else if (cmd == "w")
		{

		}
	}
	else//主动数据上报命令
	{
		for (int i = 1; i < jPkt.size();i++)
		{
			json point = jPkt[i];
			string name = point[0].get<string>();
			float val = point[1].get<float>();
			int time = point[2].get<int>();
			int status = point[3].get<int>();

			ioDev* pChild = getChild(name);
			if (pChild->m_level == "channel")
			{
				ioChannel* pC = (ioChannel*)pChild;
				pC->inputVal(val);
				return true;
			}
		}
	}
	return false;
}

bool ioDev_iq60::getCurrentVal()
{
	return false;
}