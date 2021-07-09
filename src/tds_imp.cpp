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


