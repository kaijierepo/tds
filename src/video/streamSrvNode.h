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
	STREAM_PUSHER()
	{
		downSamplingInterval = 1;
		frameIntervalIdx = 0;
		frameRate = 0;
		m_pushFrameRateStatisCount = 0;
		m_pushFrameRateStatisTick = 0;
	}
	virtual bool startStream(STREAM_INFO* si=NULL) = 0;
	virtual bool stopStream() = 0;
	virtual bool pushStream(STREAM_DATA& sd);
	void calcSrcFrameRate();
	vector<string> m_streamId; //可同时往多个id推流
	vector<streamSrvNode*> m_srvNode;
	STREAM_INFO m_streamInfoConf; //指定的推流参数.根据参数进行推流前转换。 如果需要设置推流的码率，在此处设置
	STREAM_INFO m_streamInfo; //根据m_streamInfoConf进行图像转换后， 实际的推流参数
	float frameRate; //码流源帧率。 可以在推流端和拉流端进行帧率转换
	time_t m_pushFrameRateStatisTick;
	int m_pushFrameRateStatisCount;
	float downSamplingInterval;
	int frameIntervalIdx;
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
	bool asynPushThreadRunning;
	string m_streamId;



	void convertFmt(STREAM_DATA& src, STREAM_DATA& puller);

	int GrayImgConverToRainbowRGBA(UCHAR* data, float* pSrc, int nPixel, float minval, float maxval);
	int DynamicRangeControl(float* pData, int w, int h, float& minVal, float& maxVal);
};