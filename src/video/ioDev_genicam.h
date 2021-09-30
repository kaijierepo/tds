#pragma once
#ifdef ENABLE_GENICAM
#include "json.hpp"
#include "ioDev.h"
#include "tdsSession.h"
#define PFNC_INCLUDE_HELPERS
#include "GenTL/PFNC.h"
#include "system.h"
#include "interface.h"
#include "device.h"
#include "stream.h"
#include "config.h"
#include "image.h"
#include "streamSrvNode.h"

using json = nlohmann::json;



class ioDev_genicam : public ioDev, public STREAM_PUSHER{
public:
	ioDev_genicam();
	bool disconnect() override;
	bool connect() override;
	string getDesc() override;
	bool run();

	static json listDevices();
	static void mono8ToBmp(char* pData, int w, int h, string fileName);
	static void captureImageToBmp(string devId, string fileName);
	vector<std::shared_ptr<TDS_SESSION>> m_streamPuller; //À­Á÷·½
	std::shared_ptr<rcg::Device> m_genicamDev; //
	void setGenicamDev(std::shared_ptr<rcg::Device> genDev);
	std::shared_ptr<GenApi::CNodeMapRef> m_nodemap;
	void doStreaming();
	void addStreamId(string streamId);

	void setParam(string name, json val, bool isEnum);
	void doCmd(string name);
	
	bool startStream(STREAM_INFO* si=NULL) override;
	bool stopStream() override;
	bool m_bStopStream;
	bool m_bStreaming;
	STREAM_INFO m_streamInfo;
	mutex m_csStreamThread;
	json m_jDevInfo;
};

extern ioDev_genicam* firstDiscoverGenicam;
#endif