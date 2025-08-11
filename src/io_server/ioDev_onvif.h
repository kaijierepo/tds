#pragma once
#include "ioDev.h"
#include "tdsSession.h"
#include "ioDev_camera.h"

struct DIGIST_INFO {
	string uri;
	string nouce;
	string cnouce;
	string realm;
	string authorization;
};
void onvifSnapshot(string ip, int port, string user, string pwd, DIGIST_INFO& di);

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

	static string generateNouce();
	static string generateAuthStr(string user,string pwd, string method, string uri, string realm, string nonce, string cnonce);

	static map<string, string> parseKeyValStr(string s);

	bool isAddrValid();

	void onvif_getDevInfo();
	void onvif_getSnapshotUri();
	void onvif_getProfiles();
	void onvif_getPresets();

	virtual void ptz_startMove(string dir, float panSpeed = 0.5, float tiltSpeed = 0.5) override;
	virtual void ptz_stopMove() override;
	virtual void ptz_startZoom(string dir, float zoomSpeed = 0.5) override;
	virtual void ptz_stopZoom() override;
	virtual void ptz_gotoPreset(int presetIdx) override;
	virtual void ptz_gotoPreset(string preset) override;
	virtual void ptz_gotoAbsolute(float pan, float tilt, float zoom) override;
	virtual void ptz_pausePresetPatrol(int parseTime) override;

	bool doOnvifTransaction(string msg, string uri, string method, bool log = true);
	bool doOnvifTransaction_getSnapShot();

	string m_snapshotURL;

	int  m_curPZTChan;
	TIME m_lastPTZPollTime; //上一次预置位巡检时间
	int  m_ptzPollInterval; //0

	bool m_bPaused;
	TIME m_pauseResumeTime;
	int  m_pauseResumeInterval;  //0

};