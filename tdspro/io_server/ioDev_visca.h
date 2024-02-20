#pragma once
#include "ioDev.h"
#include "tdsSession.h"
#include "proto_visca.hpp"
#include "ioDev_camera.h"
#include "udpSrv.h"

class ioDev_visca : public ioDev_camera
{
public:
	ioDev_visca();
	~ioDev_visca();
	void DoCycleTask() override;
	bool onRecvData(unsigned char* pData, size_t iLen) override;
	bool onRecvPkt(unsigned char* pData, size_t iLen) override; //接收到完整的协议数据包
	bool getCurrentVal();
	bool sendData(unsigned char* pData, size_t iLen) override;
	bool doTransaction(vector<unsigned char>& req, vector<unsigned char>& resp,bool sync = true);

	virtual void ptz_startMove(string dir,float panSpeed = 0, float tiltSpeed = 0) override;
	virtual void ptz_stopMove() override;
	virtual void ptz_startZoom(string dir, float zoomSpeed = 0) override;
	virtual void ptz_stopZoom() override;
	virtual void ptz_startFocus(string dir, float focusSpeed = 0) override;
	virtual void ptz_stopFocus() override;
	virtual void ptz_gotoPreset(int presetIdx) override;
	virtual void ptz_addPreset(int presetIdx) override;
	virtual void ptz_deletePreset(int presetIdx) override;

	semaphore m_respSignal;
	vector<unsigned char> m_currentReq;
	vector<unsigned char> m_currentResp;
	mutex m_csResp;
	bool getResponse;
	stream2pkt m_fileBuff;
	bool m_bIsReadingFile;
	bool m_bIsWaitingAcq;
};