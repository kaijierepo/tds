#include "streamSrvNode.h"
#include "json.hpp"

using json = nlohmann::json;

streamSrvNode::streamSrvNode()
{
#ifdef ENABLE_FFMPEG
	m_videoCodec = NULL;
#endif
	m_streamPusher = NULL;
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
			jSi["pixelFmt"] = sd.info.genicamPixelFmt;
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
		STREAM_PULLER* sp = m_streamPuller[i];

		if (sd.info.genicamPixelFmt == sp->destData.info.genicamPixelFmt ||
			sp->destData.info.genicamPixelFmt == "")
		{
			sendToOnePuller(sd, *sp);
		}
		else
		{
			convertFmt(sd, sp->destData);
			sendToOnePuller(sp->destData, *sp);
		}
	}
}

void streamSrvNode::pushStream(STREAM_DATA& sd)
{
	refreshStreamPuller();
	if (m_streamPuller.size() == 0)
		return;
	m_streamInfo = sd.info;
	sendToAllPullers(sd);
}

bool asynPushThreadRunning = false;
void thread_pushStream(streamSrvNode* p)
{
	asynPushThreadRunning = true;
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

void streamSrvNode::addPuller(std::shared_ptr<TDS_SESSION> tdsSession, STREAM_INFO* si)
{
	STREAM_PULLER*  sp  = new STREAM_PULLER();
	sp->tdsSession = tdsSession;
	if(si)
	sp->destData.info = *si;
	m_streamPuller.push_back(sp);

	if (m_streamPusher)
		m_streamPusher->startStream();
}

void streamSrvNode::addPuller(void* user, fp_onVideoStreamRecv callbackFunc, STREAM_INFO* si)
{
	STREAM_PULLER* sp = new STREAM_PULLER();
	sp->callbackFunc = callbackFunc;
	sp->user = user;
	if (si)
		sp->destData.info = *si;
	m_streamPuller.push_back(sp);

	if (m_streamPusher)
		m_streamPusher->startStream();
}

void streamSrvNode::setPusher(STREAM_PUSHER* pusher)
{
	m_streamPusher = pusher;
	if (m_streamPuller.size() > 0)
	{
		m_streamPusher->startStream();
	}
}


void streamSrvNode::convertFmt(STREAM_DATA& src, STREAM_DATA& dest)
{
	STREAM_INFO& si = src.info;
	if (si.genicamPixelFmt == "bmp")
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
	else if (si.genicamPixelFmt == "Mono8" && dest.info.genicamPixelFmt == "rgba")
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
			dest.info.genicamPixelFmt = "rgba";
		}
			
		GrayImgConverToRainbowRGBA((UCHAR*)dest.pData, pFloatBuff, si.w * si.h, min, max);	
		delete pFloatBuff;
	}
	else if ((si.genicamPixelFmt == "Mono16" || si.genicamPixelFmt == "Mono12") && dest.info.genicamPixelFmt == "rgba")
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
			dest.info.genicamPixelFmt = "rgba";
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