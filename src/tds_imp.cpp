#include "pch.h"
#include "tds_imp.h"
#include "tdspSrv.h"
#include "logger.h"
#include "prj.h"
#include "ds.h"
#include "ioSrv.h"
#include "xiaot/xiaot.h"

string tdsEncoding = "utf8";

bool TDS_imp::setEncodeing(string encoding)
{
	tdsEncoding = encoding;
	return false;
}

bool TDS_imp::run(string cmdline)
{
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
		json jParam = json::parse(param);
		return tdsSrv.handleMethodCall(method, jParam);
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		return "error " + errorType;
	}
	
}

bool TDS_imp::sendToIoAddr(string ioAddr)
{
	return true;
}

bool TDS_imp::setIoAddrRecvCallback(fp_ioAddrRecv recvCallback)
{
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


