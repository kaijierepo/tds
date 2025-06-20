#include "pch.h"
#include "httplib.h"
#include "json.hpp"
#include "ioDev_visca.h"
#include "logger.h"
#include "prj.h"
#include "ioChan.h"
#include "ioSrv.h"
#include "rpcHandler.h"
#include "webSrv.h"

using namespace httplib;

ioDev* createDev_visca()
{
	return new ioDev_visca();
}


class createReg_visca {
public:
	createReg_visca();
};
createReg_visca::createReg_visca()
{
	mapDevCreateFunc["visca"] = createDev_visca;
	mapDevTypeLabel["visca"] = "Visca摄像头";
}
createReg_visca regvisca;


ioDev_visca::ioDev_visca()
{
	m_devType = "visca";
	m_devTypeLabel = "Visca摄像头";
	m_bIsReadingFile = false;
	m_bIsWaitingAcq = false;
	m_level = "devcie";
}

ioDev_visca::~ioDev_visca()
{
}

void visca_cycleThread(ioDev_visca* pDev) {
	//版本查询
	string s = "81 09 00 02 FF";
	vector<unsigned char> req = str::hexStrToBytes(s);
	vector<unsigned char> resp;
	pDev->doTransaction(req, resp);
}


void ioDev_visca::DoCycleTask()
{
	//UDP设备 不根据是否在线来决定是否轮询。tcp可以依靠tcp连接判断在线,udp必须依靠数据回包
	//if (!m_bOnline)
	//	return;

	//if (m_dispositionMode == DEV_DISPOSITION_MODE::spare)
	//	return;

	if (m_bEnableAcq) {
		if (timeopt::CalcTimePassSecond(m_stLastAcqTime) > m_fAcqInterval) {
			timeopt::now(&m_stLastAcqTime);
			if (!m_bIsWaitingResp) {
				thread t(visca_cycleThread, this);
				t.detach();
			}
		}
	}
}

bool ioDev_visca::onRecvData(unsigned char* pData, size_t iLen)
{
	setOnline();

	if (m_bIsWaitingResp) {
		m_currentResp.resize(iLen);
		memcpy(m_currentResp.data(), pData, iLen);
		m_respSignal.notify();
	}

	return false;
}

bool ioDev_visca::onRecvPkt(unsigned char* pData, size_t iLen)
{


	return false;
}


bool ioDev_visca::getCurrentVal()
{
	return false;
}

bool ioDev_visca::sendData(unsigned char* pData, size_t iLen)
{
	unique_lock<mutex> lock(m_csIOSession);
	if (pIOSession)
	{
		pIOSession->send(pData, iLen);
	}
	else if (m_udpClt) {
		if (m_jDevAddr.contains("ip") && m_jDevAddr.contains("port")) { 
			string ip = m_jDevAddr["ip"];
			int udpPort = m_jDevAddr["port"].get<int>();
			int iSent = m_udpClt->SendData(pData, iLen, ip, udpPort);
			string remoteAddr = "UDP-" + ip + ":" + str::fromInt(udpPort);
			string localAddr = "UDP-" + m_udpClt->m_bindIP + ":" + str::fromInt(m_udpClt->m_port);
			IOLogSend((unsigned char*)pData, iLen, iSent > 0,remoteAddr,localAddr);
		}
	}
	else
		return false;
	return true;
}

bool ioDev_visca::doTransaction(vector<unsigned char>& req, vector<unsigned char>& resp, bool sync)
{
	if (sync) {
		CommLock();
		m_bIsWaitingResp = true;
		m_currentReq = req;
		bool ret = false;
		if (sendData(req.data(), req.size()))
		{
			if (m_respSignal.wait_for(2000)) {
				resp = m_currentResp;
				ret = true;
			}
			else
			{
				setOffline();
				ret = false;
			}
		}
		m_bIsWaitingResp = false;
		CommUnlock();
		return ret;
	}
	else {
		sendData(req.data(), req.size());
		return true;
	}
}


void ioDev_visca::ptz_startMove(string dir, float panSpeed, float tiltSpeed)
{
	string cmd;
	int iPanS = (int)panSpeed;
	int iTiltS = (int)tiltSpeed;
	string sPanSpeed = str::format("%02X", iPanS); 
	string sTiltSpeed = str::format("%02X", iTiltS);

	if (dir == "up") {
		cmd = "81 01 06 01 VV WW 03 01 FF";
	}
	else if (dir == "down") {
		cmd = "81 01 06 01 VV WW 03 02 FF";
	}
	else if (dir == "left") {
		cmd = "81 01 06 01 VV WW 01 03 FF";
	}
	else if (dir == "right") {
		cmd = "81 01 06 01 VV WW 02 03 FF";
	}

	cmd = str::replace(cmd, "VV", sPanSpeed);
	cmd = str::replace(cmd, "WW", sTiltSpeed);

	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]startMove发送请求:%s,设备地址:%s", cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp,false);
}



void ioDev_visca::ptz_stopMove()
{
	string	cmd = "81 01 06 01 01 01 03 03 FF";
	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]stopMove发送请求:%s,设备地址:%s", cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_startZoom(string dir, float zoomSpeed)
{
	string	cmd;
	if (dir == "out") {
		cmd = "81 01 04 07 03 FF";
	}
	else if (dir == "in") {
		cmd = "81 01 04 07 02 FF";
	}

	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]startZoom发送请求:%s,设备地址:%s", cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_stopZoom()
{
	string	cmd = "81 01 04 07 00 FF";
	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]stopZoom发送请求:%s,设备地址:%s", cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_startFocus(string dir, float focusSpeed)
{
	string	cmd;
	if (dir == "far") {
		cmd = "81 01 04 08 02 FF";
	}
	else if (dir == "near") {
		cmd = "81 01 04 08 03 FF";
	}

	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]startFocus发送请求:%s,设备地址:%s", cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_stopFocus()
{
	string	cmd = "81 01 04 08 00 FF";
	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;
	
	LOG("[Visca]stopFocus发送请求:%s,设备地址:%s", cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_gotoPreset(int presetIdx)
{
	string cmd = " 81 01 04 3F 02 II FF";

	string sIdx = str::format("%02d", presetIdx);
	cmd = str::replace(cmd, "II", sIdx);

	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]发送请求,调用预置位:%d,%s,设备地址:%s",presetIdx, cmd.c_str(), getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_addPreset(int presetIdx)
{
	string cmd = "81 01 04 3F 01 II FF";

	string sIdx = str::format("%02d", presetIdx);
	cmd = str::replace(cmd, "II", sIdx);

	vector<unsigned char> req = str::hexStrToBytes(cmd);
	vector<unsigned char> resp;

	LOG("[Visca]发送请求,设定预置位:%d,%s,设备地址:%s", presetIdx, cmd.c_str(),getIOAddrStr().c_str());
	doTransaction(req, resp, false);
}

void ioDev_visca::ptz_deletePreset(int presetIdx)
{
	//string cmd = "81 01 04 3F 01 II FF";

	//string sIdx = str::format("%02d", presetIdx);
	//cmd = str::replace(cmd, "II", sIdx);

	//vector<unsigned char> req = str::hexStrToBytes(cmd);
	//vector<unsigned char> resp;

	//LOG("[Visca]发送请求,设定预置位:%d,%s", presetIdx, cmd.c_str());
	//doTransaction(req, resp, false);
}




