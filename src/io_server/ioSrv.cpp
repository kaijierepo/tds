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

ioDev* createIODev(json conf)
{
	ioDev* p = NULL;
	if (conf["type"] == "mqtt-broker")
	{
		p = new ioDev_mqttBroker();
		string ip = conf["addr"]["ip"];
		string port = conf["addr"]["port"];
		p->m_addr = ip + ":" + port ;
	}
	else if (conf["type"] == "tuya-iot-project")
	{
		p = new ioGW_tuyaProject();
		p->m_addr = conf["addr"]["client_id"];
		p->m_secret = conf["addr"]["secret"];
	}
	else if (conf["type"] == "tuya.switch")
	{
		p = new ioDev_tuya();
		p->m_addr = conf["addr"]["device_id"];
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
					pdc->m_addr = addr;
					pdc->m_strLinkMPTag = i["tag_bind"];
					p->m_mapDataChannel[addr] = pdc;
				}
			}
			else if(i["level"] == "device")
			{
				pChild = createIODev(i);
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
			ioDev* p = createIODev(it);
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
	//如果组态里有配置，更新信息。如果没有配置，增加设备。作为空闲设备
	vector<sys::COM_INFO> aryNew;
	aryNew = sys::getCOMInfoList();
	vector<ioDev*> ary = getIODevices(IO_DEV_TYPE::GW::local_serial);

	for (auto& i : ary)
	{
		i->m_bOnline = false;
	}

	for (auto& i : aryNew)
	{
		sys::COM_INFO ci = i;
		ioGW_LocalSerial* ls = (ioGW_LocalSerial*) getIODev(ci.portNum);
		if (!ls)
		{
			ls = new ioGW_LocalSerial();
			ls->m_addr = ci.portNum;
			ls->m_devTypeLabel = ci.desc;
			m_vecChild.push_back(ls);
		}
		ls->m_bOnline = true;
	}

	//串口
	for (auto& i : ary)
	{
		if (i->m_bOnline == false)
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

	serialDetectionService.run();

	refreshSerialIODev();
	
	return true;
}

bool ioServer::toJson(json& conf, string opt)
{
	conf = json::array();//empty array
	for (auto& i : m_vecChild)
	{
		json j;
		i->toJson(j, opt);
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

