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


void onRecvIQ60Pkt(char* pData, int iLen, std::shared_ptr<TDS_SESSION> pALC)
{
	char* p = new char[iLen + 1];
	memset(p, 0, iLen + 1);
	memcpy(p, pData, iLen);
	string pkt = p;
	delete p;

	LOG("收到IQ60数据包: " + pkt);


	try {
		json jpkt = json::parse(pkt);

		if (jpkt.is_array() && jpkt.size() >= 1)
		{
			//转发给对应设备
			string id = jpkt[0];
			ioDev* pIoDev = ioSrv.getIODev(id);
			if (pIoDev)
			{
				if (pIoDev->m_devType == IO_DEV_TYPE::DEV::iq60_gateway)
				{
					ioDev_iq60* p = (ioDev_iq60*)pIoDev;
					p->ioSession = pALC;
					p->onRecvPkt(jpkt);
					if (p->m_bOnline == false)
					{
						p->m_bOnline = true;
						json j;
						p->toJson(j);
						tdsSrv.notify("io.online", j);
					}
				}
				else
				{
					LOG("[error]%s iq60 online,but this addr is configured as not an iq60 dev", id);
				}
			}
			//设备发现功能
			else
			{
				ioDev_iq60* p = new ioDev_iq60();
				p->m_devAddr = id;
				p->m_mngStatus = IODEV_MNG_STATUS::spare;
				p->m_bOnline = true;
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
}

bool ioDev_iq60::onRecvPkt(json jPkt)
{
	json jLast = jPkt[jPkt.size() - 1];

	if (jLast.is_string())
	{
		string cmd = jLast.get<string>();

		if (cmd.find(currentCmd) != string::npos)
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
		/*
		w指令：
		请求：
		[版本, 验证TOKEN, 物云名, w指令, [点1, 值], [点2, 值], [点3, 值]]
		[2, "IQK", "C1201020756", "w", ["AO9", 5], ["BO4", 1]]
		返回：
		["C1201020756", ["AO9", 5, 1540697972, 0], ["BO4", 1, 1540697972, 0], "w"]
		[物云名, [点1, 值, 时间戳, 状态], [点2, 值, 时间戳, 状态], w指令]
		*/
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
		for (int i = 1; i < jPkt.size(); i++)
		{
			string valType = "real";
			json point = jPkt[i];
			string name = point[0].get<string>();
			double dbVal;
			bool bVal;
			if (name.find("B") == 0)
			{
				valType = "bool";
				bVal = point[1].get<int>() == 1 ? true : false;
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
				if (valType == "real")
					pC->input(dbVal);
				else
					pC->input(bVal);
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

bool ioDev_iq60::requestAndWaitResp(string cmd, string req)
{
	currentCmd = cmd;
	currentResp.clear();
	getResponse = false;

	if (ioSession == NULL)
		return false;

	ioSession->send((char*)req.c_str(), req.length());

	if (cmd == "hs")
	{
		if (waitResponse(5000))
		{
			return true;
		}
	}
	else if (cmd == "hr")
	{
		if (waitResponse(10000))
		{
			return true;
		}
	}
	else
	{
		if (waitResponse(10000))
		{
			return true;
		}
	}

	return false;
}

bool ioDev_iq60::scanChannel(json& chanList)
{
	if (ioSession == NULL)
		return false;

	//请求io点列表
	string req = "[2,\"IQK\",\"" + m_devAddr + "\",\"hs\"]";
	if (!requestAndWaitResp("hs", req))
		return false;

	//for (int i = 0; i < currentResp.size(); i++)
	//{
	//	json jsubPkt = currentResp.at(i);
	//	for (int j = 1; j < jsubPkt.size() - 1; j++)
	//	{
	//		string ptName = jsubPkt[j];
	//		json jChan;
	//		jChan["addr"] = ptName;
	//		chanList.push_back(jChan);
	//	}
	//}

	//获得需要请求详细信息的io点
	json jCmdHr;
	jCmdHr.push_back(2);
	jCmdHr.push_back("IQK");
	jCmdHr.push_back(m_devAddr);
	jCmdHr.push_back("hr");

	for (int i = 0; i < currentResp.size(); i++)
	{
		json jsubPkt = currentResp.at(i);
		for (int j = 1; j < jsubPkt.size() - 1; j++)
		{
			string ptName = jsubPkt[j];
			jCmdHr.push_back(ptName);
		}
	}
	req = jCmdHr.dump();
	if (!requestAndWaitResp("hr", req))
		return false;

	for (int i = 0; i < currentResp.size(); i++)
	{
		json jsubPkt = currentResp.at(i);
		for (int j = 1; j < jsubPkt.size() - 1; j++)
		{
			json jPt = jsubPkt[j];
			json jChan;
			jChan["addr"] = jPt["Name"];
			string valType = jPt["ValueType"].get<string>();
			if (valType == "float")
				jChan["valType"] = VAL_TYPE::real;
			else if (valType == "bool")
				jChan["valType"] = VAL_TYPE::boolean;
			else if (valType == "int")
				jChan["valType"] = VAL_TYPE::integer;
			else
				continue;
			jChan["valTypeLabel"] = VAL_TYPE_LABEL.at(jChan["valType"]);
			jChan["name"] = jPt["DisplayName"];
			if (jPt["RW"] == "rw")
			{
				jChan["io"] = "io";
				jChan["ioLabel"] = "输出";
			}
			else
			{
				jChan["io"] = "i";
				jChan["ioLabel"] = "输入";
			}
			jChan["tag_bind"] = "";
			jChan["type"] = IO_DEV_TYPE::CHAN::io_channel;
			jChan["type_label"] = IO_DEV_TYPE_LABEL.at(IO_DEV_TYPE::CHAN::io_channel);
			jChan["level"] = "channel";

			chanList.push_back(jChan);
		}
	}

	return true;
}

//w指令：[版本, 验证TOKEN, 物云名, w指令, [点1, 值], [点2, 值], [点3, 值]]
//请求：[2, "IQK", "C1201020756", "w", ["AO9", 5], ["BO4", 1]]
//返回：["C1201020756", ["AO9", 5, 1540697972, 0], ["BO4", 1, 1540697972, 0], "w"]
bool ioDev_iq60::writeChannel(json jVal, json& jResp)
{
	json jCmdW;
	jCmdW.push_back(2);
	jCmdW.push_back("IQK");
	jCmdW.push_back(m_devAddr);
	jCmdW.push_back("w");

	if (jVal.is_number())
	{
		double dbVal = jVal.get<double>();

		for (int i = 0; i < m_vecChild.size(); i++)
		{
			ioDev* p = m_vecChild.at(i);
			if (p)
			{
				json jonechanval;
				jonechanval.push_back(p->m_devAddr);
				jonechanval.push_back(dbVal);

				jCmdW.push_back(jonechanval);
			}
		}
	}
	else if (jVal.is_boolean())
	{
	}
	else if (jVal.is_string())
	{
	}
	else
	{
	}

	//[2,"IQK","C1210608622","w",["AI4986",36.3],["AI4987",36.3]]
	string req = jCmdW.dump() +"\n";

	if (!requestAndWaitResp("w", req))
		return false;

	//--模拟设备回包
	//string strResp = "[\"C1210608622\", [\"AI4986\", 36.3, 1540697972, 0], [\"AI4987\", 36.3, 1540697972, 0], \"w\"]";
	//currentResp.push_back(m_addr);
	//for (int i = 0; i < m_vecChild.size(); i++)
	//{
	//	ioDev* p = m_vecChild.at(i);
	//	if (p)
	//	{
	//		SYSTEMTIME t;
	//		GetLocalTime(&t);

	//		json jonechanval;
	//		jonechanval.push_back(p->m_addr);
	//		jonechanval.push_back(jVal.get<double>());
	//		jonechanval.push_back(timeopt::SysTime2Unix(t));
	//		jonechanval.push_back(0);

	//		currentResp.push_back(jonechanval);
	//	}
	//}
	//currentResp.push_back("w");
	//--

	jResp = currentResp;

	return true;
}

json ioDev_iq60::getAddr()
{
	json j;
	j["gateway_id"] = m_devAddr;
	return j;
}
