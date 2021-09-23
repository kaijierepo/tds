#pragma once
#include "tdsSession.h"
#include "tds.h"
#include "tdscore.h"
#include "videoCodec.h"


struct STREAM_DATA {
	char* pData;
	int len;
	STREAM_INFO info;

	STREAM_DATA()
	{
		pData = NULL;
		len = 0;
	}

	~STREAM_DATA()
	{
		if (pData)
			delete pData;
	}
};

class STREAM_PULLER {
public:
	std::shared_ptr<TDS_SESSION> tdsSession;
	fp_onVideoStreamRecv callbackFunc;
	void* user;

	STREAM_DATA destData;

	STREAM_PULLER()
	{
		tdsSession = NULL;
		callbackFunc = NULL;
		user = NULL;
	}
};

class videoSrvNode {
public:
	videoSrvNode();
	void refreshStreamPuller();
	void sendToOnePuller(STREAM_DATA& sd, STREAM_PULLER& sp);
	void sendToAllPullers(STREAM_DATA& sd);
	void pushStream(STREAM_DATA& sd);
	void AsynPushStream(char* pData, int len, STREAM_INFO si);
	void doAsynPush();
	void addPuller(std::shared_ptr<TDS_SESSION> tdsSession, string fmt="");
	void addPuller(void* user, fp_onVideoStreamRecv callbackFunc, string fmt="");
	vector<STREAM_PULLER*> m_streamPuller; //拉流方
	fp_startStream m_streamPusher; //推流方
	STREAM_INFO m_streamInfo;
#ifdef ENABLE_FFMPEG
	videoCodec* m_videoCodec; //
#endif
	STREAM_DATA* rtImgBuff;
	std::mutex m_csRtImg;
	semaphore m_evtNewFrame;

	void convertFmt(STREAM_DATA& src, STREAM_DATA& puller);

	int GrayImgConverToRainbowRGBA(UCHAR* data, float* pSrc, int nPixel, float minval, float maxval);
	int DynamicRangeControl(float* pData, int w, int h, float& minVal, float& maxVal);
};