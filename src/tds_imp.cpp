#include "pch.h"
#include "tds_imp.h"
#include "tdspSrv.h"
#include "logger.h"
#include "prj.h"
#include "ds.h"
#include "ioSrv.h"
#include "xiaot/xiaot.h"

bool TDS_img::run(string cmdline)
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

string TDS_img::call(string method, string param)
{
	json jParam = json::parse(param);
	return tdsSrv.handleMethodCall(method, jParam);
}

bool TDS_img::sendToIoAddr(string ioAddr)
{
	return true;
}

bool TDS_img::setIoAddrRecvCallback(fp_ioAddrRecv recvCallback)
{
	return true;
}

void TDS_img::log(string text)
{
	LOG(text);
}


