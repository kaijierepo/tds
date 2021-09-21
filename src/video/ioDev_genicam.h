#pragma once
#include "json.hpp"
#include "ioDev.h"
#include "tdsSession.h"
#include "system.h"
#include "interface.h"
#include "device.h"
#include "stream.h"
#include "config.h"
#include "image.h"
#include "videoSrvNode.h"

using json = nlohmann::json;



class ioDev_genicam : public ioDev {
public:
	static void runSingleHostMode();//单摄像头主机模式
	static json listDevices();
	static std::shared_ptr<rcg::Device> getSingleGenicam();
	static void mono8ToBmp(char* pData, int w, int h, string fileName);
	static void captureImageToBmp(string devId, string fileName);
	vector<std::shared_ptr<TDS_SESSION>> m_streamPuller; //拉流方
	std::shared_ptr<rcg::Device> m_genicamDev; //
	void startStream();


	videoSrvNode m_videoSrvNode;

	json m_jDevInfo;
};

extern ioDev_genicam* singleCamera;