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
	STREAM_INFO m_streamInfo;
};

class STREAM_PULLER {
public:
	std::shared_ptr<TDS_SESSION> tdsSession;
	fp_onVideoStreamRecv callbackFunc;
	void* user;

	STREAM_DATA destData;
	STREAM_INFO m_streamInfo; //拉流者请求的码流参数
	float downSamplingInterval; //拉流者可以指定帧率，但必须小于推流的帧率
	int frameIntervalIdx; // 0 - downSamplingInterval-1

	STREAM_PULLER()
	{
		tdsSession = NULL;
		callbackFunc = NULL;
		user = NULL;
		downSamplingInterval = 1;
		frameIntervalIdx = 0;
	}

	bool init();
};

class streamSrvNode {
public:
	streamSrvNode();
	void refreshStreamPuller();
	void sendToOnePuller(STREAM_DATA& sd, STREAM_PULLER& sp);
	void sendToAllPullers(STREAM_DATA& sd);
	void calcPusherFrameRate();
	void pushStream(STREAM_DATA& sd);
	void asynPushStream(char* pData, int len, STREAM_INFO si);
	void doAsynPush();
	void addPuller(STREAM_PULLER* sp);
	void addPuller(std::shared_ptr<TDS_SESSION> tdsSession, STREAM_INFO* si=NULL);
	void addPuller(void* user, fp_onVideoStreamRecv callbackFunc, STREAM_INFO* si=NULL);
	void setPusher(STREAM_PUSHER* pusher);
	vector<STREAM_PULLER*> m_streamPuller; //拉流方
	STREAM_PUSHER* m_streamPusher;
	
#ifdef ENABLE_FFMPEG
	videoCodec* m_videoCodec; //
#endif
	STREAM_DATA* rtImgBuff;
	std::mutex m_csRtImg;
	semaphore m_evtNewFrame;

	time_t m_pushFrameRateStatisTick;
	int m_pushFrameRateStatisCount;

	void convertFmt(STREAM_DATA& src, STREAM_DATA& puller);

	int GrayImgConverToRainbowRGBA(UCHAR* data, float* pSrc, int nPixel, float minval, float maxval);
	int DynamicRangeControl(float* pData, int w, int h, float& minVal, float& maxVal);
};