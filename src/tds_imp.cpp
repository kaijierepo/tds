#include "pch.h"
#include "tds_imp.h"
#include "tdspSrv.h"
#include "logger.h"
#include "prj.h"
#include "ds.h"
#include "ioSrv.h"
#include "xiaot/xiaot.h"
#include "io_server/ioDev.h"
#include "conf.h"
#include "mp.h"
#include "videoCodec.h"

string tdsEncoding = "utf8";

TDS_imp::TDS_imp()
{
	conf = &tdsConf;
}

bool TDS_imp::setEncodeing(string encoding)
{
	tdsEncoding = encoding;
	return true;
}

bool TDS_imp::run(string cmdline)
{
	//load tds.json
	tdsConf.loadConf();

	logger.setLogLevel(tdsConf.logLevel);
	LOG("current log Level is:" + tdsConf.logLevel);

	//startup xiaot
	xiaot.init();

	//startup tds modules
	prj.loadConf();
	ds.run();  //data server
#ifdef ENABLE_FFMPEG
	//rds.run(); //remote desktop server
#endif
	ioSrv.run();

	return true;
}

string TDS_imp::call(string method, string param)
{
	try {
		json jParam;
		if(param == "")
			jParam = nullptr;
		else
			jParam = json::parse(param);
		return tdsSrv.handleMethodCall(method, jParam);
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		return "error " + errorType;
	}
	
}

void TDS_imp::setRpcHandler(fp_rpcHandler handler)
{
	tdsSrv.m_pluginHandler = handler;
}

bool TDS_imp::sendToIoAddr(string ioAddr, char* p, int l)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		return d->sendData(p, l);
	}
	return false;
}

bool TDS_imp::setIoAddrRecvCallback(string ioAddr, void* user, fp_ioAddrRecv recvCallback)
{
	ioAddress sIoAddr;
	sIoAddr.FromString(ioAddr);
	ioDev* d = ioSrv.getIODev(sIoAddr);
	if (d)
	{
		d->m_pRecvCallback = recvCallback;
		d->m_pCallbackUser = user;
	}
	return true;
}

void TDS_imp::registerVideoTag(string tag, fp_startStream startStream,void*& mp)
{
	MP* pmp = prj.getMp(tag);
	if(pmp == NULL)
	{
		pmp = (MP*)prj.createChildMO(tag, MO_TYPE::mp);
		prj.m_mapAllMP[tag] = pmp;
	}
	pmp->m_valType = DATA_TYPE::video;
	pmp->m_streamPusher = startStream;
	mp = pmp;
}

void TDS_imp::pushStream(void* mp, char* pData, int len, STREAM_TYPE st, STREAM_INFO* si)
{
	MP* pmp = (MP*)mp;
	if (pmp->m_streamPuller == NULL)
		return;
	if (pmp->m_streamPuller->pTcpSession == NULL)
	{
		delete pmp->m_videoCodec;
		pmp->m_videoCodec = NULL;
		pmp->m_streamPuller = NULL;
		return;
	}

	if (st == ST_BMP)
	{
		if (pmp->m_videoCodec == NULL)
		{
			pmp->m_videoCodec = new videoCodec();
		}

		videoCodec& vc = *pmp->m_videoCodec;
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
		if (pmp->m_streamPuller->pTcpSession->iSendSucCount == 0)
		{
			pmp->m_streamPuller->send(vc.headerBuff, vc.iHeaderBuffLen);
		}
		pmp->m_streamPuller->send(vc.outputBuff, vc.iOutputLen);
		vc.iOutputLen = 0;
	}
	else if (st == ST_RGBA)
	{
		if (pmp->m_streamPuller->streamFmt == "rgba")//直接转发
		{
			pmp->m_streamPuller->send(pData,len);
		}
	}
}

void TDS_imp::log(char* text)
{
	if (tdsEncoding == "gb2312")
	{
		string strUtf8 = charCodec::ansi2Utf8(text);
		LOG(strUtf8);
	}
	else
		LOG(text);
}


