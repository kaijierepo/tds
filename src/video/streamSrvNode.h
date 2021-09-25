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

class STREAM_PUSHER {
public:
	virtual bool startStream() = 0;
	virtual bool stopStream() = 0;
	string m_streamId;
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

class streamSrvNode {
public:
	streamSrvNode();
	void refreshStreamPuller();
	void sendToOnePuller(STREAM_DATA& sd, STREAM_PULLER& sp);
	void sendToAllPullers(STREAM_DATA& sd);
	void pushStream(STREAM_DATA& sd);
	void asynPushStream(char* pData, int len, STREAM_INFO si);
	void doAsynPush();
	void addPuller(std::shared_ptr<TDS_SESSION> tdsSession, STREAM_INFO* si=NULL);
	void addPuller(void* user, fp_onVideoStreamRecv callbackFunc, STREAM_INFO* si=NULL);
	void setPusher(STREAM_PUSHER* pusher);
	vector<STREAM_PULLER*> m_streamPuller; //À­Á÷·½
	STREAM_PUSHER* m_streamPusher;
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