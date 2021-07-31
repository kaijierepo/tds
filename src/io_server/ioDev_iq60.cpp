#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_iq60.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"

using namespace httplib;

map<string, ioDev_iq60*> g_mapIQ60;


void onRecvIQ60Pkt(char* pData, int iLen,std::shared_ptr<TDS_SESSION> pALC)
{
	char* p = new char[iLen + 1];
	memset(p, 0, iLen + 1);
	memcpy(p, pData, iLen);
	string pkt = p;
	delete p;

	LOG("收到IQ60数据包: " + pkt);

	vector<string> aryPkt;
	str::split(aryPkt,pkt,"\n");

	//分开粘连包
	for (int i = 0; i < aryPkt.size(); i++)
	{
		string pktData = aryPkt[i];
		try {
			json jpkt = json::parse(pktData);

			if (jpkt.is_array() && jpkt.size() >= 1)
			{
				//转发给对应设备
				string id = jpkt[0];
				if (g_mapIQ60.find(id) != g_mapIQ60.end())
				{
					ioDev_iq60* p = g_mapIQ60[id];
					p->ioSession = pALC;
					p->onRecvPkt(jpkt);
				}
				//设备上线功能
				else
				{
					ioDev_iq60* p = new ioDev_iq60();
					p->m_addr = id;
					g_mapIQ60[id] = p;
					ioSrv.m_vecChild.push_back(p);
					json j;
					p->toJson(j);
					tdsSrv.notify("io.devDiscovered", j);
				}
			}
		}
		catch (std::exception& e)
		{
			string errorType = e.what();
			string log = "pkt from iq60,json parse error. " + errorType;
			LOG(log);
		}
	}
}


ioDev_iq60::ioDev_iq60()
{
	m_devType = TDS::IO_DEV_TYPE::DEV::iq60_gateway;
	m_devTypeLabel = IO_DEV_TYPE_LABEL.at(m_devType);
	m_parentDevType = "tds";
	m_channelType = "io-point";
	m_channelTypeLabel = "IO点";
	m_level = "device";
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

		if (cmd.find(currentCmd)!=string::npos)
		{
			currentResp.push_back(jPkt);
			if (cmd.find("-") == string::npos) //结束包
			{
				getResponse = true;
			}
			return true;
		}
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
		/*
		2、【搜】对象，hs指令：
		请求：
			[版本, 验证TOKEN, 物云名, hs指令]
			[2, "IQK", "C1201020756", "hs"]
		返回：
			[物云名, 点1, 点2, 点3, hs指令]]
			["C1201020756", "AI9", "AO8", "BI1", "RI3", "FR1", "FA2", "BO1", "RH496", "hs"]
		*/
		else if (cmd == "hs")
		{
			
		}
		/*
		3、【读】对象，hr指令：
		请求：
			[版本, 验证TOKEN, 物云名, hr指令, 点1, 点2, 点3]
			[2, "IQK", "C1201020756", "hr", "BO1", "AI9"]
		返回：["C1201020756",
		      { "Name":"BO1","COV" : 1,"Enable" : 1,"ValueType" : "bool","RW" : "rw","Unit" : "关:0,开:1" },
			  { "Name":"AI9","DisplayName" : "CPU温度","COV" : 0.5,"Enable" : 1,"ValueType" : "float","RW" : "ro","Unit" : "℃" },
			  "hr"]
			[物云名, { 键1:值,键2 : 值,键3 : 值 }, { 键1:值,键2 : 值,键3 : 值 }, hr指令]
		*/
		else if (cmd == "hr")
		{
			
		}
	}
	else//主动数据上报命令
	{
		for (int i = 1; i < jPkt.size();i++)
		{
			string valType = "real";
			json point = jPkt[i];
			string name = point[0].get<string>();
			double dbVal;
			bool bVal;
			if (name.find("B") == 0)
			{
				valType = "bool";
				bVal = point[1].get<int>() == 1?true:false;
			}
			else
			{
				dbVal = point[1].get<double>();
			}
			
			int time = point[2].get<int>();
			int status = point[3].get<int>();

			ioDev* pChild = getChild(name);
			if (pChild && pChild->m_level == "channel")
			{
				ioChannel* pC = (ioChannel*)pChild;
				if(valType == "real")
					pC->inputVal(dbVal);
				else 
					pC->inputVal(bVal);
			}
		}
	}
	return false;
}

bool ioDev_iq60::getCurrentVal()
{
	return false;
}

bool ioDev_iq60::waitResponse(int timeout)
{
	while (timeout > 0)
	{
		Sleep(50);
		timeout -= 50;
		if (getResponse)
			return true;
	}
	return false;
}

bool ioDev_iq60::scanChannel(json& chanList)
{
	if (ioSession == NULL)
		return false;

	currentCmd = "hs";
	currentResp.clear();
	getResponse = false;

	string req = "[2,\"IQK\",\"" + m_addr + "\",\"hs\"]";
	ioSession->send((char*)req.c_str(), req.length());


	if (waitResponse(5000))
	{
		for (int i = 0; i < currentResp.size(); i++)
		{
			json jsubPkt = currentResp.at(i);
			for (int j = 1; j < jsubPkt.size() - 1; j++)
			{
				string ptName = jsubPkt[j];
				json jChan;
				jChan["addr"] = ptName;
				chanList.push_back(jChan);
			}
		}
		return true;
	}
	else
	{
		return false;
	}


	/*
	if (waitResponse(1000))
	{
		json req;
		req.push_back(2);
		req.push_back("IQK");
		req.push_back(m_addr);
		req.push_back("hr");
		for (int i = 0; i < currentResp.size(); i++)
		{
			json jsubPkt = currentResp.at(i);
			for (int j = 1; j < jsubPkt.size() - 1; j++)
			{
				req.push_back(jsubPkt[i]);
			}
		}
		string strReq = req.dump();

		getResponse = false;
		currentResp.clear();
		currentCmd = "hr";

		ioSession->send((char*)strReq.c_str(), strReq.length());
	}
	else
	{
		return false;
	}

	
	if (waitResponse(5000))
	{
		for (int i = 0; i < currentResp.size(); i++)
		{
			json jsubPkt = currentResp.at(i);
			for (int j = 1; j < jsubPkt.size() - 1; j++)
			{
				json jPt = jsubPkt[j];
				json jChan;
				jChan["addr"] = jPt["name"];
				string valType = jPt["ValueType"].get<string>();
				if (valType == "float")
					jChan["valType"] = "real";
				else if (valType == "bool")
					jChan["valType"] = "bool";
				chanList.push_back(jChan);
			}
		}

		currentResp.clear();
		currentCmd = "";
		return true;
	}
	else
	{
		return false;
	}*/

	return false;
}

json ioDev_iq60::getAddr()
{
	json j;
	j["gateway_id"] = m_addr;
	return j;
}
