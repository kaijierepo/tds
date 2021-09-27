#include "streamSrvNode.h"
#include "json.hpp"
#include "logger.h"

using json = nlohmann::json;

streamSrvNode::streamSrvNode()
{
#ifdef ENABLE_FFMPEG
	m_videoCodec = NULL;
#endif
	m_streamPusher = NULL;
	asynPushThreadRunning = false;
}

void streamSrvNode::refreshStreamPuller()
{
	for (int i = 0; i < m_streamPuller.size(); i++)
	{
		STREAM_PULLER* sp = m_streamPuller[i];
		if ((sp->tdsSession && sp->tdsSession->pTcpSession == NULL) ||
			(sp->tdsSession==NULL && sp->callbackFunc == NULL))
		{
			delete sp;
			m_streamPuller.erase(m_streamPuller.begin() + i);
			i--;
		}
		
	}

#ifdef ENABLE_FFMPEG
	if (m_streamPuller.size() == 0)
	{
		delete m_videoCodec;
		m_videoCodec = NULL;
	}
#endif
}

void streamSrvNode::sendToOnePuller(STREAM_DATA& sd, STREAM_PULLER& sp)
{
	if (sp.tdsSession)
	{
		std::shared_ptr<TDS_SESSION> p = sp.tdsSession;
		std::unique_lock<recursive_mutex> lock(p->m_mutex);//p->pTcpSession该指针不可多线程并发使用，加锁
		if (p->pTcpSession == NULL)
			return;
		if (p->pTcpSession->iSendSucCount == 0)
		{
			json jSi;
			jSi["w"] = sd.info.w;
			jSi["h"] = sd.info.h;
			jSi["pixelFmt"] = sd.info.pixelFmt;
			jSi["pixelSize"] = sd.info.pixelSize;
			string s = jSi.dump();
			p->send((char*)s.c_str(), s.length());
		}
		p->send(sd.pData, sd.len);
	}
	else if (sp.callbackFunc)
	{
		sp.callbackFunc(sd.pData, sd.len, sd.info, sp.user);
	}
}



void streamSrvNode::sendToAllPullers(STREAM_DATA& sd)
{
	//发送视频信息头
	for (int i = 0; i < m_streamPuller.size(); i++)
	{
		STREAM_PULLER* puller = m_streamPuller[i];

		//如果拉流者指定了帧率，且拉流帧率小于推流帧率，进行帧率转换
		//LOG("[debug] pusherRate:" + str::fromFloat(m_streamPusher->m_streamInfo.frameRate) + ",pullerRate:"+ str::fromFloat(puller->m_streamInfo.frameRate));
		if (puller->m_streamInfo.frameRate != 0 && 
			m_streamPusher->m_streamInfo.frameRate != 0 &&
			puller->m_streamInfo.frameRate < m_streamPusher->m_streamInfo.frameRate)
		{
			//LOG("[debug] changeFrameRate");
			puller->downSamplingInterval = m_streamPusher->m_streamInfo.frameRate / puller->m_streamInfo.frameRate;
			puller->downSamplingInterval = round(puller->downSamplingInterval);
			puller->frameIntervalIdx++;
			//LOG("[debug] downSamplingInterval:" + str::fromFloat(puller->downSamplingInterval) + ",frameIntervalIdx:" + str::fromInt(puller->frameIntervalIdx));
			if (puller->frameIntervalIdx < puller->downSamplingInterval)
			{
				//LOG("[debug] abandon frame");
				continue;
			}
				
			puller->frameIntervalIdx = 0;
		}
		
		//LOG("[debug] send frame");
		//拉流者未指定格式或者格式与推流者相同，直接发送
		if (m_streamPusher->m_streamInfo.pixelFmt == puller->destData.info.pixelFmt ||
			puller->m_streamInfo.pixelFmt == "")
		{
			sendToOnePuller(sd, *puller);
		}
		//先转化pixelFmt再发送
		else
		{
			convertFmt(sd, puller->destData);
			sendToOnePuller(puller->destData, *puller);
		}
	}
}

void STREAM_PUSHER::calcSrcFrameRate()
{
	m_pushFrameRateStatisCount++;
	if (m_pushFrameRateStatisTick == 0)
	{
		m_pushFrameRateStatisTick = timeopt::getTick();
	}
		
	else
	{
		time_t now = timeopt::getTick();
		time_t pass = now - m_pushFrameRateStatisTick;
		if (pass >= 2000)
		{
			frameRate = ((float)m_pushFrameRateStatisCount / pass) * 1000;
			m_pushFrameRateStatisTick = now;
			m_pushFrameRateStatisCount = 0;
		}
	}

}

void streamSrvNode::pushStream(STREAM_DATA& sd)
{
	refreshStreamPuller();
	if (m_streamPuller.size() == 0)
		return;
	sendToAllPullers(sd);
}


void thread_pushStream(streamSrvNode* p)
{
	p->asynPushThreadRunning = true;
	p->doAsynPush();
}

void streamSrvNode::asynPushStream(char* pData, int len, STREAM_INFO si)
{
	if (!asynPushThreadRunning)
	{
		thread t(thread_pushStream, this);
		t.detach();
	}

	m_csRtImg.lock();
	if (rtImgBuff)
	{
		delete rtImgBuff;
		rtImgBuff = NULL;
	}
	rtImgBuff = new STREAM_DATA();
	rtImgBuff->pData = new char[len];
	memcpy(rtImgBuff->pData, pData, len);
	rtImgBuff->len = len;
	rtImgBuff->info = si;
	m_evtNewFrame.notify();
	m_csRtImg.unlock();
}

void streamSrvNode::doAsynPush()
{
	while (1)
	{
		//取出当前帧
		m_evtNewFrame.wait();
		STREAM_DATA* pFrm = NULL;
		m_csRtImg.lock();
		pFrm = rtImgBuff;
		rtImgBuff = NULL;
		m_csRtImg.unlock();
		if (pFrm == NULL)continue;


		pushStream(*pFrm);

		delete pFrm;
	}
}



void streamSrvNode::addPuller(STREAM_PULLER* sp)
{
	m_streamPuller.push_back(sp);

	//if(m_streamPusher)
		//m_streamPusher->startStream();
}


void streamSrvNode::addPuller(std::shared_ptr<TDS_SESSION> tdsSession, STREAM_INFO* si)
{
	STREAM_PULLER*  sp  = new STREAM_PULLER();
	sp->tdsSession = tdsSession;
	if(si)
	sp->m_streamInfo = *si;
	addPuller(sp);
}

void streamSrvNode::addPuller(void* user, fp_onVideoStreamRecv callbackFunc, STREAM_INFO* si)
{
	STREAM_PULLER* sp = new STREAM_PULLER();
	sp->callbackFunc = callbackFunc;
	sp->user = user;
	if (si)
		sp->destData.info = *si;
	addPuller(sp);
}

void streamSrvNode::setPusher(STREAM_PUSHER* pusher)
{
	m_streamPusher = pusher;
	pusher->m_srvNode.push_back(this);
	if (m_streamPuller.size() > 0)
	{
		//m_streamPusher->startStream();
	}
}


void streamSrvNode::convertFmt(STREAM_DATA& src, STREAM_DATA& dest)
{
	STREAM_INFO& si = src.info;
	if (si.pixelFmt == "bmp")
	{
#ifdef ENABLE_FFMPEG
		if (m_videoCodec == NULL)
		{
			m_videoCodec = new videoCodec();
		}

		videoCodec& vc = *m_videoCodec;
		if (!vc.bInit)
		{
			vc.inConf.pixelFmt = AV_PIX_FMT_RGB24;
			vc.outConf.codecID = AV_CODEC_ID_VP9;
		}

		vc.input_Bmp((char*)src.pData, src.len);
		int iStreamLen = 0;
		char* pStream = NULL;
		vc.output();
		//发送视频头，web端mse收到该头才能正确解码
		// sendToPuller_h264
		//for (int i = 0; i < m_streamPuller.size(); i++)
		//{
		//	std::shared_ptr<TDS_SESSION> p = m_streamPuller[i];
		//	if (p->pTcpSession->iSendSucCount == 0)
		//	{
		//		p->send(vc.headerBuff, vc.iHeaderBuffLen);
		//	}
		//	p->send(vc.outputBuff, vc.iOutputLen);
		//}

		vc.iOutputLen = 0;
#endif
	}
	else if (si.pixelFmt == "Mono8" && dest.info.pixelFmt == "rgba")
	{
		float* pFloatBuff = new float[si.w * si.h];
		for (int i = 0; i < si.w * si.h; i++)
		{
			pFloatBuff[i] = (unsigned char)src.pData[i];
		}

		float min; float max;
		DynamicRangeControl(pFloatBuff, si.w, si.h, min, max);
		if (dest.pData == NULL)
		{
			dest.pData = new char[si.w * si.h * 4];
			dest.len = si.w * si.h * 4;
			dest.info = si;
			dest.info.pixelFmt = "rgba";
		}
			
		GrayImgConverToRainbowRGBA((UCHAR*)dest.pData, pFloatBuff, si.w * si.h, min, max);	
		delete pFloatBuff;
	}
	else if ((si.pixelFmt == "Mono16" || si.pixelFmt == "Mono12") && dest.info.pixelFmt == "rgba")
	{
		float* pFloatBuff = new float[si.w * si.h];
		for (int i = 0; i < si.w * si.h; i++)
		{
			unsigned short* pMono16 = (unsigned short*)src.pData;
			pFloatBuff[i] = pMono16[i];
		}

		float min; float max;
		DynamicRangeControl(pFloatBuff, si.w, si.h, min, max);
		if (dest.pData == NULL)
		{
			dest.pData = new char[si.w * si.h * 4];
			dest.len = si.w * si.h * 4;
			dest.info = si;
			dest.info.pixelFmt = "rgba";
		}
		GrayImgConverToRainbowRGBA((UCHAR*)dest.pData, pFloatBuff, si.w * si.h, min, max);
		delete pFloatBuff;
	}
	else
	{
		
	}
}

int streamSrvNode::GrayImgConverToRainbowRGBA(UCHAR* data, float* pSrc, int nPixel, float minval, float maxval)
{
	float range = maxval - minval;
	UCHAR mapVal = 0;
	float srcVal = 0;
	int i = 0;

	while (i < nPixel * 4)
	{
		srcVal = pSrc[i / 4];
		mapVal = (((srcVal - minval) * 255 / range));	//映射到0-255

		if (mapVal > 255 || mapVal < 0)
		{
			return 0;
		}

		if (mapVal <= 51)
		{
			data[i++] = 0;
			data[i++] = mapVal * 5;
			data[i++] = 255;
			data[i++] = 255;
		}
		else if (mapVal <= 102)
		{
			mapVal -= 51;
			data[i++] = 0;
			data[i++] = 255;
			data[i++] = 255 - mapVal * 5;
			data[i++] = 255;
		}
		else if (mapVal <= 153)
		{
			mapVal -= 102;
			data[i++] = mapVal * 5;
			data[i++] = 255;
			data[i++] = 0;
			data[i++] = 255;

		}
		else if (mapVal <= 204)
		{
			mapVal -= 153;
			data[i++] = 255;
			data[i++] = 255 - UCHAR(128.0 * mapVal / 51.0 + 0.5);
			data[i++] = 0;
			data[i++] = 255;
		}
		else
		{
			mapVal -= 204;
			data[i++] = 255;
			data[i++] = 127 - UCHAR(127.0 * mapVal / 51.0 + 0.5);
			data[i++] = 0;
			data[i++] = 255;
		}
	}

	return 0;
}

int streamSrvNode::DynamicRangeControl(float* pData, int w, int h, float& minVal, float& maxVal)
{
	float* pSortData = new float[w * h];
	memcpy(pSortData, pData, w * h * sizeof(float));
	std::sort(pSortData, pSortData + w * h, [](const float& a, const float& b) { return a < b; });
	int ignoreNum = 0.05 * w * h;

	//忽略最大和最小的5%
	minVal = pSortData[ignoreNum];
	maxVal = pSortData[w * h - 1 - ignoreNum];

	////静态阀值设置
	//if (TRUE == m_bStaticRange && maxVal < m_fThreshold)
	//{
	//	maxVal = m_fThreshold;
	//}

	for (int i = 0; i < w * h; i++)
	{
		if (pData[i] > maxVal)
		{
			pData[i] = maxVal;
		}

		if (pData[i] < minVal)
		{
			pData[i] = minVal;
		}
	}
	delete pSortData;
	return 0;
}

bool STREAM_PULLER::init()
{
	return false;
}

bool STREAM_PUSHER::pushStream(STREAM_DATA& sd)
{
	if (m_srvNode.size() == 0) return false;

	calcSrcFrameRate();

	//推流端帧率控制
	if (m_streamInfoConf.frameRate != 0 && frameRate != 0 &&
		frameRate > m_streamInfoConf.frameRate)
	{
		downSamplingInterval = frameRate / m_streamInfoConf.frameRate;
		downSamplingInterval = round(downSamplingInterval);
		frameIntervalIdx++;	
		if (frameIntervalIdx < downSamplingInterval)
		{
			return true;
		}
		frameIntervalIdx = 0;
		m_streamInfo.frameRate = m_streamInfoConf.frameRate;
	}
	//无控制使用源帧率
	else
	{
		m_streamInfo.frameRate = frameRate;
	}

	m_streamInfo.w = sd.info.w;
	m_streamInfo.h = sd.info.h;
	m_streamInfo.pixelFmt = sd.info.pixelFmt;
	m_streamInfo.pixelSize = sd.info.pixelSize;

	for (int i = 0; i < m_srvNode.size(); i++)
	{
		streamSrvNode* ssn = m_srvNode[i];
		ssn->asynPushStream(sd.pData,sd.len, m_streamInfo);
	}

	return false;
}
