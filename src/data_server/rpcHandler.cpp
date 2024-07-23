#include "pch.h"
#include "rpcHandler.h"
#include "prj.h"
#include "as.h"
#include "mp.h"
#include  "reverseInterface.h"
#include "logger.h"
#include "tdb.h"
#include <json.hpp>
#include "ioSrv.h"
#include "tdb.h"
#include "logger.h"
#include "ioChan.h"
#include "userMng.h"
#include "logServer.h"
#include "scriptManager.h"
#include "base64.h"
#include "httplib.h"
#include "webSrv.h"
#include "ioDev_camera.h"
#include "statusServer.h"
#include <io_server/ioDev_tdsp.h>

#ifdef _WIN32
	#include <shellapi.h>
	#include "memDiag.h"
#endif

rpcHandler rpcSrv;

void msgSinker_rpcHandler(MODULE_BUS_MSG& msg)
{
	if (msg.eventName == "ioDev.offline")
	{
		json jMsg = json::parse(msg.content);
		json j;
		j["addr"] = jMsg["ioAddr"];
		j["type"] = msg.eventName;
		rpcSrv.notify("devOffline", j);
	}
}

size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream) {
	if (stream == nullptr) return 0;
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

bool DownloadHTTPFile(std::string url, std::string file_save_path)//待下载文件的URL, 存放到本地的路径
{
	url = charCodec::gb_to_utf8(url);
	//HRESULT hr = URLDownloadToFile(NULL, url.c_str(), file_save_path.c_str(), 0, NULL);
	//return hr == S_OK;
	return false;
}

rpcHandler::rpcHandler()
{
	m_pluginHandler = NULL;
	tds->rpcServer = this;
}

rpcHandler::~rpcHandler()
{

}


bool rpcHandler::init()
{
	return true;
}





//string rpcHandler::ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession)
//{
//	//使用正则搜寻 ${XXX}
//	//"${src_ip}" 替换成 pSession->ip
//	string str = strIn;
//	if (str.find("$srcIp$") != str.npos) {
//		string ip = pSession->ip;
//		string::size_type pos = pSession->ip.find(":");
//		if (std::string::npos != pos) {
//			ip = pSession->ip.substr(0, pos);
//		}
//		str = str::replace(str, "$src_ip$", ip);
//		}
//
//	str = str::replace(str, "$dbPath$", db.m_path);
//	str = str::replace(str, "$confPath$", tds->conf->confPath);
//
//	return str;
//}


bool rpcHandler::handleMethodCall_OSFunc(string method, json& params, RPC_RESP& rpcResp)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	//文件操作
	if (method == "fs.readFile")
	{
		if (params["type"] != nullptr && params["type"].get<string>() == "binary")
		{
			char* p = NULL; int len = 0;
			if (fs::readFile(params["path"].get<string>(), p, len))
			{
				rpcResp.setResult(p, len);
				if (p)
					delete p;
			}
		}
		else
		{
			if (fs::readFile(params["path"], result))
			{
				json j = result;
				result = j.dump();
			}
			else
			{
				if (!fs::fileExist(params["path"]))
				{
					error = makeRPCError(OS_fileNotExist, "file not exist");
				}
				else
					error = makeRPCError(TEC_FAIL, "fail");
			}
		}
	}
	else if (method == "fs.deleteFile")
	{
		string p = params["path"].get<string>();
		if (fs::deleteFile(p)) {
			result = "\"ok\"";
		}
		else {
			error = makeRPCError(TEC_FAIL, "fail");
		}
	}
	else if (method == "fs.writeFile")
	{
		string p = params["path"].get<string>();

		if (params["data"] != nullptr)
		{
			string d = params["data"].get<string>();
			string encode = "";
			if (params.contains("encode")) {
				encode = params["encode"].get<string>();
			}
			if (encode == "base64") {
				unsigned char* out = new unsigned char[d.length()];
				int len = base64_decode(d.c_str(),(int) d.length(), out);
				if (fs::writeFile(p,(char*)out, len))
				{
					result = "\"ok\"";
				}
				else
				{
					error = makeRPCError(TEC_FAIL, "fail");
				}
			}
			else {
				string ap = fs::toAbsolutePath(p);
				if (fs::writeFile(ap, d))
				{
					result = "\"ok\"";
				}
				else
				{
					error = makeRPCError(TEC_FAIL, "fail");
				}
			}
		}
	}
#ifdef _WIN32
	else if (method == "fs.getCurDir")
	{
		WCHAR buff[300] = { 0 };
		GetCurrentDirectoryW(300, buff);
		wstring s = buff;
		json j = charCodec::utf16_to_utf8(s);
		result = j.dump();
	}
#endif
	else if (method == "fs.getFileList")
	{
		string path = params["path"];
		bool includeFolder = false;
		bool recursive = false;
		if (params["includeFolder"] != nullptr)
			includeFolder = params["includeFolder"].get<bool>();
		if (params["recursive"] != nullptr)
			recursive = params["recursive"].get<bool>();
		vector<string> fl;
		fs::getFileList(fl, path, includeFolder, recursive);
		json j = fl;
		result = j.dump();
	}
	else if (method == "fs.exploreFolder")
	{
		vector<fs::FILE_INFO> fileList;
		vector<fs::FILE_INFO> folderList;

		string path = tds->conf->getStr("fsRoot", "");
		if (path == "") {
			error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "file service is not started,config fsRoot param in tds.ini");
			return true;
		}
		path = fs::toAbsolutePath(path);


		if (params.contains("path")) {
			string subPath = params["path"];
			path = path + "/" + subPath;
		}

		fs::getFileList(fileList, path);
		fs::getFolderList(folderList, path);
		json infoList = json::array();
		for (int i = 0; i < folderList.size(); i++) {
			fs::FILE_INFO& fi = folderList[i];
			json j;
			j["name"] = fi.name;
			j["size"] = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"] = true;
			infoList.push_back(j);
		}
		for (int i = 0; i < fileList.size(); i++) {
			fs::FILE_INFO& fi = fileList[i];
			json j;
			j["name"] = fi.name;
			j["size"] = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"] = false;
			infoList.push_back(j);
		}
		result = infoList.dump();
	}
	else if (method == "com.open")
	{
		result = rpc_openCom(params, error);
	}
	else if (method == "com.close")
	{
		result = rpc_closeCom(params, error);
	}
	else if (method == "com.list")
	{
		result = rpc_com_list(params, error);
	}
	else if (method == "fs.openFileDlg")
	{
		//std::thread t(openFileDlgThread, params);
		//t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.saveFileDlg")
	{
		//std::thread t(saveFileDlgThread, params);
		//t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.selectFolderDlg")
	{
		//std::thread t(selectFolderDlgThread, params);
		//t.detach();
		result = "\"ok\"";
	}
#ifdef _WIN32
	else if (method == "fs.openFolder")
	{
		string s = params["path"];
		s = str::replace(s, "/", "\\");
		wstring ws = charCodec::utf8_to_utf16(s);
		ShellExecuteW(NULL, L"open", L"explorer.exe", ws.c_str(), NULL, SW_SHOWNORMAL);
		result = "\"ok\"";
	}
	else if (method == "fs.openDEFolder")
	{
		string tag = params["tag"];
		string time = params["time"];
		DB_TIME dbt;
		dbt.fromStr(time);
		string path = db.m_path + db.getPath_deFile(tag, dbt);

		path = str::replace(path, "/", "\\");
		wstring ws = charCodec::utf8_to_utf16(path);
		ShellExecuteW(NULL, L"open", L"explorer.exe", ws.c_str(), NULL, SW_SHOWNORMAL);
		result = "\"ok\"";
	}
	else if (method == "ui.maximize")
	{
		SendMessage((HWND)tds->uiWnd, WM_SYSCOMMAND, SC_MAXIMIZE, NULL);
		rpcResp.result = "\"ok\"";
	}
	else if (method == "ui.minimize")
	{
		SendMessage((HWND)tds->uiWnd, WM_SYSCOMMAND, SC_MINIMIZE, NULL);
		rpcResp.result = "\"ok\"";
		LOG("[debug]ui.minimize");
	}
	else if (method == "ui.close")
	{
		SendMessage((HWND)tds->uiWnd, WM_SYSCOMMAND, SC_CLOSE, NULL);
		rpcResp.result = "\"ok\"";
		LOG("[debug]ui.close");
	}
	else if (method == "getServerStatus") {
		json j;
		j["cpu"] = statusSrv.m_srvStatus.cpu;
		j["mem"] = statusSrv.m_srvStatus.mem;
		j["thread"] = statusSrv.m_srvStatus.thread;
		j["handle"] = statusSrv.m_srvStatus.handle;
		rpcResp.result = j.dump();
	}
#endif
	else
	{
		bHandled = false;
	}

	return bHandled;
}

//此处有两种可能的设计，萤石云设计成ioDev，通过ioDev中转
//但这样子服务的控制还要先发给子服务，子服务再发给萤石云，有点多余
//目前采用主服务获取到子服务的萤石云配置后，直接发给萤石云
bool rpcHandler::handleMethodCall_ptz_cloud(string method, MP* pmp, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
#ifdef ENABLE_OPENSSL
	if (prj.m_mapEzvizAccess.find(pmp->m_serialNo) != prj.m_mapEzvizAccess.end()) {
		string addr = "https://open.ys7.com";
		httplib::Client cli(addr);
		cli.enable_server_certificate_verification(false); //这句不加上可能返回 serverSSLVerification错误
		EZVIZ_ACCESS_INFO& info = prj.m_mapEzvizAccess[pmp->m_serialNo];
		//操作命令：0 - 上，1 - 下，2 - 左，3 - 右，4 - 左上，5 - 左下，6 - 右上，7 - 右下，8 - 放大，9 - 缩小，10 - 近焦距，11 - 远焦距
		string ezvizDir;
		if (method.find("start") != string::npos) {
			string dir = params["dir"];
			if (method == "startPanTilt")
			{
				if (dir == "up") ezvizDir = "0";
				else if (dir == "down") ezvizDir = "1";
				else if (dir == "left") ezvizDir = "2";
				else if (dir == "right") ezvizDir = "3";
			}
			else if (method == "startZoom") {
				if (dir == "in") ezvizDir = "8";
				else if (dir == "out") ezvizDir = "9";
			}
			else if (method == "startFocus") {
				if (dir == "near") ezvizDir = "10";
				else if (dir == "far") ezvizDir = "11";
			}

			params = {
				{"accessToken",info.token},
				{"deviceSerial",info.serialNo},
				{"channelNo","1"},
				{"direction",ezvizDir},
				{"speed","1"}
			};

			auto resp = cli.Post("/api/lapp/device/ptz/start", params);
			if (resp != nullptr) {
			}
		}
		else {
			params = {
				{"accessToken",info.token},
				{"deviceSerial",info.serialNo},
				{"channelNo","1"}
			};

			auto resp = cli.Post("/api/lapp/device/ptz/stop", params);
		}
		rpcResp.result = "\"ok\"";
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "Ezviz access info not foud");
	}
#endif
	return true;
}

//通过ioDev进行
bool rpcHandler::handleMethodCall_ptz_ioDev(string method, string tag,json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	ioDev* p = ioSrv.getIODevByTag(tag);
	if (!p) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "no io device bind to specified tag");
		return true;
	}

	if (!p->isCamera()) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "io device binded is not a camera");
		return true;
	}

	ioDev_camera* pCam = (ioDev_camera*)p;
	if (method == "startPanTilt")
	{
		string dir = params["dir"];
		float panSpeed = params["panSpeed"].get<float>();
		float tiltSpeed = params["tiltSpeed"].get<float>();
		pCam->ptz_startMove(dir, panSpeed, tiltSpeed);

		LOG("移动云台,方向:%s,panSpeed:%.2f,tiltSpeed:%.2f", dir.c_str(), panSpeed, tiltSpeed);

		pCam->isMoving = true;
		pCam->startMoveTime = timeopt::now();

		if (params.contains("time")) {
			int time = params["time"].get<int>();
			json paramAsynCall;
			paramAsynCall["tag"] = tag;
			tds->callAsyn("stopPanTilt", paramAsynCall, time);
		}
	}
	else if (method == "stopPanTilt")
	{
		if(!pCam->isMoving)
			timeopt::sleepMilli(400);  //解决前端点击按钮快速发送一个开始停止云台命令的情况，startPanTilt的代码处理逻辑多，会导致发送出去在 stopMove的后面
		pCam->ptz_stopMove();
		pCam->isMoving = false;
	}
	else if (method == "startZoom")
	{
		string dir = params["dir"];
		float speed = 0;
		if (params.contains("speed")) {
			speed = params["speed"].get<float>();
		}
		pCam->ptz_startZoom(dir,speed);

		if (params.contains("time")) {
			int time = params["time"].get<int>();
			json paramAsynCall;
			paramAsynCall["tag"] = tag;
			tds->callAsyn("stopZoom", paramAsynCall, time);
		}
	}
	else if (method == "stopZoom")
	{
		pCam->ptz_stopZoom();
	}
	else if (method == "startFocus")
	{
		string dir = params["dir"];
		float speed = 0;
		if (params.contains("speed")) {
			speed = params["speed"].get<float>();
		}
		pCam->ptz_startFocus(dir);

		if (params.contains("time")) {
			int time = params["time"].get<int>();
			json paramAsynCall;
			paramAsynCall["tag"] = tag;
			tds->callAsyn("stopFocus", paramAsynCall, time);
		}
	}
	else if (method == "stopFocus")
	{
		pCam->ptz_stopFocus();
	}
	else if (method.find("Preset") != string::npos) {
		if (!params["presetIndex"].is_number_integer()) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "int param presetIndex must be specified");
			return true;
		}
		int idx = params["presetIndex"].get<int>();

		if (method == "gotoPreset")
		{
			pCam->ptz_gotoPreset(idx);
		}
		else if (method == "addPreset")
		{
			pCam->ptz_addPreset(idx);
		}
		else if (method == "deletePreset")
		{
			pCam->ptz_deletePreset(idx);
		}
	}
	
	rpcResp.result = "\"ok\"";

	return true;
}



bool rpcHandler::handleMethodCall_video(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;

	if (method == "getStreamInfo")
	{
		//result = rpc_getStreamInfo(params, error);
	}
	else if (method == "getYsAccessInfo") {
		string tag = params["tag"];
		MP* pmp = prj.GetMPByTag(tag, session.language);
		if (pmp) {
			map<string, EZVIZ_ACCESS_INFO>::iterator iter = prj.m_mapEzvizAccess.find(pmp->m_serialNo);
			if (iter != prj.m_mapEzvizAccess.end()) {
				EZVIZ_ACCESS_INFO& info = iter->second;
				json jrlt;
				jrlt["token"] = info.token;
				jrlt["serialNo"] = info.serialNo;
				rpcResp.result = jrlt.dump();
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "can not found access info of specified tag");
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound,"specified tag not found");
		}
	}
#ifdef ENABLE_GENICAM
	else if (method == "setStream")
	{
		result = rpc_setStream(params, error);
	}
	else if (method == "genicam.doCmd")
	{
	if (firstDiscoverGenicam)
	{
		firstDiscoverGenicam->doCmd(params["name"]);
	}
	}
	else if (method == "genicam.setParam")
	{
	string ioAddr = params["ioAddr"];
	ioDev* p = ioSrv.getIODev(ioAddr);
	if (p && p->m_devType == IO_DEV_TYPE::DEV::genicam)
	{
		ioDev_genicam* piod = (ioDev_genicam*)p;

		string name = params["name"];
		json val = params["val"];
		bool isEnum = false;
		if (params["isEnum"] != nullptr && params["isEnum"].get<bool>() == true)
			isEnum = true;
		piod->setParam(name, val, isEnum);
		rpcResp.result = "\"ok\"";
	}
	}
	else if (method == "genicam.getParam")
	{
	if (firstDiscoverGenicam)
	{

	}
	}
#endif
	else if (method == "startPanTilt" ||
				method == "stopPanTilt" ||
				method == "startZoom" ||
				method == "stopZoom" ||
				method == "startFocus" ||
				method == "stopFocus" ||
				method == "gotoPreset" ||
				method == "addPreset" ||
				method == "deletePreset" ||
				method == "openStream" ||
				method == "keepStream" ||
				method == "closeStream" )
	{
		string tag, rootTag;
		if (!parseParam_tag(params, rpcResp, session, tag, rootTag))
			return true; 

		MP* pObj =(MP*) prj.queryObj(tag,session.language);
		if (!pObj) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "object of specified tag not found");
			return true;
		}


		if (method.find("start") != string::npos) {
			string dir = params["dir"];
			string info;
			if (dir == "up")
				info = "上移";
			else if (dir == "down")
				info = "下移";
			else if (dir == "left")
				info = "左移";
			else if (dir == "right")
				info = "右移";
			else if (dir == "in")
				info = "放大";
			else if (dir == "out")
				info = "缩小";
			else if (dir == "near")
				info = "聚焦拉近";
			else if (dir == "far")
				info = "聚焦拉远";

			//操控日志记录
			json logParams;
			logParams["src"] = "用户:" + session.user;
			logParams["object"] = tag;
			logParams["type"] = "云台控制";
			logParams["org"] = session.org;
			logParams["host"] = session.remoteAddr;
			logParams["info"] = info;
			logSrv.rpc_addLog(logParams, session);
		}
		else if(method.find("Preset") != string::npos) {
			
			string info;
			if (method == "gotoPreset") {
				if (!params["presetIndex"].is_number_integer()) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "param presetIndex must be specified,and must be int type");
					return true;
				}

				string presetIndex = params["presetIndex"].dump();
				info = "调用预置位:" + presetIndex;
			}
			else if (method == "deletePreset") {
				if (!params["presetIndex"].is_number_integer()) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "param presetIndex must be specified,and must be int type");
					return true;
				}

				string presetIndex = params["presetIndex"].dump();
				info = "删除预置位:" + presetIndex;
			}
			else {
				info = "添加预置位";
			}
				

			//操控日志记录
			json logParams;
			logParams["src"] = "用户:" + session.user;
			logParams["object"] = tag;
			logParams["type"] = "云台控制";
			logParams["org"] = session.org;
			logParams["host"] = session.remoteAddr;
			logParams["info"] = info;
			logSrv.rpc_addLog(logParams, session);
		}
		

		//通过第三方云平台进行控制。如萤石云。该模式无需转发到子服务
		if (pObj->m_mediaSrcType == "ezviz") {
			if (method == "startPanTilt" ||
				method == "stopPanTilt" ||
				method == "startZoom" ||
				method == "stopZoom" ||
				method == "startFocus" ||
				method == "stopFocus" ||
				method == "gotoPreset" ||
				method == "addPreset" ||
				method == "deletePreset"  )
			{
				handleMethodCall_ptz_cloud(method, pObj, params, rpcResp, session);
				return true;
			}
			else if (method == "openStream") {
				rpcResp.result = RPC_OK;
				return true;
			}
		}


		//TDS系统内控制。 有转发给子服务和直接处理两种情况
		ioDev* childTds = ioSrv.getOwnerChildTdsDev(tag);
		//转发给子服务
		if (childTds) {
			string childTdsTag = childTds->m_strTagBind;
			tag = TAG::trimRoot(tag, childTdsTag);
			json paramsChild = params;
			paramsChild["tag"] = tag;
			json childRlt, childErr;
			childTds->call(method, paramsChild, nullptr, childRlt, childErr);
			LOG("[warn][服务级联   ]转发摄像头控制指令\r\n" + session.req);
			if (childRlt != nullptr) {
				json jRlt;
				if (childTds->pIOSession) {
					jRlt["ip"] = childTds->pIOSession->getRemoteIP();
				}
				rpcResp.result = jRlt.dump();
			}
			else{
				rpcResp.error = childErr.dump();
			}
		}
		//直接处理
		else if (method == "startPanTilt" ||
			method == "stopPanTilt" ||
			method == "startZoom" ||
			method == "stopZoom" ||
			method == "startFocus" ||
			method == "stopFocus" ||
			method == "gotoPreset" ||
			method == "addPreset" ||
			method == "deletePreset")
		{
			MP* pmp = pObj;
			if (!pmp) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
			}
			else {
					handleMethodCall_ptz_ioDev(method, tag, params, rpcResp, session);
			}
			return true;
		}
		else if (method == "openStream") {
			MP* pmp = pObj;
			if (!pmp) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
				return true;
			}
			
			string pushTo;
			if (params.contains("pushTo")) {
				string tag = params["pushTo"];
				string pushToIP = session.remoteIP;
				if (params["pushToIP"].is_string()) {
					pushToIP = params["pushToIP"];
				}

				pushTo = "rtsp://" + pushToIP + "/stream/" + tag;
			}
			bool opend = prj.openStream(tag, pushTo);

			if (opend)
				rpcResp.result = RPC_OK;
			else
				rpcResp.error = RPC_FAIL;
			return true;
		}
		else if (method == "pushStream") {
			
			return true;
		}
		else if (method == "keepStream") {
			TIME st;
			timeopt::now(&st);
			reverseInterface.m_mapPullerActive[tag] = st;
			rpcResp.result = "\"ok\"";
		}
		else if (method == "closeStream") {
			bool opend = prj.closeStream(tag);

			if (opend)
				rpcResp.result = RPC_OK;
			else
				rpcResp.error = RPC_FAIL;
		}
	}
	else
	{
		bHandled = false;
	}

	return bHandled;
}



//参见核心概念，位号表示法
//https://www.liangtusoft.com/doc/#/核心概念?id=位号表示法
//sysTag = session.org + rootTag + tag
bool rpcHandler::parseParam_tag(json& params, RPC_RESP& rpcResult, RPC_SESSION session, string& tag, string& rootTag)
{
	if (!params.contains("tag"))
	{
		rpcResult.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param : tag");
		return false;
	}

	tag = params["tag"];


	//获取查询根
	rootTag = "";
	if (params["rootTag"] != nullptr)
		rootTag = params["rootTag"].get<string>();
	rootTag = TAG::addRoot(rootTag, session.org);


	tag = TAG::addRoot(tag, rootTag);
	return true;
}

bool rpcHandler::handleMethodCall_db(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;

	
	if (method.find("db.") != string::npos)
	{
		//insert不进行 time参数校验
		if (method == "db.insert")
		{
			string s = params.dump();
			db.rpc_db_insert(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org);
		}
		else if (method == "db.renameFolder") {
			string oldName = params["old"].get<string>();
			string newName = params["new"].get<string>();

			vector<fs::FILE_INFO> vec;
			fs::getFolderList(vec, db.m_path,true);
			try
			{
				for (int i = 0; i < vec.size(); i++) {
					fs::FILE_INFO& fi = vec[i];
					if (fi.name.find(oldName) != string::npos) {
						string newPath = str::replace(fi.path, oldName, newName);
						wstring wop = charCodec::utf8_to_utf16(fi.path);
						wstring wnp = charCodec::utf8_to_utf16(newPath);
						filesystem::rename(wop, wnp);
					}
				}
				rpcResp.result = RPC_OK;
			}
			catch (const std::exception& e)
			{
				json jErr = e.what();
				rpcResp.error = jErr.dump();
			}
		}
		else if (!params.contains("time"))
		{
			error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param : time");
		}
		else if (method == "db.select")
		{
			string s = params.dump();
			db.rpc_db_select(s, rpcResp.result,rpcResp.error,rpcResp.info, session.org);
		}
		else if (method == "db.update")
		{
			string s = params.dump();
			db.rpc_db_update(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org);
		}
		else if (method == "db.delete")
		{
			string s = params.dump();
			db.rpc_db_delete(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org);
		}
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}


vector<std::shared_ptr<TDS_SESSION>>  concurrentTestVec;

void threadAdd() {
	while (1)
	{
		std::shared_ptr<TDS_SESSION> t(new TDS_SESSION);
		concurrentTestVec.push_back(t);
	}
}

void threadErase() {
	while (1)
	{
		concurrentTestVec.erase(concurrentTestVec.begin());
	}
}


void rpcHandler::rpc_getApiSessionStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION session) {
	WebServer* pWs = getWebServer(session.localPort, session.isHttps);
	if (pWs) {
		pWs->m_csWsSessions.lock();
		json jList = json::array();
		for (auto i : pWs->m_wsSessions) {
			std::shared_ptr<TDS_SESSION> p = i.second;
			json jSession;
			jSession["ip"] = p->remoteIP;
			jSession["port"] = p->remotePort;
			jSession["protocol"] = "ws";
			jSession["reqCount"] = "";
			jSession["lastRecvTime"] = p->sLastRecvTime;
			jSession["recved"] = p->recvedLen.load();
			jSession["lastSendTime"] = p->sLastSendTime;
			jSession["sended"] = p->sendedLen.load();
			jList.push_back(jSession);
		}
		pWs->m_csWsSessions.unlock();

		//http是短连接，按照ip地址统计，目前该数组不删除
		for (auto i : pWs->m_httpSessions) {
			SESSION_STATIS* p = i.second;
			json jSession;
			jSession["ip"] = p->remoteIP;
			jSession["port"] = p->remotePort;
			jSession["protocol"] = "http";
			jSession["reqCount"] = p->reqCount;
			jSession["lastRecvTime"] = p->lastRecvTime.toStr();
			jSession["recved"] = p->recv;
			jSession["lastSendTime"] = "";
			jSession["sended"] = p->send;
			jList.push_back(jSession);
		}

		rpcResp.result = jList.dump(2);
	}
}

bool rpcHandler::handleMethodCall_debugFunc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getIoSessions")
	{
		ioSrv.rpc_getSessionStatus(params,rpcResp,session);
	}
	else if (method == "getApiSessions")
	{
		rpc_getApiSessionStatus(params, rpcResp, session);
	}
	else if (method == "captureFrame")
	{

	}
	else if (method == "getSessionBuff")
	{
		string remoteAddr = params["remoteAddr"].get<string>();
		shared_ptr<TDS_SESSION> pSession = ioSrv.getTDSSession(remoteAddr);

		if (pSession != nullptr)
		{
			json buff;
			buff["len"] = pSession->m_alBuf.iStreamLen;
			buff["data"] = str::bytesToHexStr(pSession->m_alBuf.stream, pSession->m_alBuf.iStreamLen);

			rpcResp.result = buff.dump(2);
		}
		else
		{
			json jError = "session not found";
			rpcResp.error = jError.dump();
		}
	}
	else if (method == "stopCycleAcq")
	{
		ioSrv.m_stopCycleAcq = true;
	}
	else if (method == "startCycleAcq")
	{
		ioSrv.m_stopCycleAcq = false;
	}
	else if (method == "sendToSession")
	{
		string tdsSession = params["sessionAddr"].get<string>();
		string data = params["data"].get<string>();
		shared_ptr<TDS_SESSION> pDestSession = ioSrv.getTDSSession(tdsSession);
		if (pDestSession == nullptr)
		{
			rpcResp.error = "\"session not found," +  tdsSession +  "\"";
			return true;
		}
		else {
			size_t iSended = pDestSession->send((char*)data.c_str(), data.length());
			if (iSended > 0)
				rpcResp.result = "\"ok\"";
			else
				rpcResp.error = "\"fail\"";
			return true;
		}
	}
#ifdef TEST
	else if (method == "testCrash")
	{
		rpcResp.result = "\"ok\"";


		//程序崩溃
		int i = 13; int j = 0; int m = i /(1-1+ j*20);
		LOG("[debug]tds.Crash" + str::fromInt(m));
	}
	else if (method == "testCrash1")
	{
		rpcResp.result = "\"ok\"";


		//该仿真可以仿真出dumpCatch无法抓取的奔溃
		//windows Server 2008 R2 enterprize有时会显示 程序当前遇到问题需要关闭的对话框,程序卡住； 有时能够退出截取dump
		//win10 直接退出，dumpCatch不能截取到dump。能不能出现截取到 dump 的现象可能还需更多测试
		thread t(threadAdd);
		t.detach();

		thread t2(threadErase);
		t2.detach();
	}
	else if (method == "testCrash2")
	{
		rpcResp.result = "\"ok\"";


		vector<string> vec;
		vec.erase(vec.begin());
	}
	//json异常字符串解析奔溃问题
	else if (method == "testCrash3")
	{
		string s = "{\"123\":\"123\"}";
		char sTmp[200] = { 0 };
		memcpy(sTmp, s.c_str(), s.length());
		sTmp[2] = -74;
		sTmp[3] = 116;
		s = sTmp;
		try {
			json j = json::parse(s);
		}
		catch (std::exception& e)
		{
			//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符
			//所以也非法。后面 jError如果使用这段字符串dump会导致奔溃。不知道如何展示这个错误信息好
			char* szError = (char*)e.what();
			string errorType = "";
			if (szError)
			{
				errorType = szError;
				errorType = str::encodeAscII(errorType);
			}
			else
			    errorType = "unknown error";
			json jError = {
					{"code", -32700},
					{"message" , "Parse error," + errorType}
			};
			string sError = jError.dump();
		}
	}
#endif
	else if (method == "testCall")
	{
	    int timeCost = 5;
		if (params["time"] != nullptr)
		{
			timeCost = params["time"].get<int>();
		}
		timeopt::sleepMilli(1000 * timeCost);
		rpcResp.result =  params.dump();
	}
	else
	{
		bHandled = false;
	}

	return bHandled;
}

bool rpcHandler::handleMethodCall_IoMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getDev")
	{
		rpc_getDev(params, rpcResp,session);
	}
	else if (method == "getDevStatis")
	{
		rpc_getDevStatis(params, rpcResp, session);
	}
	else if (method == "setIOTree")
	{
		//io tree 热更新
		ioSrv.stop(); //退出所有工作线程.包括采集线程，tcp客户端线程。stop不会锁住配置
		ioSrv.loadConfMerge(params);
		ioSrv.saveConf();
		ioSrv.run();
		result = "\"ok\"";
	}
	else if (method == "closeAllCycleAcq") {
		ioSrv.closeAllCycleAcq();
		result = RPC_OK;
	}
	else if (method == "openAllCycleAcq") {
		ioSrv.openAllCycleAcq();
		result = RPC_OK;
	}
	else if (method == "closeIOSession")
	{
		string remoteAddr = "";
		if(params.contains("remoteAddr"))
			remoteAddr = params["remoteAddr"].get<string>();
	
		ioSrv.m_mutexIoSessions.lock();
		for (auto  ioSession : ioSrv.m_IoSessions) {
			ioSession.second->disconnect();
		}
		ioSrv.m_mutexIoSessions.unlock();
	}
	else if (method == "getChanStatus")
	{
		rpc_getChanStatus(params,rpcResp);
	}
	else if (method == "getChanVal")
	{
		rpc_getChanVal(params, rpcResp);
	}
	else if (method == "reboot")
	{
		rpcResp.result = RPC_OK;
		timeopt::sleepMilli(200);
		exit(0);
	}
	else if (method == "rebootAllDev")
	{
		string req = R"s({
						"jsonrpc": "2.0",
						"method": "rebootDev",
						"params": {},
						"clientId": "tds",
						"ioAddr": "any",
						"id": 1
					}

				)s";

		ioSrv.m_tcpSrv_tdsp->SendData((char*)req.c_str(),req.length());
	}
	else if (method == "scanChannel" || method == "scanchannel")
	{
		result = rpc_io_scanChannel(params, error);
	}
	else if (method == "addDev")
	{
		ioSrv.rpc_addDev(params,rpcResp,session);
	}
	else if (method == "searchDev")
	{
		ioSrv.rpc_searchDev(params, rpcResp, session);
	}
	else if (method == "deleteDev")
	{
		ioSrv.rpc_deleteDev(params, rpcResp, session);
	}
	else if (method == "setDev")
	{
		ioSrv.rpc_modifyDev(params, rpcResp, session);
	}
	else if (method == "disposeDev")
	{
		ioSrv.rpc_disposeDev(params, rpcResp, session);
	}
	else if (method == "getDevConfBuff") //获取服务缓存的设备配置信息。目前仅用于tdsp设备
	{
		if (params.contains("ioAddr"))
		{
			string ioAddr = params["ioAddr"].get<string>();
			ioDev* pD = ioSrv.getIODev(ioAddr);
			if (pD)
			{
				if (pD->m_jConf != nullptr)
				{
					rpcResp.result = pD->m_jConf.dump();
				}
				else
				{
					json j = json::object();
					rpcResp.result = j.dump();
				}
			}
			else
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound,"ioDev with specified ioAddr not found");
			}
		}
		else
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_ioAddrNotSpecified, "ioAddr not specified in params");
		}
	}
	else if (method == "getDevInfoBuff") //获取服务缓存的设备配置信息。目前仅用于tdsp设备
	{
		if (params.contains("ioAddr"))
		{
			string ioAddr = params["ioAddr"].get<string>();
			ioDev* pD = ioSrv.getIODev(ioAddr);
			if (pD)
			{
				if (pD->m_jInfo != nullptr)
				{
					rpcResp.result = pD->m_jInfo.dump();
				}
				else
				{
					json j = json::object();
					rpcResp.result = j.dump();
				}
			}
			else
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "ioDev with specified ioAddr not found");
			}
		}
		else
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_ioAddrNotSpecified, "ioAddr not specified in params");
		}
	}
	else if (method == "getDevFirmware")
	{
		vector<string> list;
		//fs::getFileList(list,fs::appPath() +"/files/firmware",false);
		vector<fs::FILE_INFO> filist;
		fs::getFileList(filist, fs::appPath() + "/files/firmware", false);
		for (int i = 0; i < filist.size(); i++) {
			fs::FILE_INFO& fi = filist[i];
			list.push_back(fi.name);
		}
		json j = json::array();
		for (int i = 0; i < list.size(); i++) {
			j.push_back(list[i]);
		}
		rpcResp.result = j.dump();
	}
	else if(method == "getChanTemplateList")
	{
		json jList = json::array();
		for (auto& i : ioSrv.m_mapChanTempalte) {
			json tplInfo;
			tplInfo["name"] = i.second.name;
			tplInfo["label"] = i.second.label;
			jList.push_back(tplInfo);
		}
		rpcResp.result = jList.dump();
	}
	else if (method == "startDevUpgrade")
	{
		ioSrv.rpc_startDevUpgrade(params, rpcResp, session);
	}
	else if (method == "stopDevUpgrade")
	{
		ioSrv.rpc_stopDevUpgrade(params, rpcResp, session);
	}
	else if (method == "startDevUpgradeProcess")
	{
		ioSrv.rpc_startDevUpgradeProc(params, rpcResp, session);
	}
	else if (method == "stopDevUpgradeProcess")
	{
		ioSrv.rpc_stopDevUpgradeProc(params, rpcResp, session);
	}
	else if (method == "uploadDevFirmware")
	{
		ioSrv.rpc_uploadDevFirmware(params, rpcResp, session);
	}
	else if(method == "getChanTemplate"){
		ioSrv.rpc_getChanTemplate(params, rpcResp, session);
	}
	else if (method == "setChanTemplate") {
		ioSrv.rpc_setChanTemplate(params, rpcResp, session);
	}
	else if (method == "discoverDev")
	{
#ifdef ENABLE_GENICAM
		if (params["type"] == IO_DEV_TYPE::DEV::genicam)
		{
			json j = ioDev_genicam::listDevices();
			rpcResp.result = j.dump(2);
		}
#endif
	}
	else if (method == "hexSend" || method == "sendToDev") {
		LOG("[warn][sendToDev] %s", params.dump().c_str());
		string spkt = params["data"];
		if (!str::isValidHexString(spkt)) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "invalid hex string");
			return true;
		}

		if (!params["tag"].is_string()) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "tag params must be specifed and be string type");
			return true;
		}

		string tag = params["tag"];
		//查看是否属于子服务.
		ioDev* pChildTds = ioSrv.getOwnerChildTdsDev(tag);
		if (pChildTds) {
			string childTdsTag = pChildTds->m_strTagBind;
			string tagInChild = TAG::trimRoot(tag, childTdsTag);
			json childParams = params;
			childParams["tag"] = tagInChild;
			json childRlt, childErr;
			LOG("[warn][数据输出  ]请求子服务，tag=%s,子服务名称:%s", tag.c_str(), childTdsTag.c_str());
			pChildTds->call(method, childParams, nullptr, childRlt, childErr);
			if (childRlt != nullptr) {
				LOG("[warn][数据输出  ]请求子服务 成功，tag=%s,子服务名称:%s,返回:%s", tag.c_str(), childTdsTag.c_str(), childRlt.dump().c_str());
				rpcResp.result = childRlt.dump();
			}
			if (childErr != nullptr) {
				LOG("[warn][数据输出  ]请求子服务 失败，tag=%s,子服务名称:%s,返回:%s", tag.c_str(), childTdsTag.c_str(), childErr.dump().c_str());
				rpcResp.error = childErr.dump();
			}
		}
		else {
			ioDev* p = ioSrv.getIODevByTag(tag);
			if (p == nullptr) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "io device bind to specified tag not found");
				return true;
			}

			LOG("[warn][hexSend] 发送到设备:%s,数据:%s", p->getIOAddrStr().c_str(),spkt.c_str());
			vector<unsigned char> pkt = str::hexStrToBytes(spkt);
			p->sendData(pkt.data(), pkt.size());
			rpcResp.result = RPC_OK;
		}
	}
	else
	{
		bHandled = false;
	}

	return bHandled;
}

bool rpcHandler::handleMethodCall_audioPlayer(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	return false;
	/*string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "audioPlayer.play")
	{
		audioPlayer.rpc_play(params, rpcResp, session);
	}
	else if(method == "audioPlayer.pause")
	{
		audioPlayer.pause();
	}
	else if (method == "audioPlayer.stop")
	{
		audioPlayer.stop();
	}
	else if (method == "audioPlayer.unpause")
	{
		audioPlayer.unpause();
	}
	else if (method == "audioPlayer.getPlayList")
	{
		audioPlayer.rpc_getPlayList(params, rpcResp, session);
	}
	else
	{
		bHandled = false;
	}
	return bHandled;*/
}

bool rpcHandler::handleMethodCall_edgeDev(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getDevInfo")
	{
		json p;
		p["softVer"] = tds->getVersion();
		p["hardVer"] = "v1.0";
		p["deviceId"] = tds->conf->deviceID;
		p["deviceType"] = "TDS-Server";

		result = p.dump();
	}
	else if (method == "acq") {
		json j;
		OBJ_QUERIER query;
		query.getConf = false;
		query.getStatus = true;
		query.getChild = true;
		query.getMp = true;
		prj.toJson(j, query, nullptr,session.user);
		result = j.dump(4);
		//LOG("88888: " + result);
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}

bool rpcHandler::handleMethodCall_gamePad(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method.find("gamepad")!= string::npos)
	{
		json p;
		p["user"] = session.user;
		rpcSrv.notify(method, p);
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}

bool rpcHandler::handleMethodCall_MoMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	//配置的使用与配置的修改之间不允许并发。使用读写锁保护
	if (method == "setConfFile") {
		rpc_setconffile(params, rpcResp,session);
	}
	else if (method == "getConfFile") {
		rpc_getconffile(params, rpcResp, session);
	}
	else if (method == "getObjTemplate") {
		if (params.contains("name")) {
			string type = params["name"];
			if (prj.m_mapObjTempalte.find(type) != prj.m_mapObjTempalte.end()) {
				OBJ_TEMPLATE* ct = prj.m_mapObjTempalte[type];
				rpcResp.result = ct->tplData;
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::OBJ_templateNotFound, "object template not found");
			}
		}
		else {
			json j = json::array();
			for (auto& iter : prj.m_mapObjTempalte) {
				json jItem = json::object();
				jItem["name"] = iter.first;
				jItem["data"] = iter.second->tplData;
				j.push_back(jItem);
			}
			rpcResp.result = j.dump();
		}
	}
	else if (method == "setObjTemplate") {
		prj.setObjTemplate(params);
		rpcResp.result = "\"ok\"";
	}
	else if (method == "setObj")
	{
		if (params.contains("children")) { //如果包含children字段，说明要修改树结构。该模式重载对象树。冷重载
			if (!params.contains("tag")) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
			}
			else {
				unique_lock<shared_mutex> lock(prj.m_csPrj);
				LOCK_THREAD_RECORDER recorder(&prj.m_prjWriteLockThread, sys::getThreadId());

				//加载新的树
				project tmpPrj;
				tmpPrj.loadConf(params);
				tmpPrj.loadTreeStatus(&prj);//保留原有的实时数据状态
				prj.clear();
				prj.m_name = tmpPrj.m_name;
				prj.m_mapAllMP = tmpPrj.m_mapAllMP;
				prj.m_childObj = tmpPrj.m_childObj;
				prj.m_type = tmpPrj.m_type;
				for (int i = 0; i < prj.m_childObj.size(); i++) {
					OBJ* p = prj.m_childObj[i];
					p->m_pParentMO = &prj;
				}
				tmpPrj.m_childObj.clear();
				bool bSaved = prj.saveConfFile();
				if (!bSaved) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "save mo.json file fail; maybe file is set to readonly");
					LOG("[error]保存mo.json失败;检查该文件是否被设置成了只读属性");
					return true;
				}
				std::map<string, SCRIPT_INFO> expScripts;
				prj.getAllVarExpScript();

				//数据服务自己缓存状态，并重新加载，此处不应从ioSrv同步数据，后续应当删除。
				//ioSrv.updateTag2IOAddrBinding();
				//ioSrv.updateAllChanVal();

				rpcSrv.notify("objTreeUpdated", nullptr);
				result = "\"ok\"";
			}
		}
		else {//只用于不改变mo的类型和id信息的非关键信息配置，不改变children,目前暂用于gps地址。热重载
			if (params.is_object()) //单个设置
			{
				if (!params.contains("tag")) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
				}
				else {
					json mo = params;
					string tag = mo["tag"].get<string>();
					string rootTag = "";
					if (mo.contains("rootTag"))
						rootTag = mo["rootTag"].get<string>();
					tag = TAG::addRoot(tag, rootTag);
					tag = TAG::addRoot(tag, session.org);
					OBJ* pmo = prj.queryObj(tag,session.language);
					if (pmo)
					{
						pmo->loadConf(mo);
						prj.saveConfFile();
						result = "\"ok\"";
					}
					else {
						rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, tag + " specified tag not found");
					}
				}
			}
			else if (params.is_array())
			{
				bool ok = true;
				for (int i = 0; i < params.size(); i++) {
					json& mo = params[i];
					string tag = mo["tag"].get<string>();
					string rootTag = "";
					if (mo.contains("rootTag"))
						rootTag = mo["rootTag"].get<string>();
					tag = TAG::addRoot(tag, rootTag);
					tag = TAG::addRoot(tag, session.org); 
					OBJ* pmo = prj.queryObj(tag, session.language);
					if (pmo)
					{
						pmo->loadConf(mo);
					}
					else {
						ok = false;
						rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, tag + " specified tag not found");
						break;
					}
				}

				if (ok) {
					prj.saveConfFile();
					result = "\"ok\"";
				}
			}
		}
	}
	else if (method == "setObjAttr") {
		prj.loadConf(params, false);
		prj.saveConfFile();
		rpcResp.result = RPC_OK;
	}
	else if (method == "updateTagBinding") {
		for (auto& binding : params) {
			string tag = binding["tag"];
			OBJ* p = prj.queryObj(tag, session.language);
			if (p) {
				p->m_strIoAddrBind = binding["ioAddr"];
			}
		}
	}
	else if (method == "objOnline") {
		rpc_onObjOnline(params,session);
	}
	else if (method == "objOffline") {
		rpc_onObjOffline(params, session);
	}
	else
	{
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		//以下配置使用 mo conf 和 io conf
		if (method == "input")
		{
			if (params.contains("name")) { //tdsp接收到子服务的数据后，会进入到此处
				string rootTag;
				if (params.contains("rootTag")) { 
					rootTag = params["rootTag"];
				}
				json jDeList = json::array();
				json jOnlineStatusList = json::array();

				//params为子服务上送的对象树，rootTag为子服务在上级服务中的位号
				//数据值和在线状态都来自于io设备采集，因此统一用input接口输入
				OBJ::treeStatus2ListStatus(params, jDeList, jOnlineStatusList, rootTag);

				for (int i = 0; i < jOnlineStatusList.size();i++) {
					json& os = jOnlineStatusList[i];
					if (os["online"].is_boolean()) {
						if (os["online"].get<bool>() == true) {
							rpc_onObjOnline(os, session);
						}
						else if (os["online"].get<bool>() == false) {
							rpc_onObjOffline(os, session);
						}
					}
				}

				rpc_input(jDeList, rpcResp, session);
			}
			else {
				rpc_input(params, rpcResp, session);
			}
		}
		else if (method == "output")
		{
			rpc_output(params, rpcResp,session);
		}
		else if (method == "getMpStatus")
		{
			result = rpc_getMpStatus(params, error, session);
		}
		else if (method == "getMpVal")
		{
			result = rpc_getMpStatus(params, error, session,true);
		}
		else if (method == "getMoOnlineStatus") //智能设备在线状态
		{
			result = rpc_getMoOnlineStatus(params, error);
		}
		else if (method == "getMoStatis")
		{
			rpc_getMoStatis(params, rpcResp, session);
		}
		else if (method == "getObjStatis")
		{
			rpc_getObjStatis(params, rpcResp, session);
		}
		else if (method == "getMpStatis")
		{
			rpc_getMpStatis(params, rpcResp, session);
		}
		else if (method == "getMoAttri" || method == "getMoAttr")
		{
			rpc_getMoAttr_list(params, rpcResp, session);
		}
		else if (method == "getconf")
		{
			result = rpc_getconf(params, error);
		}
		else if (method == "getMpTypeList")
		{
			json list;
			prj.getMpTypeList(list);
			result = list.dump();
		}
		else if (method == "getObjTree") {
			result = prj.m_moConfFileDump;
		}
		else if (method == "getMo" || method == "getOrg" || method == "getObj" || method == "getMp" || method == "getCustomOrg" || method == "getCustomMo")
		{
			//位号选择器 参数tag + rootTag
			//用户查询时 tag默认"",rootTag默认""
			//tag是相对于rootTag的相对位号
			//rootTag和tag组合出用户位号。
			//用户位号和用户组织结构组合成系统位号
			string rootTag = "";//查询根
			if (params != nullptr && params["rootTag"] != nullptr && params["rootTag"].get<string>() != "") //获取子树
			{
				rootTag = params["rootTag"].get<string>();
			}
			rootTag = TAG::addRoot(rootTag, session.org);//组合为系统查询根

			//类型选择
			string type = ""; //为空表示选中所有，为*表示选中所有自定义类型
			if (params["type"] != nullptr) {
				type = params["type"].get<string>();
			}
	
			//层级选择
			string level = "*";
			if (params["level"] != nullptr)
				level = params["level"].get<string>();
			//将getOrg,getMp,getMo统一转化为getObj
			else if (method == "getOrg") {
				level = "org";
			}
			else if (method == "getCustomOrg") {
				level = "org";
			}
			else if (method == "getMo") {
				level = "mo";
			}
			else if (method == "getCustomMo") {
				level = "mo";
			}
			else if (method == "getMp") {
				params["getMp"] = true;
				level = "mp";
			}

			//位号选择
			vector<string> vecTagSel = parseTagSel(params["tag"],type);
			TAG_SELECTOR tagSel;
			tagSel.language = session.language;
			tagSel.init(vecTagSel, rootTag, type,level);

			string mode = "array";
			if (params["mode"] != nullptr) {
				mode = params["mode"].get<string>();
			}
			
			vector<OBJ*> objList;
			prj.getObjByTagSelector(objList, tagSel);

			//多选模式
			if (!tagSel.singleSelMode()) {
				params["rootTag"] = rootTag;

				//方便api使用，通配模式下，默认获取子节点和mp
				//非通配模式下，默认关闭
				//if (!params.contains("getMp")) {
				//	params["getMp"] = true;
				//}
				//if (!params.contains("getChild")) {
				//	params["getChild"] = true;
				//}

				OBJ_QUERIER q = OBJ::parseQuerier(params);
				q.language = session.language;

				if (mode == "array") {
					json jRlt = json::array();
					for (int i = 0; i < objList.size(); i++) {
						OBJ* pObj = objList[i];

						//string sTag = pObj->getTag();

						//if (session.user != "")
						//{
						//	if (!userMng.checkTagPermission(session.user, sTag))
						//		continue;
						//}

						json jObj;
						bool selectedByLeafType = false;
						if (pObj->toJson(jObj, q,&selectedByLeafType, session.user))
							jRlt.push_back(jObj);
					}
					result = jRlt.dump(2);
				}
				else
				{
					json jRlt = json::object();
					for (int i = 0; i < objList.size(); i++) {
						OBJ* pObj = objList[i];
						json jObj;
						bool selectedByLeafType = false;
						if (pObj->toJson(jObj, q,&selectedByLeafType, session.user)) {

							string tag = jObj["tag"].get<string>();
							tag = str::replace(tag, ".", "_");
							jRlt[tag] = jObj;
						}

					}
					result = jRlt.dump(2);
				}
			}
			//精确查找模式，返回一个对象
			else if(objList.size() == 1){
				OBJ* pmo = objList[0];
				//所有位号以用户位号的方式展示。除非另外指定rootTag

				json j;
				params["rootTag"] = rootTag;

				OBJ_QUERIER q = OBJ::parseQuerier(params);
				q.pRoot = pmo;
				q.language = session.language;
				bool selectedByLeafType = false;
				if (pmo->toJson(j, q,&selectedByLeafType, session.user))
					result = j.dump(4);
			}
			else
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "monitor object of specified tag not found");
			}
		}
		else if ( method == "getCustomTypes")
		{
			OBJ* pmo = nullptr;
			if (params != nullptr && params.contains("tag"))
			{
				string tag = params["tag"].get<string>();
				if (tag == "")
				{
					pmo = &prj;
				}
				else
					pmo = prj.queryObj(tag, session.language);
			}
			else
			{
				pmo = &prj;
			}

			if (pmo != nullptr)
			{
				string level = "*";
				if (params["level"].is_string()) {
					level = params["level"];
				}
				map<string, json> list = pmo->getChildCustomTypeList(level);
				json jList = json::array();

				for (auto& i : list)
				{
					jList.push_back(i.second);
				}

				result = jList.dump();
			}
			else
			{
				error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "未找到指定位号");
			}
		}
		else if (method == "sum") {
			if (params["tag"] == nullptr) //获取子树
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
				return true;
			}
			string tag = params["tag"];
			bool invalidAsZero = false;
			if(params.contains("invalidAsZero"))
				invalidAsZero = params["invalidAsZero"].get<bool>();
			vector<MP*> mpList;
			TAG_SELECTOR tagSel;
			tagSel.init(tag);
			prj.getMpByTagSelector(mpList, tagSel);

			json rlt = nullptr;
			double dbSum = 0;
			bool success = true;

			//对时间段内的求和
			if (params.contains("time")) {
				
				params["aggr"] = "sum";
				string err, rlt, queryInfo;
				string sParams = params.dump();
				db.rpc_db_select(sParams, err, rlt, queryInfo);

				if (rlt != "") {
					json j = json::parse(rlt);
					dbSum = j["val"].get<double>();
				}
			}
			//对所有位号的当前值聚合,相当于 time=last
			else {
				for (int i = 0; i < mpList.size(); i++) {
					MP* pmp = mpList[i];
					if (pmp->m_curVal.is_number()) {
						double val = pmp->m_curVal.get<double>();
						dbSum += val;
					}
					else {
						if (!invalidAsZero) {
							success = false;
							break;
						}
					}
				}
			}

			if (success) {
				rlt = dbSum;
			}
			rpcResp.result = rlt.dump();
		}
		else if (method == "avg") {
			if (params["tag"] == nullptr) //获取子树
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
				return true;
			}
			string tag = params["tag"];
			bool invalidAsZero = false;
			if (params.contains("invalidAsZero"))
				invalidAsZero = params["invalidAsZero"].get<bool>();
			vector<MP*> mpList;
			TAG_SELECTOR tagSel;
			tagSel.init(tag);
			prj.getMpByTagSelector(mpList, tagSel);

			json rlt = nullptr;
			double dbAvg = 0;
			double dbSum = 0;
			int calcCount = 0;
			bool success = true;
			for (int i = 0; i < mpList.size(); i++) {
				MP* pmp = mpList[i];
				if (pmp->m_curVal.is_number()) {
					double val = pmp->m_curVal.get<double>();
					dbSum += val;
					calcCount++;
				}
				else {

				}
			}
			if (success) {
				dbAvg = dbSum / calcCount;
				rlt = dbAvg;
			}
			rpcResp.result = rlt.dump();
		}
		else
		{
			bHandled = false;
		}
	}

	return bHandled;
}

vector<string> rpcHandler::parseTagSel(json& tagSel,string& type) {
	vector<string> vec;
	if (tagSel.is_null()) { //位号未指定
		if (type == "")  //type未指定
		{
			vec.push_back(""); //选中根位号
		}
		else { //指定了某种自定义对象，认为是一种批量查找
			vec.push_back("*");
		}
	}
	else if (tagSel.is_string()) {
		vec.push_back(tagSel.get<string>());
	}
	else if (tagSel.is_array()) {
		for (auto& t : tagSel) {
			if (t.is_string()) {
				vec.push_back(t.get<string>());
			}
		}
	}

	return vec;
}

bool rpcHandler::handleMethodCall_alarmMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	almServer* pAlmSrv = nullptr;

	if (session.dbpath == "alarmsDevelop")
	{
		pAlmSrv = &almSrv_dev;
	}
	else if(session.dbpath == "alarms")
	{
		pAlmSrv = &almSrv;
	}
	else if (session.dbpath == "faultsDevelop")
	{
		pAlmSrv = &almSrv_fauDev;
	}
	else if (session.dbpath == "faults")
	{
		pAlmSrv = &almSrv_fau;
	}
	else
	{
		pAlmSrv = &almSrv;
	}
	string& result = rpcResp.result;
	bool bHandled = true;
	//** 数据查询系列
	if (method == "getAlarmCurrent")
	{
		json jFilter= params;
		//jFilter["rootTag"] = params["rootTag"];
		result = pAlmSrv->rpc_getCurrent(jFilter, session);
	}
	else if (method == "getAlarmUnRecover")
	{
		json jFilter= params;
		result = pAlmSrv->rpc_getUnRecover(jFilter, session);
	}
	else if (method == "getAlarmUnack")
	{
		json jFilter= params;
		result = pAlmSrv->rpc_getUnack(jFilter, session);
	}
	//getAlm为上面3个接口的合并接口
	else if (method == "getAlm")
	{
		if (params.contains("status")) {
			string status = params["status"].get<string>();
			json jFilter;
			jFilter["rootTag"] = params["rootTag"];
			if (status == "unRecover") {
				result = pAlmSrv->rpc_getUnRecover(jFilter, session);
			}
			else if (status == "unAck") {
				result = pAlmSrv->rpc_getUnack(jFilter, session);
			}
			else if (status == "unRecover||unAck" || status == "unAck||unRecover") {
				result = pAlmSrv->rpc_getCurrent(jFilter, session);
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "param  status format error");
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "missing param  status");
		}
	}

	else if (method == "getAlarmHistory")
	{
		result = pAlmSrv->rpc_getHistory(params, session);
	}
	//** 数据生成系列 以下接口都会修改报警数据
	else if (method == "addAlarm")
	{
		if (session.dbpath == "alarms2")
		{
			result = pAlmSrv->rpc_addAlarm(params, rpcResp);
		}
		else
		{
			result = pAlmSrv->rpc_addAlarm(params, rpcResp);
		}
	}
	else if (method == "recoverAlarm" || method == "clearAlarm") {
		pAlmSrv->rpc_recoverAlarm(params, rpcResp);
	}
	else if (method == "updateAlarmStatus") //该接入送入一个最新计算出的报警状态，报警服务内部计算 是需要add还是 recover
	{
		pAlmSrv->rpc_updateStatus(params, rpcResp);
	}
	else if (method == "ackAlarm" || method == "ackAlarmEvent")
	{
		pAlmSrv->rpc_acknowledge(params,rpcResp, session);
	}
	else if (method == "ackAllAlarm" || method == "ackAllAlarmEvent")
	{
		pAlmSrv->rpc_acknowledge(params, rpcResp, session);
	}
	else if (method == "approveAlarm" ) //审核报警  审核通过则更新到正式报警
	{
		if (pAlmSrv == &almSrv_dev) {
			string dd = params.dump();

			int nRet = pAlmSrv->rpc_approve(params, rpcResp, session);
			if(1 == nRet )
				almSrv.rpc_addAlarm(params, rpcResp);
		}
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}

bool rpcHandler::handleMethodCall_userMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	json jRlt;
	json jErr;

	if (method == "addUser")
	{
		userMng.rpc_addUser(params, rpcResp, session);
	}
	else if (method == "getUsers")
	{
		json j = userMng.rpc_getUsers(params,rpcResp,session);
		result = j.dump(4);
	}
	else if (method == "deleteUser")
	{
		userMng.rpc_deleteUser(params, rpcResp, session);
	}
	else if (method == "changePwd")
	{
		params["user"] = session.user;
		userMng.rpc_changePwd(params, jRlt, jErr);
	}
	else if (method == "getRoles")
	{
		json j = userMng.getRoles(session.user);
		result = j.dump(4);
	}
	else if (method == "setUsers")
	{
		userMng.rpc_setUsers(params,rpcResp,session);
	}
	else if (method == "getUiTree")
	{
		result = userMng.m_jUI.dump(4);
	}
	else if (method == "updateToken" || method == "refreshToken")
	{
		userMng.rpc_updateToken(params,rpcResp,session);
	}
	else
	{
		bHandled = false;
	}


	if (jRlt != nullptr)
		result = jRlt.dump();
	else if (jErr != nullptr)
		error = jErr.dump();
	return bHandled;
}

bool rpcHandler::handleMethodCall_unclassified(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	bool bHandled = true;
	//可完全并发的命令
	if (method == "xiaot")
	{
		rpcResp.result = tds->xiaoT->getReply(params);
	}
	else if (method == "addLog")
	{
		if (params["host"] != nullptr)
		{
			params["host"] = session.remoteAddr + ";" + params["host"].get<string>();
		}
		logSrv.rpc_addLog(params, session);
		rpcResp.result = "\"ok\"";
	}
	//该函数用于分析性能问题，性能调用可能是由于某个rpc接口调用过于频繁，禁用该接口观察性能状态
	else if (method == "disableMethod") {
		string method = params["method"];
		m_mapDisableMethod[method] = method;
		rpcResp.result = "\"ok\"";
	}
	else if (method == "enableMethod") {
		string method = params["method"];
		m_mapDisableMethod.erase(method);
		if (method == "*")
			m_mapDisableMethod.clear();
		rpcResp.result = "\"ok\"";
	}
	else if (method == "enableIOHandle") {
		ioSrv.m_bDisableIOHandle = false;
		rpcResp.result = "\"ok\"";
	}
	else if (method == "disableIOHandle") {
		ioSrv.m_bDisableIOHandle = true;
		rpcResp.result = "\"ok\"";
	}
	else if (method == "enableWsNotify") {
		g_enableWsNotify = true;
		rpcResp.result = "\"ok\"";
	}
	else if (method == "disableWsNotify") {
		g_enableWsNotify = false;
		rpcResp.result = "\"ok\"";
	}
	else if (method == "queryLog" || method == "getLog")
	{
		rpcResp.result = logSrv.rpc_queryLog(params, session);
	}
	else if (method == "rpcCallStatis") {
		m_csCallStatis.lock();
		json j = json::object();
		for (auto& i : m_mapCallStatis) {
			j[i.first] = i.second;
		}
		rpcResp.result = j.dump();
		m_csCallStatis.unlock();
	}
#ifdef ENABLE_JERRY_SCRIPT
	else if (method == "runScript")
	{
		scriptManager.rpc_runScript(params, rpcResp, session);
	}
	else if (method == "getScriptList")
	{
		scriptManager.rpc_getScriptList(params, rpcResp, session);
	}
	else if (method == "getScriptFile")
	{
		scriptManager.rpc_getScript(params, rpcResp, session);
	}
	else if (method == "deleteScriptFile") {
		scriptManager.rpc_deleteScript(params, rpcResp, session);
	}
	else if (method == "setScriptFile")
	{
		scriptManager.rpc_setScript(params, rpcResp, session);
	}
	else if (method == "getReportConf") {
		string sConf;
		json jConf;
		fs::readFile(tds->conf->confPath + "/report.json", sConf);
		if (sConf != "") {
			jConf = json::parse(sConf);
		}
		else {
			jConf = json::array();
		}

		rpcResp.result = jConf.dump();
	}
	else if (method == "setReportConf") {
		string sConf = params.dump(2);
		fs::writeFile(tds->conf->confPath + "/report.json", sConf);
		rpcResp.result = "\"ok\"";
	}
#endif
	else if (method == "getLicenceStatus") {
		m_csLicenceStatus.lock();
		if (m_licenceStatus == nullptr) {
			m_licenceStatus["valid"] = true;
		}

		rpcResp.result = m_licenceStatus.dump();
		m_csLicenceStatus.unlock();
		return true;
	}
	else if (method == "getStreamUrl") {
		string tag = params["tag"];
		MP* pmp = prj.GetMPByTag(tag, session.language);
		if (pmp) {
			json rlt = rpc_getStreamUrl(pmp, tag, session.isHttps, session.localIP, session.localPort);
			rpcResp.result = rlt.dump();
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "tag not found");
		}
	}
	else if (method == "getTopoList")
	{
		rpcResp.result = rpc_getTopoList(params, rpcResp.error, session);
	}
	else if (method == "startStreamPush" || method == "startPushStream") {
		reverseInterface.rpc_startStreamPush(params, rpcResp, session);
	}
	else if (method == "getVerifyCode") {
		string phoneNum = params["phoneNum"];
		tds->smsServer->sendVerificationCode(phoneNum);
		rpcResp.result = RPC_OK;
	}
	else if (method == "checkVerifyCode") {
		string phoneNum = params["phoneNum"];
		string code = params["verifyCode"];
		tds->smsServer->checkVerificationCode(phoneNum,code);
		rpcResp.result = RPC_OK;
	}
	else if (method == "sendShortMsg") {
		string phoneNum = params["phoneNum"];
		tds->smsServer->send(params, phoneNum);
		rpcResp.result = RPC_OK;
	}
	else if (method == "setSrvConf") {
		string dbPath = params["dbPath"];
		//ini配置修改
		tds->conf->setStr("dbPath", dbPath);
		rpcResp.result = RPC_OK;//返回ok
		
	}
#ifdef _WIN32
	else if (method.find("memDiag") != string::npos) {
		string para = params.dump();
		return memDiag.handleRpcCall_memDiag(method, para, rpcResp.result, rpcResp.error);
	}
#endif
	else if (method == "getProjectConf") {

		if (params.is_string()) {
			string confName = params.get<string>();
			if (confName == "*" || confName == "") {
				json jsconf;
				map<string, string>& maps = tdsConf.tdsIni.mapConf;
				for (auto itm : maps)
				{
					jsconf[itm.first] = itm.second;
				}
				rpcResp.result = jsconf.dump();
			}
			else {
				if (tdsConf.tdsIni.mapConf.find(confName) != tdsConf.tdsIni.mapConf.end()){
					string sVal = tdsConf.tdsIni.mapConf[confName];
					json jRlt;
					if (sVal == "") {
						jRlt[confName] = sVal;
					}
					else if (str::isInteger(sVal)) {
						jRlt[confName] = atoi(sVal.c_str());
					}
					else {
						jRlt[confName] = sVal;
					}
					rpcResp.result = jRlt.dump();
				}
				else {
					rpcResp.result = RPC_NULL;
				}
			}
		}
		else {
			json jsconf;
			map<string, string>& maps = tdsConf.tdsIni.mapConf;
			for (auto itm : maps)
			{
				jsconf[itm.first] = itm.second;
			}
			rpcResp.result = jsconf.dump();
		}
	}
	else if (method == "setProjectConf") {	 
		for (auto itm: params.items())
		{
			if (itm.value().is_string())
			{
				tds->conf->setStr(itm.key(), itm.value());
			}
			else
			{
				tds->conf->setInt(itm.key(), itm.value());
			}
		}
		rpcResp.result = RPC_OK;
	}
	else if (method == "GetDamageDataSummary") {
		string tag = params["tag"]; //车站的tag
		string time = params["time"];
		// send  http cmd to the station,waitting for the station's part of sunmmary 
		//根据关联的tag找到ioDev
		ioDev* pIoDev = ioSrv.getIODevByTag(tag);
		string theIp = "";
		if (pIoDev) {
			string debug = pIoDev->m_jDevAddr.dump();
			if (pIoDev->m_jDevAddr.is_object()) {
				theIp = pIoDev->m_jDevAddr["ip"];
			}
		}
		string res0;
		std::function<bool(string&, json*, string, string, json&)> funcMakeData = [](string& strJsonArr, json* in_pArr, string tagPrefix, string tagSuffix, json& out_list)-> bool
		{
			map<string, string> tableFieldsDescAndKey{
				{"道岔","dc"},
				{"直尖轨段1","zhijian1"},{"直尖轨段2","zhijian2"},{"导曲轨段1","daoqu1"},{"曲尖轨段1","qujian1"},{"曲尖轨段2","qujian2"},{"导曲轨段3","daoqu3"},
				{"导曲轨段2","daoqu2"},{"导曲轨段4","daoqu4"},{"翼轨段1","yi1"},{"翼轨段2","yi2"},{"心轨段1","xin1"},{"心轨段2","xin2"},{"心轨段3","xin3"}
			};
			json* pJsIn = NULL; json sIn;
			if (in_pArr) {
				pJsIn = in_pArr;
			}
			else if ("" != strJsonArr) {
				sIn = json::parse(strJsonArr);
				pJsIn = &sIn;
			}

			json& jsIn = *pJsIn;
			map<string, map<string, string>> tmpMap; // 1#: 段1->100,段2->100,..., 2#: 段1->100,段2->100,...,
			vector<string> tmpVec;
			for (int i = 0; i < jsIn.size(); i++) {
				json& jOne = jsIn[i];
				string ta0 = jOne["tag"];  ta0 = tagPrefix + ta0 + tagSuffix;
				int cnt = jOne["count"].get<int>();

				auto pMp = prj.GetMPByTag(ta0, "zh");
				string rail = pMp->m_pParentMO->m_name;
				string daocha = pMp->m_pParentMO->m_pParentMO->m_name;

				rail = charCodec::utf16_to_gb(charCodec::utf8_to_utf16(rail));

				if (tmpMap.find(daocha) != tmpMap.end()) {
					map<string, string>& theDcData = tmpMap[daocha];
					theDcData[rail] = to_string(cnt);
				}
				else {
					map<string, string> theDcData;
					theDcData[rail] = to_string(cnt);
					tmpMap[daocha] = theDcData;

					tmpVec.push_back(daocha);
				}
			}
			json& list = out_list;
			for (int i = 0; i < tmpVec.size(); i++) {
				string& dc = tmpVec[i];
				json jRow; jRow["dc"] = dc;

				map<string, string>& theDcData = tmpMap[dc];
				auto it = theDcData.begin();
				for (; it != theDcData.end(); it++) {
					string desc = charCodec::gb_to_utf8(it->first);
					jRow[tableFieldsDescAndKey[desc]] = it->second;
				}
				list.push_back(jRow);
			}
			return true;
		};
		
		//各种异常情况：
		//没配置ip或类似问题则 界面send这部分显示空白
		//没有指数数据 对应的区段显示0 
		//1.
		json listSend = json::array();
		if (theIp != "") {  
			json jsSend, jsParam;
			jsSend["jsonrpc"] = "2.0";   jsSend["method"] = "GetDamageDataSendSummary"; jsSend["id"] = "1";
			jsParam["time"] = time;
			jsSend["params"] = jsParam;
			string strSend = jsSend.dump();

			httplib::Client httpClt(theIp.c_str(), 81);
			httpClt.set_connection_timeout(5);
			string path = "/api/rpc";
			httplib::Result ret = httpClt.Post(path.c_str(), strSend, "application/json");
			if (ret) {
				int jj = 0;
			}
			if (ret && ret->status == 200) {
				res0 = ret->body;
				funcMakeData(res0, NULL, tag + ".", ".伤损指数", listSend);
			}
			else {
				//LOG
			}
		}
		//2.
		json listRecv = json::array();
		vector<string> vecTagSel; vecTagSel.push_back(tag + ".*"); //验证tag必须是站点
		TAG_SELECTOR tagSel;
		tagSel.init(vecTagSel, "", "道岔", "mo");
		vector<OBJ*> objList;
		prj.getObjByTagSelector(objList, tagSel);//该站的全部道岔

		json jParam;
		jParam["tag"] = tag + ".*.伤损指数";//该车站全部道岔全部铁轨的指数
		jParam["time"] = time;
		jParam["aggr"] = "count";
		string strPara = jParam.dump();
		string strRet, strErr, strQry;
		db.rpc_db_select(strPara, strRet, strErr, strQry);
		json jRet = json::parse(strRet);
		funcMakeData(strRet, NULL, "", "", listRecv);

		//3. 发送-接收
		json listLost = json::array();
		//先把recv的每个数组元素 加上 道岔的索引.
		map<string, json*> mapRecv;
		for (int i = 0; i < listRecv.size(); i++) {
			string dc = listRecv[i]["dc"];
			mapRecv[dc] = &listRecv[i];
		}
		for (int i = 0; i < listSend.size(); i++) {
				json& sendOne = listSend[i];
				string dc = sendOne["dc"];
				if (mapRecv.find(dc) == mapRecv.end()) {
					continue;
				}
				json& recvOne = *(mapRecv[dc]);

				json one;
				for (json::iterator it = sendOne.begin(); it != sendOne.end(); ++it) {
					//string debug = sendOne.dump();
					string k = it.key();
					if (k == "dc") {
						one["dc"] = it.value();
					}
					else {
						string sendCnt = it.value();
						string recvCnt = recvOne[k];
						int diff = atoi(sendCnt.c_str()) - atoi(recvCnt.c_str());
						one[it.key()] = diff;
					}
				}
				listLost.push_back(one);
		}

		json jRecv;
		jRecv["recv"] = listRecv;
		jRecv["send"] = listSend;
		jRecv["lost"] = listLost;
		res0 = jRecv.dump();

		rpcResp.result = res0;
	}
	else if (method == "updateDeList")
	{
		string tag = params["tag"];
		string time = params["time"];
		DB_TIME dbTime;
		if (dbTime.fromStr(time))
		{
			string curveIdxPath = db.getPath_dbFile(tag, dbTime, "curveIdx");
			string curveIdxPath_gbk = charCodec::utf8_to_gb(curveIdxPath);
			yyjson_read_err err_r;
			yyjson_doc* doc = yyjson_read_file(curveIdxPath_gbk.c_str(), 0, 0, &err_r);
			yyjson_mut_doc* mut_doc = yyjson_doc_mut_copy(doc, nullptr);
			yyjson_mut_val* mut_root = yyjson_mut_doc_get_root(mut_doc);

			if (yyjson_mut_is_arr(mut_root))
			{
				size_t idx = 0;
				size_t max = 0;
				yyjson_mut_val* item;
				yyjson_mut_arr_foreach(mut_root, idx, max, item) {

					string oneCurveTime = yyjson_mut_get_str(yyjson_mut_obj_get(item, "time"));
					DB_TIME curveTime;
					if (curveTime.fromStr(oneCurveTime))
					{
						string oneCurvePath = db.getPath_dbFile(tag, curveTime, "curve");
						string oneCurvePath_gbk= charCodec::utf8_to_gb(oneCurvePath);
						yyjson_doc* oneCurveDoc = yyjson_read_file(oneCurvePath_gbk.c_str(), 0, 0, &err_r);
						yyjson_val* oneCurveRoot = yyjson_doc_get_root(oneCurveDoc);
						if (yyjson_is_obj(oneCurveRoot))
						{
							if (yyjson_obj_get(oneCurveRoot, "endTime") && !yyjson_mut_obj_get(item,"endTime"))
								yyjson_mut_obj_add_val(mut_doc, item, "endTime", yyjson_val_mut_copy(mut_doc, yyjson_obj_get(oneCurveRoot, "endTime")));
							if (yyjson_obj_get(oneCurveRoot, "startTime") && !yyjson_mut_obj_get(item, "startTime"))
								yyjson_mut_obj_add_val(mut_doc, item, "startTime", yyjson_val_mut_copy(mut_doc, yyjson_obj_get(oneCurveRoot, "startTime")));
							if (!yyjson_mut_obj_get(item, "dcPos"))
								yyjson_mut_obj_add_strcpy(mut_doc, item, "dcPos", "fix");
							if (!yyjson_mut_obj_get(item, "absMax") || !yyjson_mut_obj_get(item, "PRX") || !yyjson_mut_obj_get(item, "PRY") || !yyjson_mut_obj_get(item, "PRZ"))
							{
								auto pt_arr = yyjson_obj_get(oneCurveRoot, "pt");
								if (pt_arr&&yyjson_is_arr(pt_arr))
								{
									size_t pt_idx = 0;
									size_t pt_max = 0;
									yyjson_val* pt_item;
									const char* params_string[12] = { "AX","AY","AZ","VX","VY","VZ","DX","DY","DZ","HZX","HZY","HZZ" };
									float absMaxValue[12];
									float squareSumDx = 0;
									float squareSumDy = 0;
									float squareSumDz = 0;
									float squareSumDxDyDz = 0;
									yyjson_arr_foreach(pt_arr, pt_idx, pt_max, pt_item)
									{
										for (int i=0;i<12;i++)
										{
											float itemValue;
											if (yyjson_is_real(yyjson_arr_get(pt_item, i)))
											{
												itemValue = yyjson_get_real(yyjson_arr_get(pt_item, i));
											}
											else
											{
												itemValue = yyjson_get_int(yyjson_arr_get(pt_item, i)) * 1.0;
											}
											if (pt_idx == 0)
											{
												absMaxValue[i]= abs(itemValue);
											}
											else
											{
												absMaxValue[i] = abs(itemValue) > absMaxValue[i] ? abs(itemValue) : absMaxValue[i];
											}

											if (i==6)
											{
												squareSumDx += (itemValue * itemValue);
												squareSumDxDyDz += (itemValue * itemValue);
											}
											if (i == 7)
											{
												squareSumDy += (itemValue * itemValue);
												squareSumDxDyDz += (itemValue * itemValue);
											}
											if (i == 8)
											{
												squareSumDz += (itemValue * itemValue);
												squareSumDxDyDz += (itemValue * itemValue);
											}
										}
									}
									
									if (!yyjson_mut_obj_get(item, "absMax"))
									{
										auto absMax_obj = yyjson_mut_obj(mut_doc);
										for (int i = 0; i < 12; i++) {
											yyjson_mut_obj_add_real(mut_doc, absMax_obj, params_string[i], absMaxValue[i]);
										}
										yyjson_mut_obj_add_val(mut_doc, item, "absMax", absMax_obj);
									}

									if (!yyjson_mut_obj_get(item, "PRX") && squareSumDxDyDz != 0)
										yyjson_mut_obj_add_real(mut_doc, item, "PRX", squareSumDx / squareSumDxDyDz);
									if (!yyjson_mut_obj_get(item, "PRY") && squareSumDxDyDz != 0)
										yyjson_mut_obj_add_real(mut_doc, item, "PRY", squareSumDy / squareSumDxDyDz);
									if (!yyjson_mut_obj_get(item, "PRZ") && squareSumDxDyDz != 0)
										yyjson_mut_obj_add_real(mut_doc, item, "PRZ", squareSumDz / squareSumDxDyDz);
								}
							}
						}
						yyjson_doc_free(oneCurveDoc);
					}
				}
			}
			size_t len = 0;
			yyjson_write_err err;
			yyjson_mut_write_file(curveIdxPath_gbk.c_str(),mut_doc, YYJSON_WRITE_PRETTY,0,&err);
			yyjson_mut_doc_free(mut_doc);
			yyjson_doc_free(doc);

			json jRecv;
			jRecv["result"] = "ok";
			rpcResp.result = jRecv.dump();

		}
		rpcResp.result = RPC_OK;
	}
	else if (method == "getBlackList") {
		json res;
		json list = json::array();
		getBlackList(params["type"], list);
		res["type"] = params["type"];
		res["list"] = list;
		rpcResp.result = res.dump();
	}
	else if (method == "setBlackList") {
		setBlackList(params["type"], params["list"]);
		rpcResp.result = RPC_OK;
	}
	else if (method == "deleteBlackList") {
		delBlackList(params["type"], params["ip"]);
		rpcResp.result = RPC_OK;
	}
	else if (method == "addBlackList") {
		addBlackList(params["type"], params["val"]);
		rpcResp.result = RPC_OK;
	}
	else {
		bHandled = false;
	}
	return bHandled;
}

bool rpcHandler::handleMethodCall(string method, json params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	if (method.find("set") == 0) {
		if (params == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "rpc params cannot be null when setXXX method is called");
			return true;
		}
	}


	if (handleMethodCall_unclassified(method, params, rpcResp, session)) 
	{
		return true;
	}
	if (handleMethodCall_edgeDev(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_gamePad(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_audioPlayer(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_MoMng(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_alarmMng(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_userMng(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_OSFunc(method, params, rpcResp))
	{
		return true;
	}
	if (handleMethodCall_db(method, params, rpcResp, session))
	{
		return true;
	}
	if (handleMethodCall_IoMng(method, params, rpcResp,session))
	{
		return true;
	}
	if (handleMethodCall_debugFunc(method, params, rpcResp,session))
	{
		return true;
	}
	if (handleMethodCall_video(method, params, rpcResp, session)) {
		return true;
	}

	
	if (rpcResp.iBinLen > 0 || rpcResp.result != "" || rpcResp.error!="")
		return true;
	else {
		json jError = {
			{"code", -32601},
			{"message" , "Method not found"},
			{"method", method}
		};
		rpcResp.error = jError.dump();
	}

	return false;
}





bool rpcHandler::needLog(string method)
{
	if (method == "fs.writeFile" ||
		method == "heartbeat" ||
		method == "getSessions"||
		method == "getMpStatus" ||
		method == "getMoStatus" ||
		method == "getMoStatusTable"||
		method == "getMoStatusList" ||
		method == "getChanVal" ||
		method == "acq" ||
		method == "getLicenceStatus" ||
		method == "updateToken")
		return false;
	return true;
}

void rpcHandler::setLicenceStatus(json j)
{
	m_csLicenceStatus.lock();
	m_licenceStatus = j;
	m_csLicenceStatus.unlock();
}

bool rpcHandler::handleRpcRoute(string& strReq,json& jReq, RPC_RESP& rpcResp,std::shared_ptr<TDS_SESSION> pSession)
{
	string method = jReq["method"].get<string>();
	if (jReq.contains("tdsSession")) //使用tdsSession进行io透传
	{
		string tdsSession = jReq["tdsSession"].get<string>();
		shared_ptr<TDS_SESSION> pDestSession = ioSrv.getTDSSession(tdsSession);
		
		if (pDestSession == nullptr)
		{
			return true;
		}
		jReq["clientId"] = pSession->getRemoteAddr();
		jReq.erase("user");
		jReq.erase("token");
		string s = jReq.dump() + "\n\n";
		pDestSession->send((char*)s.c_str(), s.length());
		return true;
	}
	else if (jReq.contains("ioAddr"))
	{
		ioDev* pIoDev = nullptr;
		string strIoAddr = jReq["ioAddr"].get<string>();
		pSession->route_ioAddr = strIoAddr;
		pIoDev  = ioSrv.getIODev(strIoAddr);
		if (!pIoDev)
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到指定IO地址的IO设备");
			return true;
		}
	
		pIoDev->handleDevRpcCall(jReq,rpcResp);
		logRPCRoute(method, jReq["params"], *pSession);
		return true;
	}
	else if (jReq.contains("tag")) {
		string tag = jReq["tag"].get<string>();
		tag = TAG::addRoot(tag, pSession->org);

		//查找是否有直接绑定的设备
		ioDev* pIoDev = ioSrv.getIODevByTag(tag);
		if (pIoDev)
		{
			jReq.erase("tag");
			pSession->route_ioAddr = pIoDev->getIOAddrStr();
			pIoDev->handleDevRpcCall(jReq, rpcResp);
			logRPCRoute(method, jReq["params"], *pSession);
			return true;
		}

		//查看是否属于子服务.转变为对子服务的路由请求
		ioDev* pChildTds = ioSrv.getOwnerChildTdsDev(tag);
		if (pChildTds) {
			string childTdsTag = pChildTds->m_strTagBind;
			string tagInChild = TAG::trimRoot(tag, childTdsTag);
			jReq["tag"] = tagInChild;
			pChildTds->handleDevRpcCall(jReq, rpcResp);
			return true;
		}


		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到与该位号绑定的IO设备");
		return true;

		/*string tag = jReq["tag"].get<string>();
		tag = TAG::addRoot(tag, pSession->org);
		OBJ* pObj = prj.queryObj(tag);
		if (!pObj)
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "未找到该位号的监控对象");
			return true;
		}

		OBJ* pOwnerChlidTds = pObj->getOwnerChildTds();
		if (pOwnerChlidTds) {
			if (pMasterDs) {
				string childTdsTag = pOwnerChlidTds->getTag();
				tag = TAG::trimRoot(tag, childTdsTag);

				json sessionParams;
				sessionParams["tag"] = tag;

				json childRlt, childErr;
				pMasterDs->callChildTds(childTdsTag, jReq["method"], jReq["params"], childRlt, childErr,true,sessionParams);
				if (childRlt != nullptr) {
					rpcResp.result = childRlt.dump();
				}
				else if (childErr != nullptr) {
					rpcResp.error = childErr.dump();
				}
				else {
					LOG("[error]严重错误 mp.cpp %d\n", __LINE__);
				}
			}
			else {
				rpcResp.error = "\"error: master data service is not started\"";
			}
		}
		else {
			ioDev* pIoDev = ioSrv.getIODevByTag(tag);
			if (!pIoDev)
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到与该位号绑定的IO设备");
				return true;
			}
			pSession->ioAddr = pIoDev->getIOAddrStr();

			pIoDev->handleDevRpcCall(jReq, rpcResp, pSession);
			logRPCRoute(method, jReq["params"], *pSession);
			return true;
		}*/
	}
	//else if (jReq.contains("childTds"))
	//{
	//	jReq.erase("user");
	//	jReq.erase("token");
	//	pSession->route_childTds = jReq["childTds"];
	//	ioDev* pIoDev = ioSrv.getIODevByTag(pSession->route_childTds);
	//	if (pIoDev && pIoDev->m_devSubType == TDSP_SUB_TYPE::childTds)
	//	{
	//		pIoDev->handleDevRpcCall(jReq, rpcResp, pSession);
	//	}
	//	else {
	//		rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到与该位号绑定的 TDS下级服务 设备");
	//	}
	//	return true;
	//}
	return false;
}

void rpcHandler::logRPCRoute(string method,json& params,RPC_SESSION& session) {
	if (method == "startRepel") {
		json logParams;
		logParams["src"] = "用户:" + session.user;
		logParams["type"] = "探驱联动开始";
		logParams["org"] = session.org;
		logParams["host"] = session.remoteAddr;
		logParams["info"] = "设备名称:" + session.route_tag + ",设备地址:" + session.route_ioAddr + ",水平角:" + str::fromFloat(params["pan"].get<float>()) + ",俯仰角:" + str::fromFloat(params["tilt"].get<float>());
		logSrv.rpc_addLog(logParams, session);
	}	
	else if (method == "stopRepel") {
		json logParams;
		logParams["src"] = "用户:" + session.user;
		logParams["type"] = "探驱联动结束";
		logParams["org"] = session.org;
		logParams["host"] = session.remoteAddr;
		logParams["info"] = "设备名称:" + session.route_tag + ",设备地址:" + session.route_ioAddr;
		logSrv.rpc_addLog(logParams, session);
	}
}

bool rpcHandler::isGB2312Pkt(string& req)
{
	//如果jsonRPC的json结构的第一个字段是charset，根据charset的参数决定编码类型
	size_t pos = req.find("GB2312");
	if (pos != string::npos)
	{
		int quoteNum = 0;  //gb2312前面有3个冒号，表示是第一个字段。  排除协议内部也有gb2312字段的可能性。
		for (int i = 0; i < pos; i++)
		{
			if (req[i] == '"') {
				quoteNum++;
			}
		}
		if (quoteNum == 3)
			return true;
	}
	pos = req.find("gb2312");
	if (pos != string::npos)
	{
		int quoteNum = 0;
		for (int i = 0; i < pos; i++)
		{
			if (req[i] == '"') {
				quoteNum++;
			}
		}
		if (quoteNum == 3)
			return true;
	}

	return false;
}


void thread_handleRpcCallAsyn(string str, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl, bool bEdgeDevMode) {
	RPC_RESP resp;
	rpcSrv.handleRpcCall(str, resp, pSession, bAccessCtrl,bEdgeDevMode);
	pSession->send((unsigned char*)resp.strResp.data(), resp.strResp.length(), false);
}


void rpcHandler::handleRpcCallAsyn(string& strReq, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl, bool bEdgeDevMode)
{
	thread t(thread_handleRpcCallAsyn, strReq, pSession, bAccessCtrl, bEdgeDevMode);
	t.detach();
}

void rpcHandler::handleRpcCall(string& strReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl,bool bEdgeDevMode)
{
	string error = "";
	string method = "";
	json id = nullptr;
	json clientId = nullptr;
	bool bGB2312 = false;
	bool bNeedLog = true;
	std::map<string, RPC_SESSION>::iterator iter;

	strReq = str::trim(strReq);
	if (strReq.length() == 0) 
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_InvalidReqFmt, "invalid request format. request length is 0.");
		goto HANDLE_END;
	}

	
	bGB2312 = isGB2312Pkt(strReq);
	if(bGB2312)
		strReq = charCodec::gb_to_utf8(strReq);


	pSession->req = strReq;


	try
	{
		//解析请求基本信息
		//调试用 fs::writeFile(fs::appPath() + "/req.json", strReq);
		json jReq = json::parse(strReq);
		if (!jReq.contains("method"))
		{
			LOG("[error][TDS-RPC]协议数据包必须包含method字段\n" + strReq);
			return;
		}

		method = jReq["method"].get<string>();
		statisCall(method);

		if (m_mapDisableMethod.find(method) != m_mapDisableMethod.end()) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL,"method disabled");
			goto HANDLE_END;
		}

		if (jReq.contains("dbPath")){
			pSession->dbpath = jReq["dbPath"].get<string>();
		}
		if (jReq.contains("language")) {
			pSession->language = jReq["language"].get<string>();
		}
		

		//调试命令会话不纳入统计
		if (!pSession->isDebug) {
			m_csRpcSessions.lock();
			iter = m_mapRpcSessions.find(pSession->remoteAddr);
			if (iter != m_mapRpcSessions.end()) {
				RPC_SESSION& sess = iter->second;
				sess.sLastRecvTime = timeopt::nowStr();
				sess.lastMethodCalled = method;
			}
			else {
				RPC_SESSION sess;
				sess.remoteAddr = pSession->remoteAddr;
				sess.remoteIP = pSession->remoteIP;
				sess.remotePort = pSession->remotePort;
				sess.sLastRecvTime = timeopt::nowStr();
				sess.lastMethodCalled = method;
				m_mapRpcSessions[pSession->remoteAddr] = sess;
			}
			m_csRpcSessions.unlock();
		}


		json params;
		if (jReq.contains("params"))
			params = jReq["params"];
		id = jReq["id"];
		if (id == nullptr) {
			rpcResp.isNotification = true;
			pSession->isNotification = true;
		}

		
			
		//对部分命令日志记录
		bNeedLog = needLog(method);
		if (bNeedLog)
			LOG("[trace]RPC请求:\r\n" + strReq + "\r\n");
		if(method == "output" || method == "openStream")
			LOG("[warn]RPC请求:\r\n" + strReq + "\r\n");

		//心跳最先处理
		if (method == "heartbeat")
		{
			rpcResp.result = RPC_OK;
			goto HANDLE_END;
		}


		//访问控制
		if (method == "login")
		{
			userMng.rpc_login(params, rpcResp, pSession->getRpcSession());
			goto HANDLE_END;
		}
		else if (method == "logout")
		{
			userMng.rpc_logout(params, rpcResp, pSession->getRpcSession());
			goto HANDLE_END;
		}

		//验证user;    没有打开权限控制，数据包也可以携带user，不进行验证，但是有权限控制。用于测试场景
		json jUser;
		if (jReq["user"] != nullptr)
		{
			pSession->user = jReq["user"].get<string>();
			jUser = userMng.getUser(pSession->user);
			if (jUser != nullptr) {
				pSession->org = jUser["org"].get<string>();
				pSession->role = jUser["role"].get<string>();
			}
		}
		else if (pSession->user != "") { //使用cookie设置的用户名
			jUser = userMng.getUser(pSession->user);
			if (jUser != nullptr) {
				pSession->org = jUser["org"].get<string>();
				pSession->role = jUser["role"].get<string>();
			}
		}
		else
		{
			if (tds->conf->enableAccessCtrl) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_userMissing, "user is not set to call api");
				goto HANDLE_END;
			}
			else {//没开权限控制，且没有设置用户，默认用户都是admin
				pSession->user = "admin";
				pSession->role = "管理员";
			}
		}

		//非管理员账户必须预先分配对象树管理权限
		if (pSession->role != "管理员") {
			bool isPermissionAssigned = true;
			if (jUser["permission"].is_null()) {
				isPermissionAssigned = false;
				if (jUser["permission"]["mo"].is_null()) {
					isPermissionAssigned = false;
				}
			}
			if (!isPermissionAssigned) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_noObjTreePermission, "permission denied,a non admin user must be assigned permissions first");
				goto HANDLE_END;
			}
		}

		//test账户具有所有功能模块的浏览权限，但没有控制权限，用户功能演示。可以认为是一个只能浏览的管理员账号
		if (pSession->user == "test") {
			if (method.find("set") != string::npos ||
				method.find("add") != string::npos ||
				method.find("delete") != string::npos ||
				method.find("write") != string::npos ||
				method.find("start") != string::npos ||
				method.find("stop") != string::npos ||
				method.find("run") != string::npos ||
				method.find("db.delete") != string::npos ||
				method.find("db.delete") != string::npos ||
				method.find("db.update") != string::npos ||
				method == "output" ||
				method == "input") {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_noPermission, "no permission","没有权限");
				goto HANDLE_END;
			}
		}
			
		if (bAccessCtrl && tds->conf->enableAccessCtrl) {
			if ( jReq["token"] == nullptr && jReq["pwd"] == nullptr)
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenMissing, "access denied, please set access token or password");
				goto HANDLE_END;
			}

			//user token to access
			if (jReq["token"] != nullptr && jReq["token"] != "")
			{
				pSession->token = jReq["token"].get<string>();
				string token = pSession->token;
				string user = pSession->user;
				if (!userMng.checkToken(user, token))
				{
					//LOG("[warn]认证失败，token验证未通过,user=%s,token=%s,method=%s",user.c_str(),token.c_str(),method.c_str());
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "access denied, invalid access token");
					goto HANDLE_END;
				}
			}

			//use password to access, for api testing, do not use in a productin enviroment
			if (jReq["pwd"] != nullptr && jReq["pwd"] != "")
			{
				string pwd = jReq["pwd"].get<string>();
				string user = pSession->user;
				if (!userMng.checkPwd(user, pwd))
				{
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "access denied, invalid password");
					goto HANDLE_END;
				}
			}
		}

		//设备模式不开启中继转发处理.返回true表示是中继命令.放在用户认证前面处理.
		//if (!bEdgeDevMode) 
		//{
			if (handleRpcRoute(strReq, jReq, rpcResp, pSession))
			{
				goto HANDLE_END;
			}
		//}
		
		//通知消息，无需生成响应，转发后直接返回
		if (method == "notify")//来自于tds客户端的通知消息。 转发给所有的其他tds客户端
		{
			notify("notify", params, false,pSession);
			return;
		}
		//后端总线，实现一种微前端模块之间可以相互调用函数的机制
		//前端总线，可以在前端的app之间实现相互调用，相比于后端总线，只能调用本机浏览器上的app
		else if (method.find("app.") != string::npos) 
		{
			notify(method, params, false, pSession);
			return;
		}

		
		//先使用外部注册的handler受理请求
		if (m_pluginHandler)
		{
			bool bHandled = m_pluginHandler(strReq, rpcResp, error);
			if (bHandled)
			{
				goto HANDLE_END;
			}
		}
		

		//tds自身受理
		if (!handleMethodCall(method, params, rpcResp, pSession->getRpcSession())) {
			LOG("[warn]调用不存在的rpc方法\r\n" + strReq);
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("[error]RPC请求包处理异常:\r\n错误信息:" + errorType + "\r\n数据包:\r\n" + strReq);
		json jError = {
				{"code", -32700},
				{"message" , "Parse error," + errorType}
		};

		//TDSP设备协议。不发送回包。
		if (pSession->type.find("ioDev") == string::npos)
		{
			rpcResp.error = jError.dump();
		}
		goto HANDLE_END;
	}

HANDLE_END:

	//组装jsonRPC
	if (rpcResp.error != "")
	{
		rpcResp.strResp = "{\"jsonrpc\":\"2.0\",\"error\":" + rpcResp.error + ",\"id\":" + id.dump();
	}
	else if (rpcResp.result != "")
	{
		rpcResp.strResp = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump();

		//info放在result前面打印，因为result可能比较长，info短，放前面测试观察方便
		if (rpcResp.params != "") {
			rpcResp.strResp += ",\"params\":" + rpcResp.params;
		}

		if (rpcResp.info != "") {
			rpcResp.strResp += ",\"info\":\"" + rpcResp.info + "\"";
		}

		if (rpcResp.dbQueryInfo != "") {
			rpcResp.strResp += ",\"dbLog\":\"" + rpcResp.dbQueryInfo + "\"";
		}
			
		rpcResp.strResp += ",\"result\":" + rpcResp.result;
	}


	//route参数，路由请求的回包包含请求中的路由参数
	if (pSession->route_ioAddr != "")
	{
		rpcResp.strResp += ",\"ioAddr\":\"" + pSession->route_ioAddr + "\"";
	}
	if (pSession->route_tag != "")
	{
		rpcResp.strResp += ",\"tag\":\"" + pSession->route_tag + "\"";
	}
	if (pSession->route_childTds != "")
	{
		rpcResp.strResp += ",\"childTds\":\"" + pSession->route_childTds + "\"";
	}


	rpcResp.strResp += "}\n\n";


	//数据过长或者频率过高的命令不记录日志
	if (rpcResp.result != "")
	{
		string strRespForLog = "";
		if (method == "db.select" || method == "getObj")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$resultLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}

		if (strRespForLog != "")
			LOG("[trace]RPC响应:\r\n" + strRespForLog + "\r\n");
		else if (bNeedLog)
			LOG("[trace]RPC响应:\r\n" + rpcResp.result + "\r\n");
	}


	//处理二进制响应
	if (rpcResp.iBinLen > 0)
	{
		LOG("[trace]RPC响应: 二进制数据 len = " + str::fromInt(rpcResp.iBinLen));
	}
}


void rpcHandler::saveDataFromUrl(string& strUrl, TIME& stTime, string& strTag, string suffix)
{
	string strTmpFile;
	if (strUrl.find("http") != string::npos)
	{
		size_t pos = strUrl.rfind('.');
		string strSuffix = strUrl.substr(pos);
		strTmpFile += fs::appPath();
		string id = str::replace(strUrl, "/", "_");
		strTmpFile += "/Temp/" + id ;
		DownloadHTTPFile(strUrl, strTmpFile); //http下载文件到临时目录
	}
	else
	{
		strTmpFile = strUrl;
	}

	string strTagTmp = strTag.c_str();
	strTagTmp = str::replace(strTagTmp,".", "\\");

	TIME stDateTime = stTime;

	string strTargetFile;
	strTargetFile=str::format("\\%04d%02d\\%02d\\%s\\%02d%02d%02d%s",
		stDateTime.wYear, stDateTime.wMonth, stDateTime.wDay, strTagTmp, stDateTime.wHour, stDateTime.wMinute, stDateTime.wSecond, suffix.c_str());
	string allDBPath = tds->conf->dbPath;
	strTargetFile = allDBPath + strTargetFile;
	fs::createFolderOfPath(strTargetFile);
	//MoveFile(strTmpFile.c_str(), strTargetFile.c_str());
}

void rpcHandler::cleanRpcSession()
{
	if (timeopt::CalcTimePassSecond(m_lastCleanTime) > 180) {
		m_csRpcSessions.lock();
		vector<string> toErase;
		for (auto& i : m_mapRpcSessions) {
			if (timeopt::calcTimePassSecond(i.second.sLastRecvTime) > 180) {
				toErase.push_back(i.first);
			}
		}
		for (int i = 0; i < toErase.size(); i++) {
			string& s = toErase[i];
			m_mapRpcSessions.erase(s);
		}
		m_csRpcSessions.unlock();
		m_lastCleanTime = timeopt::now();
	}
}

void rpcHandler::rpc_output(json params, RPC_RESP& resp, RPC_SESSION session)
{
	json val = nullptr;

	//获取输出参数
	if (params.is_object())
	{
		if (params["val"] != nullptr)
			val = params["val"];
	}
	else
	{
		val = params; 
	}


	//查找需要输出的位号
	string tag, rootTag; 
	if (params["tag"] != nullptr)
		tag = params["tag"].get<string>();
	if (params["rootTag"] != nullptr && params["rootTag"] != "")
		tag = params["rootTag"].get<string>() + "." + tag;
	tag = TAG::addRoot(tag, session.org);
	MP* pmp = prj.GetMPByTag(tag, session.language);
	if (!pmp)
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found:" + tag);
		LOG("[warn]output请求错误," + resp.error);
		return;
	}

	//用户权限校验
	if (!userMng.checkTagWritePermission(session.user, tag)) {
		resp.error = makeRPCError(RPC_ERROR_CODE::AUTH_noWritePermission, "no write permission");
		LOG("[error][控制输出]ouput，没有权限,tag=%s,user=%s", tag.c_str(), session.user.c_str());
		return;
	}


	//值类型校验
	if (pmp->m_valType == VAL_TYPE::boolean)
	{
		if (!val.is_boolean())
		{
			if (val.is_string() && val.get<string>() == "取反") {
				if (pmp->m_curVal.is_boolean())
					val = !pmp->m_curVal.get<bool>();
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::MO_currentValIsNull, "current value is null");
					LOG("[warn]output请求错误," + resp.error);
					return;
				}
			}
			else if (val.is_null()) //开关量输出值省略 表示输出当前值取反
			{
				if(pmp->m_curVal.is_boolean())
					val = !pmp->m_curVal.get<bool>();
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::MO_currentValIsNull, "current value is null");
					LOG("[warn]output请求错误," + resp.error);
					return;
				}
			}
			else
			{
				resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeBool, "output val should be bool type");
				LOG("[warn]output请求错误," + resp.error);
				return;
			}
		}
	}
	else if (pmp->m_valType == VAL_TYPE::Float)
	{
		if (!val.is_number()) {
			resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeNumber, "output val should be number type");
			LOG("[warn]output请求错误," + resp.error);
			return;
		}
	}
	else if (pmp->m_valType == VAL_TYPE::integer)
	{
		if (!val.is_number_integer()) {
			if (pmp->m_isEnum && val.is_string()) {
				string enumVal = val.get<string>();
				int iVal = 0;
				if (pmp->getValByEnumVal(enumVal, iVal)) {
					val = iVal;
				}
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::OBJ_enumValNotFound, "enum val not found," + enumVal);
					LOG("[warn]output请求错误," + resp.error);
					return;
				}
			}
			else if (val.is_string() && val.get<string>() == "取反" && pmp->m_curVal.is_number_integer()) {
                //modbus寄存器在工程实践中，常常用03保持寄存器0，1数值代表一个bool量
				int iVal = pmp->m_curVal.get<int>();
				if (iVal != 0) {
					val = 0;
				}
				else {
					val = 1;
				}
			}
			else {
				resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeNumber, "output val should be integer type");
				LOG("[warn]output请求错误," + resp.error);
				return;
			}
		} 
	} 


	json rlt,err;

	bool syncCall = true;
	if (session.isNotification)
		syncCall = false;
	if (params["waitResp"].is_boolean() && params["waitResp"].get<bool>() == false) {
		syncCall = false;
	}

	pmp->output(val, rlt, err, syncCall);
	if (syncCall) {
		if(rlt!=nullptr)
		{
			json jDe = params; //返回输出成功的结果
			jDe["val"] = val;
			resp.result = rlt.dump();

			pmp->input(val);
			pmp->saveToDB();
		}
		else
		{
			resp.error = err.dump();
		}
	}
	else {
		resp.result = "\"output cmd sended\"";
	}

	string valDesc = pmp->getValDesc(val);

	json logParams;
	logParams["src"] = "用户:" + session.user;
	logParams["object"] = tag;
	logParams["type"] = "控制输出";
	logParams["org"] = session.org;
	logParams["host"] = session.remoteAddr;
	logParams["info"] = "设置为:" + valDesc;;
	logSrv.rpc_addLog(logParams, session);
}

struct INPUT_DE {
	string tag;
	json val;
	json file;
	json ioAddr;
	string sTime;
	TIME time;
};


void rpcHandler::rpc_input(json params,RPC_RESP& resp, RPC_SESSION session)
{
	//输入 位号，值，文件数据，时间 四元组。 文件不一定有
	string rootTag = "";
	vector<INPUT_DE> inputDEList;
	if (params.is_object()) {
		//对象属性模式输入
		if (params.find("data") != params.end()) {
			json deList = params["data"];

			if (params.find("tag") != params.end()) {
				rootTag = params["tag"];
			}
			if (params.find("objID") != params.end()) {
				string objID = params["objID"];
				OBJ* pO = prj.getObjByID(objID);
				if (pO) {
					rootTag = pO->getTag("",session.language);
				}
				else {
					resp.error = makeRPCError(OBJ_specifiedObjIDNotFound, "specified object id not found");
					return;
				}
			}

			//对象属性模式转数组处理
			if (deList.size() > 0) {
				for (int i = 0; i < deList.size(); i++) {
					json& j = deList[i];
					INPUT_DE de;
					de.tag = j["tag"];
					de.val = j["val"];
					de.file = j["file"];
					inputDEList.push_back(de);
				}
			}
		}
		else{
			if (params["val"] == nullptr) {
				resp.error = makeRPCError(TEC_paramMissing, "param val must be specified");
				return;
			}
			if (params["tag"] == nullptr && params["ioAddr"] == nullptr) {
				resp.error = makeRPCError(TEC_paramMissing, "param ioAddr or tag must be specified");
				return;
			}

			json tag = params["tag"];
			json val = params["val"];
			json file = params["file"];
			json time = params["time"];
			if (params.contains("rootTag"))
				rootTag = params["rootTag"].get<string>();
			//单位号模式输入
			/*
				"params": {
				"tag": "客厅.温度",
				"time": "2020-02-14 20:20:20",
				"val": 35
			}
			*/
			if (tag.is_string()) {
				INPUT_DE de;
				de.tag = tag;
				de.val = val;
				de.file = file;
				if (time.is_string()) {
					de.sTime = time;
					de.time = timeopt::str2st(de.sTime);
				}
				else {
					de.time = timeopt::now();
					de.sTime = de.time.toStr();
				}
				inputDEList.push_back(de);
			}
			//多位号模式 （采集时间相同）
			/*
			"params": {
				"tag": ["客厅.温度","客厅.湿度"],
				"time": "2020-02-14 20:20:20",
				"val": [26.5,65.1]
			}
			*/
			else if (tag.is_array()) {
				for (int i = 0; i < tag.size(); i++) {
					INPUT_DE de;
					de.tag = tag[i];
					de.val = val[i];

					if (time.is_string()) {
						de.sTime = time;
						de.time = timeopt::str2st(de.sTime);
					}
					else {
						de.time = timeopt::now();
						de.sTime = de.time.toStr();
					}

					inputDEList.push_back(de);
				}
			}
			else {
				resp.error = makeRPCError(TEC_FAIL, "param tag format error");
				return;
			}
		}
	}
	//多位号模式输入
	/*
	"params": {
        "tag":"客厅",
        "data":[
            {
                "tag":"温度",
                "val":26.5
            },{
                "tag":"湿度",
                "val":23.5
            }
        ]
    }
	*/
	else if (params.is_array()) { 
		for (auto& jDe : params) {
			INPUT_DE de;
			de.tag = jDe["tag"];
			if (jDe.contains("rootTag")) {
				string rootTagTmp = jDe["rootTag"].get<string>();
				de.tag = TAG::addRoot(de.tag, rootTagTmp);
			}
				

			if (jDe["time"].is_string()) {
				de.sTime = jDe["time"];
				de.time = timeopt::str2st(de.sTime);
				if (!de.time.isValid()) {
					continue;
				}
			}
			else {
				de.time = timeopt::now();
				de.sTime = de.time.toStr();
			}
			de.val = jDe["val"];
			de.file = jDe["file"];

			inputDEList.push_back(de);
		}
	}
	else {
		resp.error = makeRPCError(TEC_FAIL, "param should be array or object");
		return;
	}


	//使用位号输入
	if (inputDEList.size() > 0) {
		//监测点组输入模式
		json jTagNotExist = json::array();
		vector<MP*> vecMps;
		for (int i = 0; i < inputDEList.size(); i++) {
			INPUT_DE& de = inputDEList[i];
			string tag = de.tag;
			json& val = de.val;
			json& file = de.file;
			tag = TAG::addRoot(tag, rootTag);
			tag = TAG::addRoot(tag, session.org);

			if (tag != "")
			{
				MP* pmp = prj.GetMPByTag(tag, session.language);
				if (pmp)
				{
					pmp->input(val, &file, &de.time);
					if(pmp->m_bEnableIO)
						vecMps.push_back(pmp);
				}
				else {
					//params.erase("tag");
					//string sDe = params.dump();
					//db.Insert(tag, sDe);
					jTagNotExist.push_back(tag);
				}
			}
		}


		if (vecMps.size() > 0) {

			//监测点组中有任意一个点需要保存，则全部保存
			//可能某些监测点发生了值变化需要保存，有些点没有变化。统一保存。因为某些可视化页面必须同一个时间点，两个位号的数据都有
			bool needSave = false;
			for (int i = 0; i < vecMps.size(); i++) {
				MP* pmp = vecMps[i];
				if (pmp->needSaveToDB()) {
					needSave = true;
				}
			}

			if (needSave) {
				for (int i = 0; i < vecMps.size(); i++) {
					MP* pmp = vecMps[i];
					pmp->saveToDB();
				}
			}


			//发送状态更新通知
			json jStatusNotify = json::array();
			for (int i = 0; i < vecMps.size(); i++) {
				MP* pmp = vecMps[i];
				json jDe;
				jDe["tag"] = pmp->getTag("",session.language);
				jDe["val"] = pmp->m_curVal;
				jDe["file"] = pmp->m_curFileData;
				jDe["time"] = pmp->m_stDataLastUpdate.toStr();
				jDe["valDesc"] = pmp->getValDesc(false);
				jStatusNotify.push_back(jDe);
			}
			rpcSrv.notify("onDataUpdate", jStatusNotify);
			
			resp.result = "\"ok\"";
		}
		else {
			resp.error = makeRPCError(MO_specifiedTagNotFound, "tag not exist");
			return;
		}
	}
}

json map2array(json& j) {
	json jArray = json::array();
	for (auto& item : j.items()) {
		json jNode = json::object();
		jNode["name"] = item.key();

		json jChildObj = item.value();
		json jChildArray = map2array(jChildObj);

		if(jChildArray.size() > 0)
			jNode["children"] = jChildArray;

		jArray.push_back(jNode);
	}

	return jArray;
}


string rpcHandler::rpc_getTopoList(json params, string& error,RPC_SESSION session)
{
	string topopath = tds->conf->confPath + "/topo";
	fs::normalizationPath(topopath);

	vector<fs::FILE_INFO> filist;
	fs::getFileList(filist, topopath, true,".svg");

	map<string, string> mapTopo; //按照层级排序
	vector<string> topoList;
	for (int i = 0; i < filist.size(); i++)
	{
		if (filist[i].path.find("/res") != string::npos) {
			continue;
		}
		else if (filist[i].path.find("/asset") != string::npos) {
			continue;
		}
		else if (filist[i].path.find("/template") != string::npos) {
			continue;
		}
		string topoName = str::trimSuffix(filist[i].name, ".svg");
		mapTopo[str::fromInt(TAG::getMoLevel(topoName)) + topoName] = topoName;
	}
	for (auto& i : mapTopo)
	{
		topoList.push_back(i.second);
	}

	if (session.user != "")
	{
		//删除没有权限的拓扑图
		for (int i = 0; i < topoList.size(); i++)
		{
			string s = topoList[i];
			if (s == "root" || s == "主视图")
				s = "";


			if (!userMng.checkTagPermission(session.user, s))
			{
				topoList.erase(topoList.begin() + i);
				i--;
			}
		}
	}

	string mode = "list";
	if (params.contains("mode")) {
		mode = params["mode"];
	}

	json j;
	if (mode == "list") {
		j = topoList;
	}
	else {
		string topoTreeFile = topopath + "/topoTree.json";
		if (fs::fileExist(topoTreeFile)) {
			string s;
			fs::readFile(topoTreeFile,s);
			try
			{
				j = json::parse(s);
			}
			catch (const std::exception& e)
			{
				string s = e.what();
				LOG("[error]解析拓扑树配置错误," + s);
			}
		}
		else {
			j = nullptr;
		}

		//根据拓扑图的命名自动生成树结构，暂时废弃
		/*else {
			json jTree = json::object();
			for (int i = 0; i < topoList.size(); i++) {
				string name = topoList[i];
				vector<string> nodes;
				str::split(nodes, name, ".");

				json* jNode = &jTree;
				for (int j = 0; j < nodes.size(); j++) {
					string node = nodes[j];
					if (!(*jNode).contains(node)) {
						(*jNode)[node] = json::object();
					}
					jNode = &(*jNode)[node];
				}
			}

			j = map2array(jTree);

			json jMain;
			for (int i = 0; i < j.size(); i++) {
				json jtmp = j[i];
				string name = jtmp["name"].get<string>();
				if (name == "root" || name == "主视图") {
					jMain = jtmp;
					j.erase(j.begin() + i);
				}
			}
			j.insert(j.begin(), jMain);
		}*/
	}
	return j.dump();
}

void rpcHandler::rpc_getMpStatis(json params, RPC_RESP& resp, RPC_SESSION session) {
	MP_STATIS mpStatis;
	vector<MP*> allMp;
	prj.GetAllChildMp(allMp);
	for (int i = 0; i < allMp.size(); i++){
		MP* pmp = allMp[i];
		if (pmp->m_ioType == "v") {
			mpStatis.Var++;
		}
		else if (pmp->m_valType == VAL_TYPE::Float || pmp->m_valType == VAL_TYPE::integer) {
			if (pmp->m_ioType == "i") {
				mpStatis.AI++;
			}
			else if (pmp->m_ioType == "io" || pmp->m_ioType == "o") {
				mpStatis.AO++;
			}
		}
		else if (pmp->m_valType == VAL_TYPE::boolean) {
			if (pmp->m_ioType == "i") {
				mpStatis.DI++;
			}
			else if (pmp->m_ioType == "io" || pmp->m_ioType == "o") {
				mpStatis.DO++;
			}
		}
	}

	json j;
	j["total"] = allMp.size();
	j["AI"] = mpStatis.AI;
	j["AO"] = mpStatis.AO;
	j["DI"] = mpStatis.DI;
	j["DO"] = mpStatis.DO;
	j["Var"] = mpStatis.Var;

	resp.result = j.dump();
}

void rpcHandler::rpc_getObjStatis(json params, RPC_RESP& resp, RPC_SESSION session)
{
	vector<string> rootTagListOrg; //用于返回统计分组时作为分组的key
	vector<string> rootTagList;
	if (params["rootTag"].is_string())
	{
		string rootTag = params["rootTag"].get<string>();
		rootTagListOrg.push_back(rootTag);
		rootTag = TAG::addRoot(rootTag, session.org);
		rootTagList.push_back(rootTag);
	}
	else if(params["rootTag"].is_array()){
		json jRootTagList = params["rootTag"];
		for (int i = 0; i < jRootTagList.size(); i++) {
			string rootTag = jRootTagList[i].get<string>();
			rootTagListOrg.push_back(rootTag);
			rootTag = TAG::addRoot(rootTag, session.org);
			rootTagList.push_back(rootTag);
		}
	}
	else {
		rootTagList.push_back("");
		rootTagListOrg.push_back("");
	}

	string mode = "groupByType";
	if (params.contains("mode")) {
		mode = params["mode"];
	}

	json jStatisRlt = json::object();
	for (int i = 0; i < rootTagList.size(); i++) {
		string tag = rootTagList[i];
		string tagOrg = rootTagListOrg[i];
		OBJ* pMo = prj.queryObj(tag, session.language);
		map<string, OBJ_STATIS> rlt;
		if (pMo)
		{
			pMo->statisChildObj(rlt);

			if (mode == "groupByType") {
				json jList = json::array();
				for (auto& iter : rlt) {
					json j;
					j["customType"] = iter.second.customType;
					j["count"] = iter.second.count;
					j["online"] = iter.second.online;
					j["offline"] = iter.second.offline;
					j["alarm"] = iter.second.alarm;
					j["fault"] = iter.second.fault;
					j["normal"] = iter.second.normal;
					jList.push_back(j);
				}

				if (rootTagList.size() == 1) {
					jStatisRlt = jList;
				}
				else {
					jStatisRlt[tagOrg] = jList;
				}
			}
			else if (mode == "total") {
				OBJ_STATIS total;
				total.customType = "*";
				for (auto& iter : rlt) {
					total.count += iter.second.count;
					total.online += iter.second.online;
					total.offline += iter.second.offline;
					total.alarm += iter.second.alarm;
					total.fault += iter.second.fault;
					total.normal += iter.second.normal;
				}
				json j;
				j["customType"] = "*";
				j["count"] = total.count;
				j["online"] = total.online;
				j["offline"] = total.offline;
				j["alarm"] = total.alarm;
				j["fault"] = total.fault;
				j["normal"] = total.normal;

				if (rootTagList.size() == 1) {
					jStatisRlt = j;
				}
				else
					jStatisRlt[tagOrg] = j;
			}

		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "没有找到需要统计的根对象" + tag);
			return;
		}
	}

	resp.result = jStatisRlt.dump();
}


void rpcHandler::rpc_getMoStatis(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string rootTag = ""; 
	if (params.contains("rootTag"))
	{
		rootTag = params["rootTag"].get<string>();
	}
	rootTag = TAG::addRoot(rootTag,session.org);

	string fmt = "";
	if (params.contains("fmt")) {
		fmt = params["fmt"];
	}

	OBJ* pMo = prj.queryObj(rootTag, session.language);
	json jStatis;
	if (pMo)
	{
		pMo->statisChildMo(jStatis);
		json almStatis = getAlarmStatis(rootTag, session);
		jStatis["alarm"] = almStatis["alarm"];
		jStatis["warn"] = almStatis["warn"];
		jStatis["tag"] = params["rootTag"];

		//该模式暂时只给topo用，后续还要优化
		if (fmt == "mplist") {
			json jRlt = json::array();
			json de;

			de["tag"] = "statis.customOrg";
			de["val"] = jStatis["customOrg"];
			jRlt.push_back(de);

			de["tag"] = "statis.smartDev.total";
			de["val"] = jStatis["smartDev"];
			jRlt.push_back(de);

			de["tag"] = "statis.smartDev.online";
			de["val"] = jStatis["online"];
			jRlt.push_back(de);

			de["tag"] = "statis.smartDev.offline";
			de["val"] = jStatis["offline"];
			jRlt.push_back(de);

			de["tag"] = "statis.alarms.alarmCount";
			de["val"] = jStatis["alarm"];
			jRlt.push_back(de);

			de["tag"] = "statis.alarms.warnCount";
			de["val"] = jStatis["warn"];
			jRlt.push_back(de);

			resp.result = jRlt.dump(4);
			resp.params = params.dump(4);
		}
		else {
			resp.result = jStatis.dump(2);
		}
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "没有找到需要统计的根对象");
	}
}

json rpcHandler::getAlarmStatis(string rootTag, RPC_SESSION session) {
	json querier = nullptr;
	if (session.user != "")
		querier["user"] = session.user;
	if (rootTag != "")
		querier["rootTag"] = rootTag;

	almServer* pAlmSrv = &almSrv;

	vector<ALARM_INFO*> vecAlarms = pAlmSrv->tableCurrent.query(querier);
	int iAlarmCount = 0;
	int iWarnCount = 0;
	for (int i = 0; i < vecAlarms.size(); i++)
	{
		ALARM_INFO* pai = vecAlarms[i];
		if (pai->bRecover)
			continue;

		if (pai->level == "alarm")
			iAlarmCount++;
		if (pai->level == "warn")
			iWarnCount++;
	}


	//全局报警禁用功能
	if (!tds->conf->enableGlobalAlarm)
	{
		iAlarmCount = 0;
		iWarnCount = 0;
	}

	json jAlmStatis;
	jAlmStatis["alarm"] = iAlarmCount;
	jAlmStatis["warn"] = iWarnCount;

	return jAlmStatis;
}



void rpcHandler::rpc_getDevStatis_old(json params, RPC_RESP& resp,RPC_SESSION session)
{
	string rootTag = session.org; //absolute queryRoot
	if (params.contains("rootTag"))
	{
		string relativeQueryRoot = params["rootTag"].get<string>();
		rootTag = TAG::addRoot(relativeQueryRoot, rootTag);
	}
	string devType = "*";
	if (params.contains("devType"))
	{
		devType = params["devType"].get<string>();
	}

	//仅统计1级设备
	vector<ioDev*> m_devList;
	for (auto& i : ioSrv.m_vecChildDev)
	{
		if (i->m_strTagBind == "")
			continue;

		if (devType != "*" && i->m_devType != devType)
			continue;

		if (rootTag != "")
		{
			string tagBind = i->m_strTagBind;
			if (tagBind.find(rootTag) == string::npos)
				continue;
		}

		m_devList.push_back(i);
	}


	//生成统计信息
	int onlineCount = 0;
	for (auto& i : m_devList)
	{
		if (i->m_bOnline)
			onlineCount++;
	}

	string fmt = "tree";
	if (params.contains("fmt"))
	{
		if(params["fmt"].get<string>() == "list")
		{
			fmt = "list";
		}
	}


	json jDev;
	jDev["total"] = m_devList.size();
	jDev["online"] = onlineCount;
	jDev["offline"] = m_devList.size() - onlineCount;
	json jRlt;
	jRlt["smartDev"] = jDev;


	//统计报警
	json jAlmStatis = getAlarmStatis(rootTag, session);
	jRlt["alarms"] = jAlmStatis;


	if (fmt == "tree")
	{
		resp.result = jRlt.dump(4);
	}
	else
	{
		json jRlt = json::array();
		json de;
		de["tag"] = "statis.smartDev.total";
		de["val"] = m_devList.size();
		jRlt.push_back(de);

		de["tag"] = "statis.smartDev.online";
		de["val"] = onlineCount;
		jRlt.push_back(de);

		de["tag"] = "statis.smartDev.offline";
		de["val"] = m_devList.size() - onlineCount;;
		jRlt.push_back(de);

		de["tag"] = "statis.alarms.alarmCount";
		//de["val"] = iAlarmCount;
		//jRlt.push_back(de);

		de["tag"] = "statis.alarms.warnCount";
		//de["val"] = iWarnCount;
		//jRlt.push_back(de);

		/*if (rootTag != "")
		{
			for (int i = 0; i < jRlt.size(); i++)
			{
				json& de = jRlt[i];
				de["tag"] = rootTag + "." + de["tag"].get<string>();
			}
		}*/

		resp.result = jRlt.dump(4);
		resp.params = params.dump(4);
	}
}

string rpcHandler::rpc_getMoOnlineStatus(json params, string& error)
{
	//智能设备的在线状态  专用监测点位号
	vector<ioDev*> arySmartDev;
	ioSrv.getAllSmartDev(arySmartDev);
	json list = json::array();
	for (int i = 0; i < arySmartDev.size(); i++)
	{
		ioDev* pdev = arySmartDev[i];
		string tag = pdev->m_strTagBind;

		json de;
		de["tag"] = tag;
		de["online"] = pdev->m_bOnline;
	
		list.push_back(de);
	}
	return list.dump(4);
}

string rpcHandler::renameItem(string orgName, json& renameMap) {
	string desName = orgName;
	if (renameMap[orgName].is_string()) {
		desName = renameMap[orgName];
	}
	return desName;
}

void rpcHandler::toMoAttr(Mo_Attr_Params& params, OBJ* pMo, nlohmann::ordered_json& attrInfo,RPC_SESSION session) {
	string sysTag = pMo->getTag("",session.language);
	string queryTag = sysTag;
	queryTag = TAG::trimRoot(sysTag, params.tagSel.m_rootTag);
	attrInfo[renameItem("位号", params.renameMap)] = queryTag;

	//自定义监测对象类型，都判断一下是否是智能设备，也就是和ioDev绑定
	if (pMo->isCustomMo()) {
		ioDev* piod = ioSrv.getIODevByTag(sysTag);
		if (piod)
		{
			attrInfo[renameItem("在线", params.renameMap)] = piod->m_bOnline;
		}
		else
		{
			attrInfo[renameItem("在线", params.renameMap)] = pMo->m_bOnline;
		}
	}


	vector<MP*> aryMps;
	if (params.bSelAttr) {
		params.attrSel.init(params.jAttrSel, sysTag);
		pMo->getMpByTagSelector(aryMps, params.attrSel);
	}
	else {
		pMo->GetAttriMp(aryMps);
	}


	for (int j = 0; j < aryMps.size(); j++)
	{
		MP* pmp = aryMps[j];
		json jVal;
		if (params.valFmt == "val") {
			jVal = pmp->m_curVal;
		}
		else if (params.valFmt == "valStr") {
			jVal = pmp->getValDesc(false);
		}
		else if (params.valFmt == "valStr-unit") {
			jVal = pmp->getValDesc(true);
		}



		if (params.columeLabel == "name") {
			attrInfo[renameItem(pmp->m_name, params.renameMap)] = jVal;
		}
		else {
			attrInfo[renameItem(pmp->getTag(sysTag,session.language), params.renameMap)] = jVal;
		}
	}
}

void rpcHandler::rpc_moList2Attrlist(Mo_Attr_Params& params,vector<OBJ*> moList,RPC_RESP& resp, RPC_SESSION session)
{
	string strList = "[";
	for (int i = 0; i < moList.size(); i++)
	{
		OBJ* pMo = moList[i];
		nlohmann::ordered_json oneData;
		toMoAttr(params,pMo,oneData,session);
		if (strList != "[")
			strList += ",";
		strList += oneData.dump(); //此处json对象内的字段顺序按照监测点配置的顺序来排列，因此先序列化再拼接字符串
	}
	strList += "]";
	resp.result = strList;
}


void rpcHandler::rpc_getMoAttr_list(json params, RPC_RESP& resp,RPC_SESSION session)
{
	Mo_Attr_Params attrParam;

	if (!params.contains("tag")) //获取子树
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
		return;
	}

	//支持中文直接输入moType;
	if (params["type"].is_string()) {
		attrParam.moType = params["type"].get<string>();
		//str::hanZi2Pinyin(attrParam.moType, attrParam.moType);
	}

	//是否进行列字段重命名
	if (params["rename"].is_object()) {
		attrParam.renameMap = params["rename"];
	}

	//列标签使用位号还是名称
	if (params["columeLabel"].is_string()) {
		attrParam.columeLabel = params["columeLabel"];
	}

	//值格式。 val, valStr, valStr-unit
	if (params["valFmt"].is_string()) {
		attrParam.valFmt = params["valFmt"];
	}

	//查询根
	attrParam.rootTag = session.org;
	if (params["rootTag"] != nullptr) 
	{
		string userQueryRootTag = params["rootTag"].get<string>();
		attrParam.rootTag = TAG::addRoot(userQueryRootTag, attrParam.rootTag);
	}


	//根据位号选择器选择对象列表
	json jTag = params["tag"];
	vector<string> tags;
	if (jTag.is_string()) {
		tags.push_back(jTag.get<string>());
	}
	else if (jTag.is_array()) {
		for (auto& i : jTag) {
			tags.push_back(i.get<string>());
		}
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param tag format error");
		return;
	}

	attrParam.tagSel.init(tags, attrParam.rootTag);
	attrParam.tagSel.type = attrParam.moType;
	attrParam.tagSel.language = session.language;
	vector<OBJ*> moList;
	prj.getObjByTagSelector(moList, attrParam.tagSel);


	//使用属性选择器选择列
	//attr相当于是指定对象内部的位号选择器
	if (params.contains("attr")) {
		attrParam.jAttrSel = params["attr"];
		attrParam.bSelAttr = true;
	}


	string mode = "list";
	if (params.contains("mode")) {
		mode = params["mode"];
	}

	if (mode == "list") {
		if (attrParam.tagSel.singleSelMode()) {
			nlohmann::ordered_json moAttr;
			if (moList.size() == 1) {
				toMoAttr(attrParam, moList[0], moAttr,session);
			}
			resp.result = moAttr.dump();
		}
		else {
			rpc_moList2Attrlist(attrParam, moList, resp, session);
		}
	}
	else
		rpc_moList2table(attrParam, moList, resp, session);
}


//返回1个具有 header,body,tag3个字段的结果
//header根据对象模版生成，位号为所有的mp，递归平铺
void rpcHandler::rpc_moList2table(Mo_Attr_Params& params, vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION session)
{
	json jTable = json::object();
	json jTableHead = json::array();
	json jTableBody = json::array();
	json jColTag = json::array();


	//根据对象模版生成列模版
	map<string, OBJ_TEMPLATE*>::iterator it = prj.m_mapObjTempalte.find(params.moType);

	bool bStandardObj = false;
	//标准化对象，显示模板列
	if (it != prj.m_mapObjTempalte.end()) {
		bStandardObj = true;
		OBJ_TEMPLATE* ot = it->second;
		vector<MP*> mps;
		ot->obj.GetAttriMp(mps);
		string parentTag = ot->obj.getTag("",session.language);
		if (mps.size() > 0) {
			if (session.language == "en") {
				jTableHead.push_back("Tag");
			}
			else
				jTableHead.push_back("位号");
			jColTag.push_back(nullptr);

			bool mpNameTheSame = false;  //所有监控点的名称全部相同，则取父节点作为列名称。一般在编辑对象树时，会有这种用法，例如 1楼.温度  2楼.温度  3楼.温度
			map<string, string> mpNames;
			for (int j = 0; j < mps.size(); j++)
			{
				MP* pmp = mps[j];
				mpNames[pmp->getName(session.language)] = pmp->getName(session.language);
			}
			if (mpNames.size() == 1) {
				mpNameTheSame = true;
			}


			for (int j = 0; j < mps.size(); j++)
			{
				MP* pmp = mps[j];
				string tag = pmp->getTag(parentTag,session.language);
				jColTag.push_back(tag);
				if (params.columeLabel == "tag") {
					jTableHead.push_back(tag);
				}
				else {
					if (!mpNameTheSame) {
						jTableHead.push_back(pmp->getName(session.language));
					}
					else {
						if(pmp->m_pParentMO)
							jTableHead.push_back(pmp->m_pParentMO->getName(session.language));
					}
				}
			}
			if (session.language == "en") {
				jTableHead.push_back("Online");
			}
			else
				jTableHead.push_back("在线");

			if (session.language == "en")
				jTableHead.push_back("UpdateTime");
			else
				jTableHead.push_back("更新时间");
			jColTag.push_back(nullptr);
			jColTag.push_back(nullptr);
			jTable["header"] = jTableHead;
			jTable["tag"] = jColTag;
		}

		for (int i = 0; i < moList.size(); i++)
		{
			OBJ* pMo = moList[i];
			string moTag = pMo->getTag("",session.language);
			string tag = moTag;

			//过滤用户权限
			if (session.user != "")
			{
				if (!userMng.checkTagPermission(session.user, tag))
					continue;
			}

			tag = TAG::trimRoot(tag, params.tagSel.m_rootTag);


			//表头与列位号，如果没有模版，根据第一个设备生成
			if (jTableHead.size() == 0)
			{
				//下属所有监控点列表
				vector<MP*> childMps;
				pMo->GetAttriMp(childMps);

				if (session.language == "en")
					jTableHead.push_back("Tag");
				else
					jTableHead.push_back("位号");
				jColTag.push_back(nullptr);
				for (int j = 0; j < childMps.size(); j++)
				{
					MP* pmp = childMps[j];
					string tag = pmp->getTag(moTag,session.language);
					jColTag.push_back(tag);
					if (params.columeLabel == "tag") {
						jTableHead.push_back(tag);
					}
					else {
						jTableHead.push_back(pmp->getName(session.language));
					}
				}
				if (session.language == "en")
					jTableHead.push_back("Online");
				else
					jTableHead.push_back("在线");
				if (session.language == "en")
					jTableHead.push_back("UpdateTime");
				else
					jTableHead.push_back("更新时间");
				jColTag.push_back(nullptr);
				jColTag.push_back(nullptr);
				jTable["header"] = jTableHead;
				jTable["tag"] = jColTag;
			}


			//数据行
			json jTableRow;
			jTableRow.push_back(tag);
			for (int j = 1; j < jColTag.size() - 2; j++)
			{
				string tag = jColTag[j];
				tag = TAG::addRoot(tag, moTag);
				MP* pmp = pMo->GetMPByTag(tag, session.language);
				if (pmp) {
					if (params.valFmt == "valStr") {
						jTableRow.push_back(pmp->getValDesc(false));
					}
					else if (params.valFmt == "valStr-unit") {
						jTableRow.push_back(pmp->getValDesc(true));
					}
					else// (valFmt == "val") 
					{
						jTableRow.push_back(pmp->m_curVal);
					}
				}
				else {
					jTableRow.push_back("-");
				}
			}
			jTableRow.push_back(pMo->m_bOnline);
			jTableRow.push_back(pMo->getUpdateTimeDesc());
			jTableBody.push_back(jTableRow);
		}
		jTable["body"] = jTableBody;
		jTable["standardObj"] = bStandardObj;
	}
	else {
		jTableHead.push_back("在线");
		jTableHead.push_back("位号");
		jTableHead.push_back("点位信息");
		jTableHead.push_back("报警状态");
		jTable["header"] = jTableHead;
		for (int i = 0; i < moList.size(); i++)
		{
			OBJ* pMo = moList[i];
			string moTag = pMo->getTag("",session.language);
			string tag = moTag;

			//过滤用户权限
			if (session.user != "")
			{
				if (!userMng.checkTagPermission(session.user, tag))
					continue;
			}

			tag = TAG::trimRoot(tag, params.tagSel.m_rootTag);

			//数据行
			json jTableRow;
			jTableRow.push_back(pMo->m_bOnline);
			jTableRow.push_back(tag);
			jTableRow.push_back(pMo->getChildObjStatis());
			jTableRow.push_back(pMo->m_jAlarmStatus);
			jTableBody.push_back(jTableRow);
		}
		jTable["body"] = jTableBody;
		jTable["standardObj"] = bStandardObj;
	}

	resp.result = jTable.dump();
}

string rpcHandler::rpc_getMpStatus(json params, string& error, RPC_SESSION session, bool bValOnly)
{
	bool getStatus = true;
	bool getConf = true;
	bool getStatusDesc = false;
	bool getUnit = true;

	if (params.contains("getConf"))
	{
		getConf = params["getConf"].get<bool>();
	}
	if (params.contains("getStatus"))
		getStatus = params["getStatus"].get<bool>();
	if (params.contains("getUnit"))
		getUnit = params["getUnit"].get<bool>();


	//获取位号查询参数
	json jTagQuerier = params["tag"];


	//获取查询根
	string rootTag = "";
	if (params["rootTag"] != nullptr)
		rootTag = params["rootTag"].get<string>();
	rootTag = TAG::addRoot(rootTag, session.org);

	string mode = "array";
	if(params["mode"]!=nullptr)
	 	mode = params["mode"].get<string>();

	if (params.contains("getStatusDesc")) {
		if(params["getStatusDesc"].get<bool>() == true)
			getStatusDesc = true;
	}


	json rtList = json::array();
	json rtMap = json::object();


	if(mode=="tree")
	{
		json j = prj.getRT();
		string result = j.dump(4);
		return result;
	}
	else
	{
		TAG_SELECTOR tagSel;
		tagSel.init(jTagQuerier, rootTag);
		vector<MP*> mpList;
		prj.getMpByTagSelector(mpList,tagSel);
		for (int i=0;i<mpList.size();i++)
		{
			MP* pmp = mpList[i];
			string tag = pmp->getTag("",session.language);

			//if (session.user != "")
			//{
			//	if (!userMng.checkTagPermission(session.user, tag))
			//		continue;
			//}

			if (rootTag != "")
			{
				if (tag.find(rootTag)  != 0)
					continue;
			}


			OBJ_QUERIER q;
			//以下两句是基于树结构的查询，应当是不需要的，以后重构
			q.getChild = true;
			q.getMp = true;
			q.getConf = getConf;
			q.getStatusDesc = getStatusDesc;
			q.getStatus = getStatus;
			q.rootTag = rootTag;
			q.getUnit = getUnit;
			json j;
			if (pmp->toJson(j, q, nullptr,session.user))
				rtList.push_back(j);
		}	
						
		string result;
		if (mode == "array")
		{
			result = rtList.dump(4);
		}
		else if (mode == "map")
		{
			for (int i = 0; i < rtList.size(); i++)
			{
				json& de = rtList[i];
				rtMap[de["tag"].get<string>()] = de;
			}
			result = rtMap.dump(4);
		}
		else
		{
			result = rtList.dump(4);
		}
			
		return result;
	}
}


string rpcHandler::rpc_getconf(json params, string& error)
{
	string type = "";
	if(params.find("type") != params.end())
		 type = params["type"].get<string>();

	if (type == "file")
	{
		return "";
	}
	else if (type == "file-list")
	{
		string p = "";
		if (params.find("path") != params.end())
			p = params["path"].get<string>();
		if (p != "")
		{
			string conf = "";
			p = tds->conf->confPath + "/" + p;
			fs::normalizationPath(p);
			vector<string> fl;
			fs::getFileList(fl,p);
			json j = fl;
			return j.dump();
		}
		return string();
	}
	return "";
}

string rpcHandler::rpc_setconf(json params, string& error)
{
	return "";
}

void rpcHandler::rpc_getconffile(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string p = "";
	if (!params["path"].is_string())
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param path error");
		return;
	}

	p = params["path"].get<string>();
	if (p != "")
	{
		string conf = "";
		p = tds->conf->confPath + "/" + p;
		fs::normalizationPath(p);
		fs::readFile(p, conf);
		json j;
		j["path"] = p;
		j["data"] = conf;
		resp.result = j.dump();
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "path not specified");
	}
}

void rpcHandler::rpc_setconffile(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string path = "";
	if (!params["path"].is_string())
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param path error");
		return;
	}

	if (!params["data"].is_string())
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param data error");
		return;
	}

	path = params["path"].get<string>();
	if (path != "")
	{
		string conf = params["data"].get<string>();
		path = tds->conf->confPath + "/" + path;
		fs::createFolderOfPath(path);
		fs::writeFile(path, conf);
		resp.result = RPC_OK;
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "path not specified");
	}
}

json rpcHandler::rpc_getStreamUrl(MP* pmp,string tag, bool isHttps, string localIP,int LocalPort)
{
	json j;
	//视频外部播放地址模式。没有进行码流中转，直接返回播放即可
	if (pmp->m_valType == VAL_TYPE::video && !pmp->m_bServeStream) {
		if (prj.m_mapEzvizAccess.find(pmp->m_serialNo) != prj.m_mapEzvizAccess.end()) {
			EZVIZ_ACCESS_INFO& info = prj.m_mapEzvizAccess[pmp->m_serialNo];
			j["flv"] = info.flvUrl;
			j["ezopen"] = info.ezopenUrl;
		}
		else {
			if (pmp->m_mediaSrcType.find("flv") != string::npos) {
				j["flv"] = pmp->m_mediaUrl;
			}
			if (pmp->m_mediaSrcType.find("rtsp") != string::npos) {
				j["rtsp"] = pmp->m_mediaUrl;
			}

		}
		return j;
	}


	string ip;
	int port = LocalPort;
	bool https = false;


	//重定向到子服务
	CHILD_TDS_INFO childTdsInfo;
	bool isChildTds = ioSrv.getOwnerChildTdsInfo(tag,childTdsInfo);
	OBJ* pObjChildTds = pmp->getOwnerChildTds();
	if (!pObjChildTds) {
		isChildTds = false;
	}

	//string tagPinyin;
	//str::hanZi2Pinyin(tag, tagPinyin);
	string urlProto;
	string wsProto;
	if (isHttps) {
		urlProto = "https://";
		wsProto = "wss://";
	}
	else {
		urlProto = "http://";
		wsProto = "ws://";
	}


	//https://github.com/zlmediakit/ZLMediaKit/wiki/%E6%92%AD%E6%94%BEurl%E8%A7%84%E5%88%99
	//zlmediakit的hls模式暂时不支持中文，因此此处转成拼音
	//本地的转发，将tds的请求转发到zlmediakit
	if (!isChildTds) {
		if (pmp->m_valType == VAL_TYPE::video) {
			//localIP是本次连接暴露给对方的ip地址。如果服务在内网中，那么此处是公网地址，需要在路由器做好端口映射
			if (tds->conf->mediaSrvIP != "" && tds->conf->mediaSrvIP != "localhost" && tds->conf->mediaSrvIP != "127.0.0.1") {
				ip = tds->conf->mediaSrvIP;
			}
			else {
				ip = localIP;
			}
			if (isHttps) {
				port = tds->conf->getInt("httpsMediaPort", 668);
			}
			else {
				port = tds->conf->getInt("httpMediaPort", 669);
			}

			if (pmp->m_mediaSrcType == "file") {
				string url = str::trimPrefix(pmp->m_mediaUrl, "/");
				url = str::trimSuffix(url, ".mp4");
				j["flv"] = urlProto + ip + ":" + str::fromInt(port) + "/record/" + url + ".mp4.live.flv";
				j["rtsp"] = "rtsp://" + ip + "/record/" + url + ".mp4";
				j["hls"] = "";
				j["rtc"] = "";
			}
			else {
				j["flv"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".live.flv";
				j["hls"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + "/hls.m3u8";
				j["rtc"] = urlProto + ip + ":" + str::fromInt(port) + "/index/api/webrtc?app=stream&stream=" + tag + "&type=play";
				j["rtsp"] = "rtsp://" + ip + "/stream/" + tag;
			}
		}
		else
		{
			if (isHttps) {
				port = tds->conf->httpsPort;
			}
			else {
				port = tds->conf->httpPort;
			}
			
			j["de"] = wsProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".de";
		}
	}
	//访问子服务数据流
	else {
		//直连子服务
		if (pObjChildTds->m_streamAccess == "direct") {
			//子服务直接端口映射访问模式
			//此处先发送到子服务的数据服务端口，让子服务再做一次重定向，使得子服务再收到该请求时可以启动码流。
			tag = TAG::trimRoot(tag, childTdsInfo.tag);
			ip = childTdsInfo.ip;
			if (pmp->m_valType == VAL_TYPE::video) {
				if (isHttps) {
					port = childTdsInfo.httpsPort;
				}
				else {
					port = childTdsInfo.httpPort;
				}
				j["flv"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".flv";
				j["rtsp"] = urlProto + ip + "/stream/" + tag ;
				j["rtc"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".rtc";
				j["hls"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".hls";
			}
			else
			{
				if (isHttps) {
					port = childTdsInfo.httpsPort;
				}
				else {
					port = childTdsInfo.httpPort;
				}
				j["de"] = wsProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".de";
			}
		}
		//码流从上级服务中转
		else// (pmp->m_streamAccess == "relay") 
		{
			if (pmp->m_valType == VAL_TYPE::video) {
				//localIP是本次连接暴露给对方的ip地址。如果服务在内网中，那么此处是公网地址，需要在路由器做好端口映射
				if (tds->conf->mediaSrvIP != "" && tds->conf->mediaSrvIP != "localhost" && tds->conf->mediaSrvIP != "127.0.0.1") {
					ip = tds->conf->mediaSrvIP;
				}
				else {
					ip = localIP;
				}
				if (isHttps) {
					port = tds->conf->getInt("httpsMediaPort", 668);
				}
				else {
					port = tds->conf->getInt("httpMediaPort", 669);
				}
				j["flv"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".flv";
				j["rtsp"] = urlProto + ip + "/stream/" + tag;
				j["rtc"] = urlProto + ip + ":" + str::fromInt(port) + "/index/api/webrtc?app=stream&stream=" + tag + "&type=play";
				j["hls"] = urlProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".hls";
			}
			else
			{
				if (isHttps) {
					port = childTdsInfo.httpsPort;
				}
				else {
					port = childTdsInfo.httpPort;
				}
				j["de"] = wsProto + ip + ":" + str::fromInt(port) + "/stream/" + tag + ".de";
			}
		}
	}

	j["isChildTds"] = isChildTds;

	return j;
}

string rpcHandler::rpc_heartbeat(json params, string& error , RPC_SESSION session)
{
	if (params.is_object())
	{
		if(params["clientName"] != nullptr)
			session.name = params["clientName"];
	}
	return "\"pong\"";
}


string rpcHandler::rpc_openCom(json params, string& error)
{
	ioDev* pDev = NULL;
	string portNum = params["portNum"].get<string>();
	pDev = ioSrv.getIODev(portNum);
	if (pDev)
	{
		if (pDev->connect(params))
		{
			pDev->run();
			return "\"ok\"";
		}
		else
		{
			json jError = "fail," + pDev->m_strErrorInfo;
			error = jError.dump();
		}
	}
	else
	{
		json jError = "fail,portNum not found";
		error = jError.dump();
	}
	return "";
}

void rpcHandler::rpc_getDevStatis(json params, RPC_RESP& resp, RPC_SESSION session)
{
	string rootTag;
	vector<ioDev*> filterRlt;
	DEV_STATIS ds = ioSrv.getDevStatis(rootTag, filterRlt);
	resp.result = ds.toJson().dump();
}

void rpcHandler::rpc_getDev(json params, RPC_RESP& resp, RPC_SESSION session)
{
	json j;
	if (!params.contains("rootTag"))
		params["rootTag"] = "";

	//用户rootTag转系统rootTag
	string rootTag = params["rootTag"].get<string>();
	rootTag = TAG::addRoot(rootTag, session.org);
	params["rootTag"] = rootTag;

	ioDev* p = nullptr;
	if (params.contains("ioAddr")) {
		string ioAddr = params["ioAddr"];
		p = ioSrv.getIODev(ioAddr);
		if (p) {
			DEV_QUERIER query = p->parseQueryOpt(params);
			p->toJson(j, query);
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device with specified ioAddr not found");
		}
	}
	else if(params.contains("tag")){
		string tag = params["tag"];
		tag = TAG::addRoot(tag, session.org);
		p = ioSrv.getIODevByTag(tag);
		if (p) {
			DEV_QUERIER query = p->parseQueryOpt(params);
			p->toJson(j, query);
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device with specified bindTag not found");
		}
	}
	else {
		ioSrv.toJson(j, params);
	};

	resp.result = j.dump();
}

void rpcHandler::rpc_getChanStatus(json params, RPC_RESP& resp)
{
	json list;
	ioSrv.getChanStatus(list);
	resp.result = list.dump(4);
}


void rpcHandler::rpc_getChanVal(json params, RPC_RESP& resp)
{
	return;
	json list;
	string ioAddrSelector = params["ioAddr"].get<string>();
	if(ioAddrSelector == "*")
		ioSrv.getChanStatus(list);
	else if (ioAddrSelector.find("/*") != string::npos) //某个设备下面的所有通道
	{
		string devIOAddr = str::trim(ioAddrSelector, "/*");
		ioDev* p = ioSrv.getIODev(devIOAddr);
		if (p)
		{
			p->getChanStatus(list);
		}
		else
		{
			return;
		}
	}


	json valList = json::object();
	string fmt = "";
	if (params["fmt"] != nullptr)
		fmt = params["fmt"].get<string>();
	for (int i = 0; i < list.size(); i++)
	{
		json& j = list[i];
		if (fmt == "")
		{
			if (j["val"] == nullptr)
				valList[j["ioAddr"].get<string>()] = "?";
			else
				valList[j["ioAddr"].get<string>()] = j["val"];
		}
		else
		{
			string valStr = fmt;
			string val;
			if (j["val"] == nullptr)
				val = "?";
			else
				val = j["val"].dump();

			string time = j["time"].get<string>();

			valStr = str::replace(valStr, "val", val);
			valStr = str::replace(valStr, "time", time);

			valList[j["ioAddr"].get<string>()] = valStr;
		}
		
	}
	resp.result = valList.dump(4);
}


string rpcHandler::rpc_io_scanChannel(json params, string& error)
{
	if (params["ioAddr"] == nullptr)
	{
		error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "必须指定ioAddr字段");
		return "";
	}

	string ioAddr = params["ioAddr"];
	ioDev* pDev = ioSrv.getIODev(ioAddr);
	if (!pDev)
	{
		error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device not found");
		return "";
	}

	
	json chanList;
	if (pDev->pIOSession == nullptr)
	{
		json jError = {
			{"code", -32603},
			{"message" , "设备不在线，请确认IQ60的连接配置，并在主动上送数据"}
		};
		error = jError.dump();
	}
	else if (pDev->scanChannel(chanList))
	{
		//比对p->m_vecChild是否已经存在,只发不存在的给前端
		for (int i = 0; i < chanList.size(); i++)
		{
			json jsubPkt = chanList.at(i);
			string straddr = jsubPkt["addr"];

			for (auto j : pDev->m_vecChildDev)
			{
				ioChannel* pioChannel = (ioChannel*)j;
				if (pioChannel->m_devAddr == straddr)
				{
					chanList.erase(i);
					i--;
					break;
				}
			}
		}
		//

		json result;
		result["ioAddr"] = pDev->getIOAddrStr();
		result["channels"] = chanList;


		//新建空闲设备通道
		for (int i = 0; i < chanList.size(); i++)
		{
			json jC = chanList[i];
			ioChannel* pC = new ioChannel;
			pC->m_dispositionMode = DEV_DISPOSITION_MODE::spare;
			pC->loadConf(jC);
			pDev->addChild(pC);
		}

		return result.dump();
	}
	else
	{
		json jError = {
			{"code", -32603},
			{"message" , "设备响应超时"}
		};
		error = jError.dump();
	}

	return "";
}

#ifdef ENABLE_GENICAM
string rpcHandler::rpc_setStream(json params,string& error)
{
	string streamId = params["streamId"].get<string>();
	
	streamSrvNode* ssn = streamSrv.getSrvNode(streamId);
	if (ssn && ssn->m_streamPusher)
	{
		int fr = params["frameRate"].get<int>();
		ssn->m_streamPusher->m_streamInfoConf.frameRate = fr;
		return "\"ok\"";
	}

	error = makeRPCError(TEC_STREAM_ID_NOT_FOUND, "stream id not found");
	return "";
}


string rpcHandler::rpc_getStreamInfo(json params,string& error)
{
	string streamId;
	if (params.find("streamId") != params.end())
		streamId = params["streamId"].get<string>();
	if (streamId == "")
	{
		error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing,"param missing,tag is not specified");
		return "";
	}

	streamSrvNode* pssn = streamSrv.getSrvNode(streamId);
	if(pssn->m_streamPusher == NULL)
	{
		error = makeRPCError(RPC_ERROR_CODE::TEC_NO_STREAM_SRC, "no stream src of this tag");
		return "";
	}

	if (pssn->m_streamPusher->m_streamInfo.w == 0 || pssn->m_streamPusher->m_streamInfo.h == 0)
	{
		error = makeRPCError(RPC_ERROR_CODE::TEC_VIDEO_PARAM_NOT_VALID, "video param is not valid");
		return "";
	}

	json jSi;
	jSi["w"] = pssn->m_streamPusher->m_streamInfo.w;
	jSi["h"] = pssn->m_streamPusher->m_streamInfo.h;
	jSi["pixelFmt"] = pssn->m_streamPusher->m_streamInfo.pixelFmt;
	if (pssn->m_streamPusher->m_pusherType == "ioDev")
	{
		ioDev* p = pssn->m_streamPusher->m_ioDev;
		json jIoDev = json::object();
		jIoDev["type"] = p->m_devType;
		jIoDev["ioAddr"] = p->getIOAddrStr();
		jSi["ioDev"] = jIoDev;
	}
	
	return jSi.dump();
}
#endif

string rpcHandler::rpc_com_list(json params, string& error)
{
	vector<ioDev*> ary = ioSrv.getChildren(DEV_TYPE_local_serial);
	json result;
	for (auto& i : ary)
	{
		ioDev* pls  =  (ioDev*)i;
		json jComInfo;
		jComInfo["portNum"] = pls->getIOAddrStr();
		jComInfo["desc"] = pls->m_devTypeLabel;
		jComInfo["online"] = pls->m_bOnline;
		jComInfo["connected"] = pls->isConnected();
		jComInfo["inUse"] = pls->m_bInUse;
		//jComInfo["callbackUser"] = (DWORD)pls->m_pCallbackUser;
		result.push_back(jComInfo);
	}
	return result.dump();
}


string rpcHandler::rpc_closeCom(json params, string& error)
{
	string portNum = params["portNum"].get<string>();
	ioDev* pCom = ioSrv.getIODev(portNum);
	if (pCom)
	{
		pCom->stop();
		json j = "ok";
		return j.dump();
	}
	
	json j = "portNum " + portNum + " is not opened";
	return j.dump();
}

void rpcHandler::rpc_onObjOnline(json params,RPC_SESSION session) {
	string tag = params["tag"];
	OBJ* p = prj.queryObj(tag, session.language);
	if (p) {
		if (p->m_bOnline == false) {
			p->m_bOnline = true;
			LOG("[对象上线  ]位号:%s", tag.c_str());
			rpcSrv.notify("objOnline", params);
		}
	}
}

void rpcHandler::rpc_onObjOffline(json params, RPC_SESSION session) {
	string tag = params["tag"];
	OBJ* p = prj.queryObj(tag, session.language);
	if (p) {
		if (p->m_bOnline) {
			p->m_bOnline = false;
			LOG("[对象掉线  ]位号:%s", tag.c_str());
			rpcSrv.notify("objOffline", params);
		}
	
		if (p->m_bChildTds) { //设置所有子对象掉线
			p->recursiveSetOffline();
		}
	}
}


void rpcHandler::notify(string method, json params, bool specialNotify,std::shared_ptr<TDS_SESSION> orgSession)
{
	string notify = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params.dump() + "}\n\n";

	WebServer::notifyAllSrvAllWs(notify);
	reverseInterface.sendToAllSessions(notify, specialNotify);
}


void rpcHandler::statisCall(string method) {
	m_csCallStatis.lock();
	std::map<string, int>::iterator iter = m_mapCallStatis.find(method);
	if (iter == m_mapCallStatis.end()) {
		m_mapCallStatis[method] = 1;
	}
	else {
		iter->second++;
	}
	m_csCallStatis.unlock();
}


bool haveNode(string link, string node)
{
	vector<string> nodes;
	str::split(nodes, link, ".");
	for (int i = 0; i < nodes.size(); i++)
	{
		if (nodes.at(i) == node)
			return true;
	}

	return false;
}
