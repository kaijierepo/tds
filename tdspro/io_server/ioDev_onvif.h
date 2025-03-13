#pragma once
#include "ioDev.h"
#include "tdsSession.h"
#include "ioDev_camera.h"


class ioDev_onvif : public ioDev_camera
{
public:
	ioDev_onvif();
	~ioDev_onvif();
	bool run() override;
	void DoCycleTask() override;
	bool onRecvPkt(unsigned char* pData, size_t iLen) override; //接收到完整的协议数据包
	bool getCurrentVal();
	void setVal(string& str, string name, string val);

	void onvif_getDevInfo();
	void onvif_getSnapshotUri();
	void onvif_getProfiles();
	void onvif_getPresets();
	void onvif_gotoPresets();
	bool doOnvifTransaction(string msg, string uri, bool log = true);
	bool isAddrValid();

	virtual void ptz_startMove(string dir, float panSpeed = 0.5, float tiltSpeed = 0.5) override;
	map<string, string> parseKeyValStr(string s);
	virtual void ptz_stopMove() override;
	virtual void ptz_startZoom(string dir, float zoomSpeed = 0.5) override;
	virtual void ptz_stopZoom() override;
	string generateNouce();
	string generateAuthStr(string uri,string realm, string nonce,string cnonce);
	string m_snapshotURL;
};