#include "pch.h"
#include "ioSrv.h"
#include "commSrv.h"
#include <thread>
#include "mo.h"
#include "mp.h"
#include "prj.h"
#include "logger.h"

#include "ioGW_tuyaProject.h"

#include "ioDev_modbusSlave.h"
#include "ioDev_mqttBroker.h"
#include "ioDev_tuya.h"

#include "ioChan.h"
#include "ioChan_tuya.h"

#include "ioGW_localSerial.h"
#include "ioDev_iq60.h"

#include "ioDev_genicam.h"

#include "rpcHandler.h"

ioServer ioSrv;

void IOThread()
{
}
ioServer::ioServer()
{
}
ioServer::~ioServer()
{
}

bool isBatchLink(string addr)
{
	if (addr.find("#") != string::npos)
	{
		return true;
	}
	else
	{
		return false;
	}
}

ioDev* createIODev(string ioAddr, string type)
{
	ioDev* p = NULL;
	if (type == "mqtt-broker")
	{
		p = new ioDev_mqttBroker();
		//string ip = conf["addr"]["ip"];
		//string port = conf["addr"]["port"];
		//p->m_devAddr = ip + ":" + port;
	}
	else if (type == "tuya-iot-project")
	{
		p = new ioGW_tuyaProject();
		/*p->m_devAddr = conf["addr"]["client_id"];
		p->m_secret = conf["addr"]["secret"];*/
	}
	else if (type == "tuya.switch")
	{
		p = new ioDev_tuya();
		//p->m_devAddr = conf["addr"]["device_id"];
	}
	else if (type == "iq60-gateway")
	{
		ioDev_iq60* piq60 = new ioDev_iq60();
		//p = piq60;
		//p->m_devAddr = conf["addr"]["gateway_id"];
	}
	else if (type == "genicam")
	{
		ioDev_genicam* pGenicam = new ioDev_genicam();
		p = pGenicam;
		p->m_devAddr = ioAddr;
		p->m_level = "device";
		p->m_devType = IO_DEV_TYPE::DEV::genicam;
	}
	return p;
}

ioDev* createIODevWithChildren(json conf)
{
	ioDev* p = NULL;
	if (conf["type"] == "mqtt-broker")
	{
		p = new ioDev_mqttBroker();
		string ip = conf["addr"]["ip"];
		string port = conf["addr"]["port"];
		p->m_devAddr = ip + ":" + port ;
	}
	else if (conf["type"] == "tuya-iot-project")
	{
		p = new ioGW_tuyaProject();
		p->m_devAddr = conf["addr"]["client_id"];
		p->m_secret = conf["addr"]["secret"];
	}
	else if (conf["type"] == "tuya.switch")
	{
		p = new ioDev_tuya();
		p->m_devAddr = conf["addr"]["device_id"];
	}
	else if (conf["type"] == "iq60-gateway")
	{
		ioDev_iq60* piq60 = new ioDev_iq60();
		p = piq60;
		p->m_devAddr = conf["addr"]["gateway_id"];
	}
	if (p)
	{
		p->m_devType = conf["type"];
		p->m_level = conf["level"];
	}
		


	if (p && conf["children"] != nullptr)
	{
		json childDev = conf["children"];
		for (auto i : childDev)
		{
			ioDev* pChild = nullptr;
			if (i["level"] == "channel")
			{
				string addr = i["addr"];

				if (isBatchLink(addr)) //datachannel instance of the batch data link will be created dynamicly when the channel data is received
				{
					p->m_mapBatchDataLink[addr] = i["tag_bind"];
				}
				else
				{
					ioChannel* pdc = nullptr;
					if (p->m_devType == "tuya.switch")
					{
						pdc = new ioChan_tuya();
					}
					else
						pdc = new ioChannel();
					pChild = pdc;
					pdc->m_level = "channel";
					pdc->m_devAddr = addr;
					if (i["tag_bind"] != nullptr)
					{
						if (i["tag_bind"].is_array())
						{
							json tagNodes = i["tag_bind"];
							string tag;
							for (int i = 0; i < tagNodes.size(); i++)
							{
								tag += tagNodes[i];
								if (i < tagNodes.size() - 1)
									tag += ".";
							}
							pdc->m_strLinkMPTag = tag;
						}
						else
						{
							pdc->m_strLinkMPTag = i["tag_bind"];
						}
					}
					p->m_mapDataChannel[addr] = pdc;
					if (i["io"] != nullptr)
						pdc->m_io = i["io"];
					if (i["ioLabel"] != nullptr)
						pdc->m_ioLabel = i["ioLabel"];
					if (i["valType"] != nullptr)
						pdc->m_valType = i["valType"];
					if (i["valTypeLabel"] != nullptr)
						pdc->m_valTypeLabel = i["valTypeLabel"];
					if(i["name"]!=nullptr)
						pdc->m_name = i["name"];
				}
			}
			else if(i["level"] == "device")
			{
				pChild = createIODevWithChildren(i);
			}

			if (pChild)
			{
				p->m_vecChild.push_back(pChild);
				pChild->m_pParent = p;
			}
		}
	}

	return p;
}

bool ioServer::loadConf()
{
	string conf;
	if (!fs::readFile(tds->conf->projectConfPath + "\\io.json", conf))
	{
		LOG("project conf io.json load fail,use empty conf");
		return true;
	}


	try {
		json io = json::parse(conf.c_str());
		
		for (auto it : io)
		{
			ioDev* p = createIODevWithChildren(it);
			if(p)
				m_vecChild.push_back(p);
		}
	}
	catch (std::exception& e)
	{
		std::cout << e.what() << std::endl;
		return false;
	}
	return true;
}

void ioServer::saveConf()
{
}

void ioServer::refreshSerialIODev()
{
	//从操作系统的设备管理器获得串口列表信息
	vector<sys::COM_INFO> aryNew;
	aryNew = sys::getCOMInfoList();

	//将当前串口设备置为离线状态
	vector<ioDev*> ary = ioSrv.getChildren(IO_DEV_TYPE::GW::local_serial);
	for (auto& i : ary)
	{
		i->m_bOnline = false;
	}

	//为新上线的串口创建对应的ioDev.并更新在线状态
	for (auto& i : aryNew)
	{
		sys::COM_INFO ci = i;
		ioGW_LocalSerial* ls = (ioGW_LocalSerial*)getIODev(ci.portNum);
		if (!ls)
		{
			ls = new ioGW_LocalSerial();
			ls->m_devAddr = ci.portNum;
			ls->m_devTypeLabel = ci.desc;
			ls->m_mngStatus = IODEV_MNG_STATUS::spare;
			m_vecChild.push_back(ls);
		}
		ls->m_bOnline = true;
	}

	//离线的空闲设备，从io设备列表中删除。已配置设备保留
	for (auto& i : ary)
	{
		if (i->m_bOnline == false && i->m_mngStatus == IODEV_MNG_STATUS::spare)
		{
			ioSrv.deleteDescendant(i);
		}
	}
}

bool ioServer::run()
{
	if (loadConf())
	{
		//commSrv.Run();
		for (auto i : m_vecChild)
		{
			i->run();
		}
		//std::thread io(IOThread);
	}

	ioDiscoverService.run();

	refreshSerialIODev();
	
	return true;
}

bool ioServer::toJson(json& conf, string opt)
{
	conf = json::array();//empty array
	for (auto& i : m_vecChild)
	{
		json j;
		if (i->m_devType == IO_DEV_TYPE::GW::local_serial)
		{
			continue;
		}
		i->toJson(j, opt);
		string s = j.dump();
		conf.push_back(j);
	}
	return true;
}


string ioServer::getTag(string strDataChannelID)
{
	/*for (int i = 0; i < m_vecGlobalChannel.size(); i++)
	{
		string strID = str::trim(m_vecGlobalChannel[i].strID);
		str::replace(strID, "\\", "/");
		str::replace(strDataChannelID, "\\", "/");
		if (strID == strDataChannelID)
		{
			return m_vecGlobalChannel[i].strLink_MP;
		}
	}*/

	return "";
}

ioDev* ioServer::onDevDiscovered(string ioAddr, string type)
{
	ioDev* p = createIODev(ioAddr,type);
	p->m_mngStatus = IODEV_MNG_STATUS::spare;
	p->m_bOnline = true;
	ioSrv.m_vecChild.push_back(p);
	json j;
	p->toJson(j);
	tdsSrv.notify("devDiscovered", j);
	return p;
}

