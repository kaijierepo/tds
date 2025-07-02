#include "pch.h"
#include "json.hpp"
#include "ioDev_iq60.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"


ioDev* createDev_iq60()
{
	return new ioDev_iq60();
}

class createReg_IQ60 {
public:
	createReg_IQ60();
};
createReg_IQ60::createReg_IQ60()
{
	mapDevCreateFunc["iq60-gateway"] = createDev_iq60;
	mapDevTypeLabel["iq60-gateway"] = "IQ60";
}
createReg_IQ60 regIQ60;




ioDev_iq60::ioDev_iq60()
{
	m_devType = "iq60-gateway";
	m_devTypeLabel = getDevTypeLabel(m_devType);
	m_level = "devcie";
}

ioDev_iq60::~ioDev_iq60()
{
}

bool ioDev_iq60::isBridged()
{
	if (pIOSession != nullptr) {
		if (pIOSession->bridgedIoSessionClient != nullptr) {
			return true;
		}
	}

	return false;
}

void ioDev_iq60::DoCycleTask()
{
	//如果通信被桥接，不发送周期类命令
	//if (!isBridged()) {
	//被桥接进入到 命令模式时，也需要发送心跳，否则iq60会自己掉线
	//并且重连之后因为是命令模式，连接上不上送首发包，导致设备不上线。
		if (timeopt::CalcTimePassSecond(m_stLastHeartbeatTime) > 30)
		{
			string req = "[]\n";
			sendStr(req);
			timeopt::now(&m_stLastHeartbeatTime);
		}
	//}
}

bool ioDev_iq60::onRecvPkt(json jPkt)
{
	if (m_bRunning == false)
		return false;

	m_bRecvProcessing = true;
	FLAG_GUARD g(&m_bRecvProcessing);

	json jLast = jPkt[jPkt.size() - 1];

	if (jLast.is_string())//请求的回包最后一个字段是字符串，则最后一个字段是命令码。 主动上报数据包最后一个字段是个数组
	{
		string cmd = jLast.get<string>();

		if (cmd.find(currentCmd) != string::npos)
		{
			currentResp.push_back(jPkt);
			//如果一个回包非常大，会分多个包回复，分包的命令字段为"-"号，结束包
			if (cmd.find("-") == string::npos) 
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
			string valType = VAL_TYPE::Float;
			json point = jPkt[i];
			//格式["AI9",48.8,1540697466,0],["BO1",1,1540697466,0]
			// 
			//点位名称
			string name = point[0].get<string>();
			bool bVal;
			json jVal;

			//点位值
			if (!point[1].is_null())
			{
				if (name.find("B") == 0)
				{
					valType = "bool";
					bVal = point[1].get<int>() == 1 ? true : false;
					jVal = bVal;
				}
				else
				{
					jVal = point[1];
				}
			}
			else
			{
				return true;
			}

			//时间戳
			int time = point[2].get<int>();
			//状态
			int status = point[3].get<int>();

			ioChannel* pC = getChanByDevAddr(name);
			if (pC)
			{
				pC->input(jVal);
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
		if (m_bRunning == false)
			return false;
		timeopt::sleepMilli(50);
		timeout -= 50;
		if (getResponse)
			return true;
	}
	return false;
}

bool ioDev_iq60::sendData(unsigned char* pData, size_t iLen)
{
	unique_lock<mutex> lock(m_csIOSession);
	if (pIOSession)
	{
		pIOSession->send(pData, iLen);
		if (m_bEnableIoLog)
			statisOnSend(pData, iLen, getIOAddrStr());
	}
	else
		return false;
	return true;
}

bool ioDev_iq60::requestAndWaitResp(string cmd, string req)
{
	currentCmd = cmd;
	currentResp.clear();
	getResponse = false;

	if(!sendData((unsigned char*)req.c_str(), req.length()))
		return false;

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
	//请求io点列表
	string req = "[2,\"IQK\",\"" + m_devAddr + "\",\"hs\"]\n";
	LOG("[IQ60]请求IO点列表," + req);
	if (!requestAndWaitResp("hs", req))
		return false;
	LOG("[IQ60]请求IO点列表,收到响应");



	//获得需要请求详细信息的io点
	vector<string> ptList;
	for (int i = 0; i < currentResp.size(); i++)
	{
		json jsubPkt = currentResp.at(i);
		for (int j = 1; j < jsubPkt.size() - 1; j++)
		{
			string ptName = jsubPkt[j];
			ptList.push_back(ptName);
		}
	}
	//最多一次读取50个点的详细信息，计算读取次数
	int readCount = ptList.size() / 50;
	if (ptList.size() % 50 > 0) {
		readCount += 1;
	}


	for (int i = 0; i < readCount; i++) {
		json jCmdHr;
		jCmdHr.push_back(2);
		jCmdHr.push_back("IQK");
		jCmdHr.push_back(m_devAddr);
		jCmdHr.push_back("hr");

		for (int j = i * 50; j < ptList.size() && j< (i+1)*50; j++) {
			string ptName = ptList[j];
			jCmdHr.push_back(ptName);
		}
		req = jCmdHr.dump() + "\n";

		LOG("[IQ60]读取IO点详细信息," + req);
		if (!requestAndWaitResp("hr", req))
			return false;
		LOG("[IQ60]读取IO点详细信息,收到响应");

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
					jChan["valType"] = VAL_TYPE::Float;
				else if (valType == "bool")
					jChan["valType"] = VAL_TYPE::boolean;
				else if (valType == "int")
					jChan["valType"] = VAL_TYPE::integer;
				else
					continue;
				jChan["name"] = jPt["DisplayName"];
				if (jPt["RW"] == "rw")
				{
					jChan["ioType"] = "io";
					jChan["ioTypeLabel"] = "输出";
				}
				else
				{
					jChan["ioType"] = "i";
					jChan["ioTypeLabel"] = "输入";
				}
				jChan["tagBind"] = "";
				jChan["type"] = "io-channel";
				jChan["typeLabel"] = getDevTypeLabel("io-channel");
				jChan["level"] = "channel";

				chanList.push_back(jChan);
			}
		}
	}


	return true;
}

//w指令：[版本, 验证TOKEN, 物云名, w指令, [点1, 值], [点2, 值], [点3, 值]]
//请求：[2, "IQK", "C1201020756", "w", ["AO9", 5], ["BO4", 1]]
//返回：["C1201020756", ["AO9", 5, 1540697972, 0], ["BO4", 1, 1540697972, 0], "w"]
void ioDev_iq60::output(string chanAddr,json jVal, json& rlt,json& err, bool sync)
{
	if (!m_bRunning) {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device acq object is not running"));
		return;
	}
		

	if (pIOSession == nullptr) {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::IO_devOffline, "device offline"));
		return;
	}

	m_bOutputting = true;
	FLAG_GUARD g(&m_bOutputting);

	json jCmdW;
	jCmdW.push_back(2);
	jCmdW.push_back("IQK");
	jCmdW.push_back(m_devAddr);
	jCmdW.push_back("w");

	if (jVal.is_boolean())
	{
		if (jVal.get<bool>() == true)
		{
			jVal = 1;
		}
		else
		{
			jVal = 0;
		}
	}

	json jonechanval;
	jonechanval.push_back(chanAddr);
	jonechanval.push_back(jVal);

	jCmdW.push_back(jonechanval);


	//[2,"IQK","C1210608622","w",["AI4986",36.3],["AI4987",36.3]]
	string req = jCmdW.dump() +"\n";

	if (sync)
	{
		bool bGetResp = requestAndWaitResp("w", req);
		if (bGetResp)
		{
			// w 的命令回包只表示设备收到数据，不代表改值成功。 收到值变化通知才认为改值成功
			//ioChannel* pC = getChanByDevAddr(chanAddr);
			//if (pC != nullptr)
			//{
			//	pC->input(jVal);
			//}
			rlt = RPC_OK;
			return;
		}	
		else {
			err = RPC_TIMEOUT;
			return;
		}
	}
	else
	{
		if (!sendData((unsigned char*)req.c_str(), req.length())) {
			err = "\"send data fail\"";
			return;
		}
		else {
			rlt = RPC_OK;
			return;
		}
	}

	
	//--模拟设备回包
	//string strResp = "[\"C1210608622\", [\"AI4986\", 36.3, 1540697972, 0], [\"AI4987\", 36.3, 1540697972, 0], \"w\"]";
	//currentResp.push_back(m_addr);
	//for (int i = 0; i < m_vecChild.size(); i++)
	//{
	//	ioDev* p = m_vecChild.at(i);
	//	if (p)
	//	{
	//		SYSTEMTIME t;
	//		timeopt::now(&t);

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
}

void ioDev_iq60::output(ioChannel* pC, json jVal, json& rlt,json& err, bool sync)
{
	if (!m_bRunning) {
		err = json::parse(makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "device acq object is not running"));
		return;
	}

	m_bOutputting = true;
	FLAG_GUARD g(&m_bOutputting);

	output(pC->getDevAddrStr(), jVal, rlt, err, sync);
	
	//iq60的输出成功仅仅代表设备收到数据，不代表改值成功，依靠数据变化上送修改值
	//if(rlt!=nullptr)
	//{
	//	pC->input(jVal);
	//}
}

json ioDev_iq60::getAddr()
{
	json j;
	j["id"] = m_devAddr;
	return j;
}

