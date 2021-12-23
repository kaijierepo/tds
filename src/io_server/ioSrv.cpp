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
#include "ioGW_rs485.h"

#include "ioChan.h"

#include "ioGW_localSerial.h"
#include "ioDev_iq60.h"
#include "ioDev_tdsp.h"
#include "ioDev_genicam.h"

#include "rpcHandler.h"

ioServer ioSrv;

void IOThread()
{
	ioSrv.m_bWorkingThreadRunning = true;
	int statisUpdateInterval = 10;
	while (1)
	{
		if (!ioSrv.m_bRunning)
			break;
		for (int i = 0; i < ioSrv.m_vecChild.size(); i++)
		{
			ioDev* pIoDev = ioSrv.m_vecChild[i];
			//空闲设备不轮询数据
			if(pIoDev->bEnableAcq && pIoDev->m_mngStatus == IODEV_MNG_STATUS::managed)
				pIoDev->DoCycleTask();

			if (!ioSrv.m_bRunning)
				break;
		}
		Sleep(5);
	}
	ioSrv.m_bWorkingThreadRunning = false;
	ioSrv.m_signalWorkThreadExit.notify();
}
ioServer::ioServer()
{
}
ioServer::~ioServer()
{
}



ioDev* createIODev(string type)
{
	ioDev* p = NULL;
	if (type == IO_DEV_TYPE::DEV::mqttBroker)
	{
		p = new ioDev_mqttBroker();
	}
	else if (type == IO_DEV_TYPE::DEV::tdsp_device)
	{
		p = new ioDev_tdsp();
	}
	else if (type == IO_DEV_TYPE::GW::tuya_iot_project)
	{
		p = new ioGW_tuyaProject();
	}
	else if (type == "tuya.switch")
	{
		p = new ioDev_tuya();
	}
	else if (type == "iq60-gateway")
	{
		p = new ioDev_iq60();	
	}
	else if (type == "genicam")
	{
#ifdef ENABLE_GENICAM
		p = new ioDev_genicam();
#endif
	}
	else if (type == IO_DEV_TYPE::GW::local_serial)
	{
		p = new ioGW_LocalSerial();
	}
	return p;
}



bool ioServer::loadConf()
{
	std::unique_lock<shared_mutex> lock(m_csThis); //写锁
	string conf;
	if (!fs::readFile(tds->conf->projectConfPath + "/io.json", conf))
	{
		LOG("project conf io.json load fail,use empty conf");
		return true;
	}


	try {
		json io = json::parse(conf.c_str());
		
		for (auto it : io)
		{
			ioDev* p = createIODev(it);
			p->loadConf(it);
			if(p)
				m_vecChild.push_back(p);
		}
	}
	catch (std::exception& e)
	{
		string error = e.what();
		LOG("加载io.json失败," + error);
		return false;
	}
	return true;
}

void ioServer::saveConf()
{
}

ioDev* ioServer::getIODev(string ioAddr)
{
	std::shared_lock<shared_mutex> lock(m_csThis); //读锁
	return ioDev::getIODev(ioAddr);
}

void ioServer::clear()
{
	std::unique_lock<shared_mutex> lock(m_csThis); //写锁
	for (int i = 0; i < m_vecChild.size(); i++)
	{
		delete m_vecChild[i];
	}
	m_vecChild.clear();
}

void ioServer::refreshSerialIODev()
{
	//从操作系统的设备管理器获得串口列表信息
	vector<sys::COM_INFO> aryNew;
	aryNew = sys::getCOMInfoList();


	//新列表里有，当前列表没有。  为新上线的串口创建对应的ioDev.置为在线
	for (auto& i : aryNew)
	{
		sys::COM_INFO ci = i;
		ioDev* ls = getIODev(ci.portNum);
		if (!ls)
		{
			ls = onChildDevDiscovered(ci.portNum, IO_DEV_TYPE::GW::local_serial);
			ls->m_devTypeLabel = ci.desc;
		}
	}

	//当前列表里有，新列表没有。 表示离线。 离线的空闲设备，从io设备列表中删除。已配置设备保留
	vector<ioDev*> ary = ioSrv.getChildren(IO_DEV_TYPE::GW::local_serial);
	for (auto& i : ary)
	{
		bool bOnline = false;
		//在新列表里面找的到才在线
		for (auto& newStatus : aryNew)
		{
			if (newStatus.portNum == i->getIOAddrStr())
			{
				bOnline = true;
			}
		}

		if (bOnline == false && i->m_mngStatus == IODEV_MNG_STATUS::spare)
		{
			ioSrv.deleteDescendant(i);
		}
	}
}

bool ioServer::run()
{
	m_bRunning = true;

	if (loadConf())
	{
		std::shared_lock<shared_mutex> lock(m_csThis);
		for (auto i : m_vecChild)
		{
			i->run();
		}
		std::thread io(IOThread);
		io.detach();
	}

	ioDiscoverService.run();

	refreshSerialIODev();
	
	return true;
}

void ioServer::stop()
{
	LOG("stoping ioServer...");
	ioDev::stop();
	LOG("ioServer stopped");
}

bool ioServer::toJson(json& conf, json opt)
{
	conf = json::array();//empty array
	for (auto& i : m_vecChild)
	{
		json j;

		if (opt != nullptr)
		{
			if (opt.contains("tagBind"))
			{
				string tagBind = opt["tagBind"].get<string>();

				//指定查找智能设备。但是是非智能设备
				if (i->m_strTagBind == "")
				{
					if(tagBind != "")
						continue;
				}
				else
				{
					if (tagBind != "*" && tagBind != i->m_strTagBind)
						continue;
				}
			}
		}


		i->toJson(j, opt);
		string s = j.dump();
		conf.push_back(j);
	}
	return true;
}

bool ioServer::getStatus(json& conf, string opt)
{
	conf = json::array();//empty array
	for (auto& i : m_vecChild)
	{
		json j;
		i->getStatus(j, opt);
		string s = j.dump();
		conf.push_back(j);
	}
	return true;
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

ioDev* ioServer::onChildDevDiscovered(json childDevAddr, string type)
{
	std::unique_lock<shared_mutex> lock(m_csThis); //写锁
	ioDev* p = createIODev(type);
	p->m_jDevAddr = childDevAddr;
	if (childDevAddr.is_string())
		p->m_devAddr = p->m_jDevAddr.get<string>();
	p->m_mngStatus = IODEV_MNG_STATUS::spare;
	p->m_bOnline = true;
	ioSrv.m_vecChild.push_back(p);
	json j;
	p->toJson(j);
	rpcSrv.notify("devDiscovered", j);
	return p;
}

void ioServer::getAllSmartDev(vector<ioDev*>& aryDev)
{
	for (auto& it : m_vecChild)
	{
		if (it->m_strTagBind != "" && it->m_level != IO_DEV_LEVEL::channel)
		{
			aryDev.push_back(it);
		}
	}
}

