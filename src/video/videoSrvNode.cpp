#include "videoSrvNode.h"
#include "json.hpp"

using json = nlohmann::json;

videoSrvNode::videoSrvNode()
{
#ifdef ENABLE_FFMPEG
	m_videoCodec = NULL;
#endif
}

void videoSrvNode::refreshStreamPuller()
{
	for (int i = 0; i < m_streamPuller.size(); i++)
	{
		STREAM_PULLER sp = m_streamPuller[i];
		if ((sp.tdsSession && sp.tdsSession->pTcpSession == NULL) ||
			(sp.tdsSession==NULL && sp.callbackFunc == NULL))
		{
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

void videoSrvNode::sendToPuller_rgba(char* pData, int len)
{
	//发送视频信息头
	for (int i = 0; i < m_streamPuller.size(); i++)
	{
		STREAM_PULLER sp = m_streamPuller[i];

		if (sp.tdsSession)
		{
			std::shared_ptr<TDS_SESSION> p = sp.tdsSession;

			//先检测session连接状态，失去连接的session释放引用
			if (!p->bConnected)
			{
				m_streamPuller.erase(m_streamPuller.begin() + i);
				i--;
				continue;
			}

			p->m_mutex.lock();//p->pTcpSession该指针不可多线程并发使用，加锁
			if (p->pTcpSession->iSendSucCount == 0)
			{
				json jSi;
				jSi["w"] = m_streamInfo.w;
				jSi["h"] = m_streamInfo.h;
				jSi["type"] = "rgba";
				string s = jSi.dump();
				p->send((char*)s.c_str(), s.length());
			}
			//if (p->streamFmt == "rgba")//直接转发
			//{
			p->send(pData, len);
			//}
			p->m_mutex.unlock();
		}
		else if(sp.callbackFunc)
		{
			sp.callbackFunc(pData, len, m_streamInfo);
		}
	}
}

void videoSrvNode::pushStream(char* pData, int len, STREAM_INFO si)
{
	refreshStreamPuller();
	if (m_streamPuller.size() == 0)
		return;

	m_streamInfo = si;

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

		vc.input_Bmp((char*)pData, len);
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
	else if (si.genicamPixelFmt == "rgba")
	{
		sendToPuller_rgba(pData, len);	
	}
	else if (si.genicamPixelFmt == "Mono8")
	{
		float* pFloatBuff = new float[si.w * si.h];
		for (int i = 0; i < si.w * si.h; i++)
		{
			pFloatBuff[i] = (unsigned char)pData[i];
		}

		float min; float max;
		DynamicRangeControl(pFloatBuff, si.w, si.h, min, max);
		UCHAR* pRGBA = new UCHAR[si.w * si.h*4];
		GrayImgConverToRainbowRGBA(pRGBA, pFloatBuff, si.w * si.h, min, max);
		sendToPuller_rgba((char*)pRGBA, si.w * si.h * 4);
		delete pFloatBuff;
		delete pRGBA;
	}
	else if (si.genicamPixelFmt == "Mono16" || si.genicamPixelFmt == "Mono12")
	{
		float* pFloatBuff = new float[si.w * si.h];
		for (int i = 0; i < si.w * si.h; i++)
		{
			unsigned short* pMono16 = (unsigned short*)pData;
			pFloatBuff[i] = pMono16[i];
		}

		float min; float max;
		DynamicRangeControl(pFloatBuff, si.w, si.h, min, max);
		UCHAR* pRGBA = new UCHAR[si.w * si.h * 4];
		GrayImgConverToRainbowRGBA(pRGBA, pFloatBuff, si.w * si.h, min, max);
		sendToPuller_rgba((char*)pRGBA, si.w * si.h * 4);
		delete pFloatBuff;
		delete pRGBA;
	}
}

bool asynPushThreadRunning = false;
void thread_pushStream(videoSrvNode* p)
{
	asynPushThreadRunning = true;
	p->doAsynPush();
}

void videoSrvNode::AsynPushStream(char* pData, int len, STREAM_INFO si)
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

void videoSrvNode::doAsynPush()
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


		pushStream(pFrm->pData,pFrm->len,pFrm->info);

		delete pFrm;
	}
}

void videoSrvNode::addPuller(std::shared_ptr<TDS_SESSION> tdsSession)
{
	STREAM_PULLER sp;
	sp.tdsSession = tdsSession;
	m_streamPuller.push_back(sp);
}

void videoSrvNode::addPuller(fp_onVideoStreamRecv callbackFunc)
{
	STREAM_PULLER sp;
	sp.callbackFunc = callbackFunc;
	m_streamPuller.push_back(sp);
}


int videoSrvNode::GrayImgConverToRainbowRGBA(UCHAR* data, float* pSrc, int nPixel, float minval, float maxval)
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

int videoSrvNode::DynamicRangeControl(float* pData, int w, int h, float& minVal, float& maxVal)
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