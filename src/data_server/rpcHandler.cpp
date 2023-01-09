#include "pch.h"
#include "rpcHandler.h"
#include "prj.h"
#include "as.h"
#include "mp.h"
#include "ds.h"
#include "logger.h"
#include "db.h"
#include <json.hpp>
#include "amo.h"
#include "ioSrv.h"
#include "tcpClt.h"
#include "db.h"
#include <UrlMon.h>
#include "logger.h"
#include "ioChan.h"
#include "ioDev_genicam.h"
#include "streamServer.h"
#include "users/userMng.h"
#include "logServer/logServer.h"
#include "xiaot/scriptManager.h"
#include "audioPlayer.h"
#include "base64.h"
#include "ffmpegCmd.h"
#include "masterDs.h"
#include "ioDev/ioDev_visca.h"
#include "httplib.h"

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
	url = charCodec::ansi2Utf8(url);
	//HRESULT hr = URLDownloadToFile(NULL, url.c_str(), file_save_path.c_str(), 0, NULL);
	//return hr == S_OK;
	return false;
}

rpcHandler::rpcHandler()
{
	m_pluginHandler = NULL;
}

rpcHandler::~rpcHandler()
{

}


bool rpcHandler::init()
{
	return true;
}





string rpcHandler::ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession)
{
	//使用正则搜寻 ${XXX}
	//"${src_ip}" 替换成 pSession->ip
	string str = strIn;
	if (str.find("$srcIp$") != str.npos) {
		string ip = pSession->ip;
		string::size_type pos = pSession->ip.find(":");
		if (std::string::npos != pos) {
			ip = pSession->ip.substr(0, pos);
		}
		str = str::replace(str, "$src_ip$", ip);
		}

	str = str::replace(str, "$dbPath$", db.m_path);
	str = str::replace(str, "$confPath$", tds->conf->confPath);

	return str;
}


void selectFolderDlgThread(json params)
{
	vector<string> paths = fs::fileDlg(false,true,true);
	if (paths.size() > 0)
	{
		json jn = json::object();

		if (paths.size() > 1)
		{
			json jPs = json::array();
			for (int i = 0; i < paths.size(); i++)
			{
				string p = paths[i];
				jPs.push_back(p);
			}
			jn["path"] = jPs;
			rpcSrv.notify("fs.selectFolderDlg", jn);
		}
		else if (paths.size() == 1)
		{
			jn["path"] = paths[0];
			rpcSrv.notify("fs.selectFolderDlg", jn);
		}
	}
}


void openFileDlgThread(json params)
{
	string filter;
	if (params["filter"] != nullptr)
		filter = params["filter"].get<string>();
	string title;
	if (params["title"] != nullptr)
		title = params["title"].get<string>();
	//filter = filter
	vector<string> paths  = fs::fileDlg(false,true,false,(char*)filter.c_str(), (char*)title.c_str());
	if (paths.size()>0)
	{
		json jn = json::object();

		if (paths.size() > 1)
		{
			json jPs = json::array();
			for (int i = 0; i < paths.size(); i++)
			{
				string p = paths[i];
				jPs.push_back(p);
			}
			jn["path"] = jPs;
			rpcSrv.notify("fs.openFileDlg", jn);
		}
		else if(paths.size() == 1)
		{
			jn["path"] = paths[0];
			rpcSrv.notify("fs.openFileDlg", jn);
		}
	}
}

void saveFileDlgThread(json params)
{
	string filter;
	if (params["filter"] != nullptr)
		filter = params["filter"].get<string>();
	string title;
	if(params["title"]!=nullptr)
		title = params["title"].get<string>();
	string fileName;
	if (params["fileName"] != nullptr)
		fileName = params["fileName"].get<string>();
	vector<string> paths = fs::fileDlg(false,false, false, (char*)filter.c_str(), (char*)title.c_str(),(char*)fileName.c_str());
	if (paths.size() > 0)
	{
		json jn = json::object();

		if (paths.size() > 1)
		{
			json jPs = json::array();
			for (int i = 0; i < paths.size(); i++)
			{
				string p = paths[i];
				jPs.push_back(p);
			}
			jn["path"] = jPs;
			rpcSrv.notify("fs.saveFileDlg", jn);
		}
		else if (paths.size() == 1)
		{
			jn["path"] = paths[0];
			rpcSrv.notify("fs.saveFileDlg", jn);
		}
	}
}


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
			string encode = params["encode"].get<string>();
			if (encode == "base64") {
				unsigned char* out = new unsigned char[d.length()];
				int len = base64_decode(d.c_str(), d.length(), out);
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
				if (fs::writeFile(p, d))
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
	else if (method == "fs.getCurDir")
	{
		WCHAR buff[300] = { 0 };
		GetCurrentDirectoryW(300, buff);
		wstring s = buff;
		json j = charCodec::utf16toUtf8(s);
		result = j.dump();
	}
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
		string path = params["path"];
		vector<string> fileList,folderList;
		fs::getFileList(fileList, path);
		fs::getFolderList(folderList, path);
		json infoList = json::array();
		for (int i = 0; i < folderList.size(); i++) {
			string sFolder = folderList[i];
			fs::FILE_INFO fi;
			fs::getFileInfo(path + "/" + sFolder, fi);
			json j;
			j["name"] = sFolder;
			j["size"] = fi.len;
			j["modifyTime"] = fi.modifyTime;
			j["isFolder"] = true;
			infoList.push_back(j);
		}
		for (int i = 0; i < fileList.size(); i++) {
			string sFile = fileList[i];
			fs::FILE_INFO fi;
			fs::getFileInfo(path + "/" + sFile, fi);
			json j;
			j["name"] = sFile;
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
		std::thread t(openFileDlgThread, params);
		t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.saveFileDlg")
	{
		std::thread t(saveFileDlgThread, params);
		t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.selectFolderDlg")
	{
		std::thread t(selectFolderDlgThread, params);
		t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.openFolder")
	{
		string s = params["path"];
		s = str::replace(s, "/", "\\");
		wstring ws = charCodec::utf8toUtf16(s);
		ShellExecuteW(NULL, L"open", L"explorer.exe", ws.c_str(), NULL, SW_SHOWNORMAL);
		result = "\"ok\"";
	}
	else if (method == "fs.openDEFolder")
	{
		string tag = params["tag"];
		string time = params["time"];
		string path = db.m_path + db.getPath_deFile(tag, timeopt::str2st(time));

		path = str::replace(path, "/", "\\");
		wstring ws = charCodec::utf8toUtf16(path);
		ShellExecuteW(NULL, L"open", L"explorer.exe", ws.c_str(), NULL, SW_SHOWNORMAL);
		result = "\"ok\"";
	}
	else if (method == "ui.maximize")
	{
		SendMessage(tds->uiWnd, WM_SYSCOMMAND, SC_MAXIMIZE, NULL);
		rpcResp.result = "\"ok\"";
	}
	else if (method == "ui.minimize")
	{
		SendMessage(tds->uiWnd, WM_SYSCOMMAND, SC_MINIMIZE, NULL);
		rpcResp.result = "\"ok\"";
		LOG("[debug]ui.minimize");
	}
	else if (method == "ui.close")
	{
		SendMessage(tds->uiWnd, WM_SYSCOMMAND, SC_CLOSE, NULL);
		rpcResp.result = "\"ok\"";
		LOG("[debug]ui.close");
	}
	else
	{
		bHandled = false;
	}

	return bHandled;
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
				method == "openStream" ||
				method == "keepStream" ||
				method == "closeStream")
	{
		string tag, rootTag;
		if (!parseParam_tag(params, rpcResp, session, tag, rootTag))
			return true; 

		OBJ* pObj = prj.queryObj(tag);
		if (!pObj) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "object of specified tag not found");
			return true;
		}

		OBJ* childTds = pObj->getOwnerChildTds();
		if (childTds) {
			if (pMasterDs) {
				string childTdsTag = childTds->getTag();
				tag = TAG::trimRoot(tag, childTdsTag);
				json paramsChild = params;
				paramsChild["tag"] = tag;
				json childRlt, childErr;
				pMasterDs->callChildTds(childTdsTag, method, paramsChild, childRlt, childErr);
				
				if (childRlt != nullptr) {
					json jRlt;
					jRlt["ip"] = pMasterDs->getChildTdsIP(childTdsTag);
					rpcResp.result = jRlt.dump();
				}
				else{
					rpcResp.error = childErr.dump();
				}
			}
		}
		else if (method == "startPanTilt" ||
			method == "stopPanTilt" ||
			method == "startZoom" ||
			method == "stopZoom" ||
			method == "startFocus" ||
			method == "stopFocus") {
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

				if (params.contains("time")) {
					int time = params["time"].get<int>();
					json paramAsynCall;
					paramAsynCall["tag"] = tag;
					tds->callAsyn("stopPanTilt", paramAsynCall.dump(), time);
				}
			}
			else if (method == "stopPanTilt")
			{
				pCam->ptz_stopMove();
			}
			else if (method == "startZoom")
			{
				string dir = params["dir"];
				float speed = 0;
				if (params.contains("speed")) {
					speed = params["speed"].get<float>();
				}
				pCam->ptz_startZoom(dir);

				if (params.contains("time")) {
					int time = params["time"].get<int>();
					json paramAsynCall;
					paramAsynCall["tag"] = tag;
					tds->callAsyn("stopZoom", paramAsynCall.dump(), time);
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
					tds->callAsyn("stopFocus", paramAsynCall.dump(), time);
				}
			}
			else if (method == "stopFocus")
			{
				pCam->ptz_stopFocus();
			}
			rpcResp.result = "\"ok\"";
		}
		else if (method == "openStream") {
			MP* pmp = prj.GetMPByTag(tag);
			if (!pmp) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
			}
			else {
				if (pmp->m_mediaSrcType != "file") {
					if (!pmp->m_bIsStreaming)
					{
						string tag = pmp->getTag();
						zlm_openStream(tag,pmp->m_mediaUrl);
					}
					else {
						LOG("码流已打开");
					}
				}
				
				rpcResp.result = "\"ok\"";
			}
			LOG("打开码流,tag=" + tag);
		}
		else if (method == "keepStream") {
			TIME st;
			timeopt::now(&st);
			ds.m_mapPullerActive[tag] = st;
			rpcResp.result = "\"ok\"";
		}
		else if (method == "closeStream") {
			if (!fs::fileExist(fs::appPath() + "/com/ffmpeg/ffmpeg.exe")) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "component ffmpeg not found");
			}
			else {
				MP* pmp = prj.GetMPByTag(tag);
				if (!pmp) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
				}
				else {
					if (pmp->m_srcPullingFFmpegProcID)
					{
						HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pmp->m_srcPullingFFmpegProcID);
						if (hProcess) {
							TerminateProcess(hProcess, 0);
						}
						pmp->m_bIsStreaming = false;
						pmp->m_srcPullingFFmpegProcID = 0;
					}
					rpcResp.result = "\"ok\"";
				}
			}
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
		if (!params.contains("time"))
		{
			error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param : time");
		}
		else if (method == "db.select")
		{
			db.rpc_db_select(params, rpcResp, session);
		}
		else if (method == "db.count")
		{
			db.rpc_db_count(params, rpcResp, session);
		}
		else if (method == "db.update")
		{
			string tag = params["tag"].get<string>();
			string time = params["time"].get<string>();
			json val = params["val"];
			db.Update(tag, timeopt::str2st(time), val);
			result = "\"ok\"";
		}
		else if (method == "db.delete")
		{
			string tag = params["tag"].get<string>();
			string time = params["time"].get<string>();
			if (db.Delete(tag, timeopt::str2st(time)))
				result = "\"ok\"";
			else
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "data element not found");
		}
		else if (method == "db.insert")
		{
			if (!params.contains("val"))
			{
				error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param : val");
			}
			else
			{
				string tag = params["tag"].get<string>();
				string time = params["time"].get<string>();
				if (!timeopt::isValidTimeStr(time)) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_TIME_SELECTOR_FMT_ERROR, "param time invalid format.");
				}
				else {
					db.Insert(tag, timeopt::str2st(time), params["val"]);
					rpcResp.result = "\"ok\"";
				}
			}
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

bool rpcHandler::handleMethodCall_debugFunc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getIoSessions")
	{
		ioSrv.rpc_getSessionStatus(params,rpcResp,session);
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
			int iSended = pDestSession->send((char*)data.c_str(), data.length());
			if (iSended > 0)
				rpcResp.result = "\"ok\"";
			else
				rpcResp.error = "\"fail\"";
			return true;
		}
	}
	else if (method == "testCrash")
	{
		rpcResp.result = "\"ok\"";


		//程序崩溃
		int i = 13; int j = 0; int m = i / j;
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
	else if (method == "testCall")
	{
	    int timeCost = 5;
		if (params["time"] != nullptr)
		{
			timeCost = params["time"].get<int>();
		}
		Sleep(1000 * timeCost);
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
	if (method == "ioTree" || method == "iotree" || method == "getIOTree" || method == "getDev")
	{
		rpc_getDev(params, rpcResp,session);
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
	else if (method == "closeIOSession")
	{
		string remoteAddr = "";
		if(params.contains("remoteAddr"))
			remoteAddr = params["remoteAddr"].get<string>();
		if (ioSrv.m_tcpSrv_tdsp)
		{
			ioSrv.m_tcpSrv_tdsp->disconnect(remoteAddr);
		}
		if (ioSrv.m_tcpSrv_rtu)
		{
			ioSrv.m_tcpSrv_rtu->disconnect(remoteAddr);
		}
		if (ioSrv.m_tcpSrv_iq60)
		{
			ioSrv.m_tcpSrv_iq60->disconnect(remoteAddr);
		}
	}
	else if (method == "getChanStatus")
	{
		rpc_getChanStatus(params,rpcResp);
	}
	else if (method == "getChanVal")
	{
		rpc_getChanVal(params, rpcResp);
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
	else if (method == "deleteDev")
	{
		ioSrv.rpc_deleteDev(params, rpcResp, session);
	}
	else if (method == "modifyDev")
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
		fs::getFileList(list,fs::appPath() +"/files/firmware",false);
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
		if (params.contains("name")) {
			string name = params["name"];
			if (ioSrv.m_mapChanTempalte.find(name) != ioSrv.m_mapChanTempalte.end()) {
				CHAN_TEMPLATE ct = ioSrv.m_mapChanTempalte[name];
				rpcResp.result = ct.channels.dump();
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_chanTemplateNotFound, "chan template not found");
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param name is not specified");
		}
	}
	else if (method == "setChanTemplate") {
		CHAN_TEMPLATE ct;
		ct.label = params["name"];
		str::hanZi2Pinyin(ct.label, ct.name);
		ct.channels = params["channels"];
		ioSrv.m_mapChanTempalte[ct.name] = ct;
		ioSrv.saveChanTemplate();
		rpcResp.result = "\"ok\"";
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
		p["deviceType"] = "TDS-Edge智能边缘网关";

		result = p.dump();
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
	if (method == "setconf")
	{
		unique_lock<shared_mutex> lock(prj.m_csPrj);
		result = rpc_setconf(params, error);
	}
	else if (method == "getObjTemplate") {
		if (params.contains("type")) {
			string type = params["type"];
			if (prj.m_mapObjTempalte.find(type) != prj.m_mapObjTempalte.end()) {
				OBJ_TEMPLATE ct = prj.m_mapObjTempalte[type];
				rpcResp.result = ct.tplData;
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::OBJ_templateNotFound, "object template not found");
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "param name is not specified");
		}
	}
	else if (method == "setObjTemplate") {
		OBJ_TEMPLATE ct;
		ct.typeLabel = params["typeLabel"];
		str::hanZi2Pinyin(ct.typeLabel, ct.type);
		ct.tplData = params["tplData"];
		prj.saveObjTemplate(ct);
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
				//加载新的树
				project tmpPrj;
				tmpPrj.loadConf(params);
				tmpPrj.loadStatus(&prj);//保留原有的实时数据状态
				prj.clear();
				prj.m_name = tmpPrj.m_name;
				prj.m_parentTag = tmpPrj.m_parentTag;
				prj.m_mapAllMP = tmpPrj.m_mapAllMP;
				prj.m_childObj = tmpPrj.m_childObj;
				for (int i = 0; i < prj.m_childObj.size(); i++) {
					OBJ* p = prj.m_childObj[i];
					p->m_pParentMO = &prj;
				}
				tmpPrj.m_childObj.clear();
				prj.saveConfFile();
				std::map<string, SCRIPT_INFO> expScripts;
				prj.getAllVarExpScript();
				ioSrv.updateTag2IOAddrBinding();
				ioSrv.updateAllChanVal();
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
					prj.setMo(mo, tag);
					prj.saveConfFile();
					result = "\"ok\"";
				}
			}
			else if (params.is_array())
			{
				for (int i = 0; i < params.size(); i++) {
					json& mo = params[i];
					string tag = mo["tag"].get<string>();
					string rootTag = "";
					if (mo.contains("rootTag"))
						rootTag = mo["rootTag"].get<string>();
					tag = TAG::addRoot(tag, rootTag);
					tag = TAG::addRoot(tag, session.org);
					prj.setMo(mo, tag);
				}
				prj.saveConfFile();
				result = "\"ok\"";
			}
		}
	}
	else if (method == "updateTagBinding") {
		for (auto& binding : params) {
			string tag = binding["tag"];
			OBJ* p = prj.queryObj(tag);
			if (p)
				p->m_strIoAddrBind = binding["ioAddr"];
		}
	}
	else
	{
		//对象关联的命令都必须有参数tag
		if (params["tag"] == nullptr) //获取子树
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
			return true;
		}
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		//以下配置使用 mo conf 和 io conf
		if (method == "input")
		{
			if (params.is_array()) {
				for (int i = 0; i < params.size(); i++) {
					json de = params[i];
					rpc_input(params, rpcResp, session);
				}
			}
			else
				rpc_input(params, rpcResp,session);
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
		else if (method == "getMo" || method == "getOrg" || method == "getObj" || method == "getMp")
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
			TAG_SELECTOR tagSel;
			tagSel.init(params["tag"], rootTag);

			string type = "obj";
			if (params["type"] != nullptr)
				type = params["type"].get<string>();
			//将getOrg,getMp,getMo统一转化为getObj
			else if (method == "getOrg") type = "org";
			else if (method == "getMo") type = "mo";
			else if (method == "getMp") {
				params["getMp"] = true;
				type = "mp";
			}
			tagSel.type = type;

			string mode = "array";
			if (params["mode"] != nullptr) {
				mode = params["mode"].get<string>();
			}
			
			vector<OBJ*> objList;
			prj.getObjByTagSelector(objList, tagSel);

			//通配模式，返回一个数组
			if (objList.size()>1) {
				params["rootTag"] = rootTag;
				if (mode == "array") {
					json jRlt = json::array();
					for (int i = 0; i < objList.size(); i++) {
						OBJ* pObj = objList[i];
						json jObj;
						pObj->toJson(jObj, params);
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
						pObj->toJson(jObj, params);
						string tag = jObj["tag"].get<string>();
						tag = str::replace(tag, ".", "_");
						jRlt[tag] = jObj;
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
				pmo->toJson(j, params);
				result = j.dump(4);
			}
			else
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "monitor object of specified tag not found");
			}
		}
		else if (method == "getMoConf")
		{
			if (params["tag"] == nullptr)
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "请求中缺少tag字段");
				return true;
			}

			string tag = params["tag"].get<string>();
			if (session.org != "")
			{
				tag = TAG::addRoot(tag, session.org);
			}

			OBJ* pmo = prj.queryObj(tag);
			if (pmo)
			{
				json j;
				json jOpt;
				jOpt["recursive"] = false;
				pmo->toJson(j, jOpt);
				rpcResp.result = j.dump(4);
			}
			else
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "没有找到位号");
			}
		}
		else if (method == "getMoCustomType" || method == "getMoTypes")
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
					pmo = prj.queryObj(tag);
			}
			else
			{
				pmo = &prj;
			}

			if (pmo != nullptr)
			{
				map<string, json> list = pmo->getChildCustomMoTypeList();
				json jList = json::array();

				if (method == "getMoTypes")
				{
					for (auto& i : list)
					{
						jList.push_back(i.second);
					}
				}
				else
				{
					for (auto& i : list)
					{
						jList.push_back(i.second["label"].get<string>());
					}
				}
				
				result = jList.dump();
			}
			else
			{
				error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "未找到指定位号");
			}
		}
		else if (method == "getmplist")//or getMpList or get_mp_list
		{
			json list;
			prj.getMpList(list);
			result = list.dump(2);
		}
		else if (method == "sum") {
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
			if (success) {
				rlt = dbSum;
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

bool rpcHandler::handleMethodCall_alarmMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	bool bHandled = true;
	if (method == "getAlarmCurrent")
	{
		json jFilter;
		jFilter["rootTag"] = params["rootTag"];
		result = almSrv.rpc_getCurrent(jFilter, session);
	}
	else if (method == "getAlm")
	{
		if (params.contains("status")) {
			string status = params["status"].get<string>();
			json jFilter;
			jFilter["rootTag"] = params["rootTag"];
			if (status == "unRecover") {
				result = almSrv.rpc_getUnRecover(jFilter, session);
			}
			else if (status == "unAck") {
				result = almSrv.rpc_getUnack(jFilter, session);
			}
			else if (status == "unRecover||unAck" || status == "unAck||unRecover") {
				result = almSrv.rpc_getCurrent(jFilter, session);
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "param  status format error");
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "missing param  status");
		}
	}
	else if (method == "getAlarmStatus")
	{
		json jFilter;
		result = almSrv.rpc_getUnRecover(jFilter, session);
	}
	else if (method == "getAlarmUnack")
	{
		json jFilter;
		result = almSrv.rpc_getUnack(jFilter, session);
	}
	else if (method == "getAlarmHistory")
	{
	result = almSrv.rpc_getHistory(params, session);
	}
	else if (method == "addAlarmEvent")
	{
	result = almSrv.rpc_addEvent(params);
	}
	else if (method == "updateAlarmStatus")
	{
		almSrv.rpc_updateStatus(params, rpcResp);
	}
	else if (method == "ackAlarmEvent")
	{
		almSrv.rpc_acknowledge(params,rpcResp, session);
	}
	else if (method == "ackAllAlarmEvent")
	{
		almSrv.rpc_acknowledge(params, rpcResp, session);
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
	if (method == "getUsers")
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
	else if (method == "queryLog")
	{
		rpcResp.result = logSrv.rpc_queryLog(params, session);
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
		MP* pmp = prj.GetMPByTag(tag);
		if (pmp) {
			json rlt = rpc_getStreamUrl(pmp, tag, session.isHttps, session.hostName, session.hostPort);
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
	else {
		bHandled = false;
	}
	return bHandled;
}

bool rpcHandler::handleMethodCall(string method, json params, RPC_RESP& rpcResp, RPC_SESSION session)
{
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

bool rpcHandler::handleChildTdsDispatch(string& strReq, json& jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession)
{
	if (pMasterDs == nullptr)
		return false;

	string method = jReq["method"].get<string>();
	if (jReq.contains("childTds"))
	{
		jReq.erase("user");
		jReq.erase("token");
		pMasterDs->rpc_childTdsDispatch(jReq, rpcResp);
		return true;
	}


	return false;
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
		pSession->ioAddr = strIoAddr;
		pIoDev  = ioSrv.getIODev(strIoAddr);
		if (!pIoDev)
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到指定IO地址的IO设备");
			return true;
		}
	
		pIoDev->handleDevRpcCall(jReq,rpcResp,pSession);
		logTDSPDispatch(method, jReq["params"], *pSession);
		return true;
	}
	else if (jReq.contains("tag")) {
		string tag = jReq["tag"].get<string>();
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
			logTDSPDispatch(method, jReq["params"], *pSession);
			return true;
		}
	}

	return false;
}

void rpcHandler::logTDSPDispatch(string method,json& params,RPC_SESSION& session) {
	if (method == "startRepel") {
		json logParams;
		logParams["object"] = "用户:" + session.user;
		logParams["event"] = "探驱联动开始";
		logParams["org"] = session.org;
		logParams["host"] = session.remoteAddr;
		logParams["detail"] = "设备名称:" + session.tag + ",设备地址:" + session.ioAddr + ",水平角:" + str::fromFloat(params["pan"].get<float>()) + ",俯仰角:" + str::fromFloat(params["tilt"].get<float>());
		logSrv.rpc_addLog(logParams, session);
	}	
	else if (method == "stopRepel") {
		json logParams;
		logParams["object"] = "用户:" + session.user;
		logParams["event"] = "探驱联动结束";
		logParams["org"] = session.org;
		logParams["host"] = session.remoteAddr;
		logParams["detail"] = "设备名称:" + session.tag + ",设备地址:" + session.ioAddr;
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



void rpcHandler::handleRpcCall(string& strReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl)
{
	string error = "";
	string method = "";
	json id = nullptr;
	json clientId = nullptr;
	bool bGB2312 = false;
	bool bNeedLog = true;

	strReq = str::trim(strReq);
	if (strReq.length() == 0) 
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_InvalidReqFmt, "invalid request format. request length is 0.");
		goto HANDLE_END;
	}

	
	bGB2312 = isGB2312Pkt(strReq);
	if(bGB2312)
		strReq = charCodec::ansi2Utf8(strReq);

	strReq = ResolveTdsRpcEvnVar(strReq, pSession);


	try
	{
		//解析请求基本信息
		json jReq = json::parse(strReq);
		if (!jReq.contains("method"))
		{
			LOG("[error][TDS-RPC]协议数据包必须包含method字段\n" + strReq);
			return;
		}

		method = jReq["method"].get<string>();
		json params;
		if (jReq.contains("params"))
			params = jReq["params"];
		id = jReq["id"];
		if (id == nullptr) {
			rpcResp.isNotification = true;
			pSession->isNotification = true;
		}


		clientId = jReq["clientId"]; //tds edge模式使用
		pSession->lastMethodCalled = method;
			
		//对部分命令日志记录
		bNeedLog = needLog(method);
		if (bNeedLog)
			LOG("[trace]RPC请求:\r\n" + strReq + "\r\n");
		if(method == "output")
			LOG("[warn]RPC请求:\r\n" + strReq + "\r\n");

		//心跳最先处理
		if (method == "heartbeat")
		{
			if (params.is_object())
			{
				if (params["clientName"] != nullptr)
					pSession->name = params["clientName"];
				else if (params["name"] != nullptr)
					pSession->name = params["name"];

				if (params["echo"] != nullptr)
				{
					if (params["echo"].get<bool>() == false)
					{
						return;
					}
				}
			}
			rpcResp.result = "\"pong\"";
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
			if(jUser!=nullptr)
				pSession->org = jUser["org"].get<string>();
		}
		else
		{
			if (tds->conf->enableAccessCtrl) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_userMissing, "user is not set to call api");
				goto HANDLE_END;
			}
			else {//没开权限控制，且没有设置用户，默认用户都是admin
				pSession->user = "admin";
			}
		}
			
		//验证token
		if (bAccessCtrl) {
			if (tds->conf->enableAccessCtrl && jReq["token"] == nullptr)
			{
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenMissing, "access denied, please set access token.");
				goto HANDLE_END;
			}

			if (jReq["token"] != nullptr)
			{
				pSession->token = jReq["token"].get<string>();
			}
			//即使服务端没有打开鉴权，如果用户指定了token或者user中的
			if (pSession->token != "")
			{
				string token = pSession->token;
				string user = pSession->user;
				if (!userMng.checkToken(user, token))
				{
					//LOG("[warn]认证失败，token验证未通过,user=%s,token=%s,method=%s",user.c_str(),token.c_str(),method.c_str());
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "access denied, invalid access token");
					goto HANDLE_END;
				}
			}
		}

		//设备类命令中继转发处理.返回true表示是设备中继命令.放在用户认证前面处理.
		if (!tds->conf->edge) //tds edge模式无需转发
		{
			if (handleRpcRoute(strReq, jReq, rpcResp, pSession))
			{
				goto HANDLE_END;
			}
			if (handleChildTdsDispatch(strReq, jReq, rpcResp, pSession))
			{
				goto HANDLE_END;
			}
		}
		
		//通知消息，无需生成响应，转发后直接返回
		if (method == "notify")//来自于tds客户端的通知消息。 转发给所有的其他tds客户端
		{
			notify("notify", params, pSession);
			return;
		}
		//后端总线，实现一种微前端模块之间可以相互调用函数的机制
		//前端总线，可以在前端的app之间实现相互调用，相比于后端总线，只能调用本机浏览器上的app
		else if (method.find("app.") != string::npos) 
		{
			notify(method, params, pSession);
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
		handleMethodCall(method, params, rpcResp,pSession->getRpcSession());

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
	string strRespForLog = "";//对于某些内容特别长的数据包，省略一些内容进行日志记录
	if (rpcResp.error != "")
	{
		rpcResp.strResp = "{\"jsonrpc\":\"2.0\",\"error\":" + rpcResp.error + ",\"id\":" + id.dump();
		if (tds->conf->edge)
		{
			rpcResp.strResp += ",\"ioAddr\":\"" + tds->conf->deviceID + "\"";
			if (clientId != nullptr)
			{
				rpcResp.strResp += ",\"clientId\":" +  clientId.dump();
			}
		}
		rpcResp.strResp += "}\n\n";
	}
	else if (rpcResp.result != "")
	{
		rpcResp.strResp = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump();

		if (rpcResp.info != "") {
			rpcResp.strResp += ",\"info\":\"" + rpcResp.info + "\"";
		}
			
		rpcResp.strResp += ",\"result\":" + rpcResp.result;
		if (tds->conf->edge)
		{
			rpcResp.strResp += ",\"ioAddr\":\"" + tds->conf->deviceID + "\"";
			if (clientId != nullptr)
			{
				rpcResp.strResp += ",\"clientId\":" + clientId.dump();
			}
		}
		if (pSession->ioAddr != "")
		{
			rpcResp.strResp += ",\"ioAddr\":\"" + pSession->ioAddr + "\"";
		}
		if (pSession->tag != "")
		{
			rpcResp.strResp += ",\"tag\":\"" + pSession->tag + "\"";
		}
		rpcResp.strResp += "}\n\n";


		if (method == "fs.readFile")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$fileLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}
		else if (method == "getMoTree" || method == "getMo")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$MoTreeJsonLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}
		else if (method == "getMoTree" || method == "getMo")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$MoTreeJsonLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}
		else if (method == "db.select")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$dataSetLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}
	}

	if (rpcResp.result != "")
	{
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
		int pos = strUrl.rfind('.');
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
	MoveFile(strTmpFile.c_str(), strTargetFile.c_str());
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


	string tag, rootTag;
	if (params["tag"] != nullptr)
		tag = params["tag"].get<string>();
	if (params["rootTag"] != nullptr && params["rootTag"] != "")
		tag = params["rootTag"].get<string>() + "." + tag;
	tag = TAG::addRoot(tag, session.org);

	MP* pmp = prj.GetMPByTag(tag);
	if (!pmp)
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found:" + tag);
		return;
	}

	if (pmp->m_valType == VAL_TYPE::boolean)
	{
		if (!val.is_boolean())
		{
			if (val.is_null()) //开关量输出值省略 表示输出当前值取反
			{
				if(pmp->m_curVal.is_boolean())
					val = !pmp->m_curVal.get<bool>();
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::MO_currentValIsNull, "current value is null");
					return;
				}
			}
			else
			{
				resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeBool, "output val should be bool type");
				return;
			}
		}
	}
	else if (!val.is_number() && pmp->m_valType == VAL_TYPE::Float)
	{
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeNumber, "output val should be number type");
		return;
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
			resp.result = rlt.dump();
		}
		else
		{
			resp.error = err.dump();
			LOG("[warn]输出失败," + resp.error);
		}
	}
	else {
		resp.result = "\"output cmd sended\"";
	}
}


void rpcHandler::rpc_input(json params,RPC_RESP& resp, RPC_SESSION session)
{
	//parse param
	TIME stTimeStamp;
	string time="";
	json dataFile;
	json inputVal = nullptr;
	json inputTag, inputIoAddr;
	if (params.find("val") != params.end())
		inputVal = params["val"];
	else
	{
		resp.error = makeRPCError(TEC_paramMissing, "param val must be specified");
		return;
	}
	if (params.find("dataFile") != params.end())
		dataFile = params["dataFile"];
	if (params.find("tag") != params.end())
		inputTag = params["tag"];
	if (params.find("ioAddr") != params.end())
		inputIoAddr = params["ioAddr"];
	if (inputTag == "" && inputIoAddr == "")
	{
		resp.error = makeRPCError(TEC_paramMissing, "param ioAddr or tag must be specified");
		return;
	}
	string rootTag = "";
	if (params.contains("rootTag"))
		rootTag = params["rootTag"].get<string>();
	if (params.find("time") != params.end())
	{
		time = params["time"];
		stTimeStamp = timeopt::str2st(time);
	}
	else
	{
		timeopt::now(&stTimeStamp);
	}


	//单点输入模式统一转成数组处理
	if (inputTag.is_string()) {
		string s = inputTag.get<string>();
		inputTag = json::array();
		inputTag.push_back(s);

		json val = inputVal;
		inputVal = json::array();
		inputVal.push_back(val);
	}
	else if (inputIoAddr.is_string()) {
		string s = inputIoAddr.get<string>();
		inputIoAddr = json::array();
		inputIoAddr.push_back(s);

		json val = inputVal;
		inputVal = json::array();
		inputVal.push_back(val);
	}


	//监测点组输入模式
	vector<MP*> vecMps;
	for (int i = 0; i < inputTag.size(); i++) {
		string tag = inputTag[i];
		json val = inputVal[i];
		tag = TAG::addRoot(tag, rootTag);
		tag = TAG::addRoot(tag, session.org);

		if (tag != "")
		{
			MP* pmp = prj.GetMPByTag(tag);
			if (!pmp)
			{
				resp.error = makeRPCError(MO_specifiedTagNotFound, "tag not exist");
				return;
			}

			pmp->input(val, &stTimeStamp, dataFile);
			vecMps.push_back(pmp);
		}
	}

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
	json jStatusNotify;
	json jUpdateTags = json::array();
	json jUpdateVals = json::array();
	for (int i = 0; i < vecMps.size(); i++) {
		MP* pmp = vecMps[i];
		jUpdateTags.push_back(pmp->getTag());
		jUpdateVals.push_back(pmp->m_curVal);
	}
	jStatusNotify["tag"] = jUpdateTags;
	jStatusNotify["val"] = jUpdateVals;
	jStatusNotify["time"] = time;
	rpcSrv.notify("statusUpdate", jStatusNotify);


	//使用ioAddr来input忘了哪里调用了，后续观察删掉
	for (int i = 0; i < inputIoAddr.size(); i++) {
		string ioAddr = inputIoAddr[i];
		json val = inputIoAddr[i];

		if (ioAddr != "")
		{
			ioChannel* pC = ioSrv.getChanByIOAddr(ioAddr);
			pC->input(val);
		}
	}

	resp.result = "\"ok\"";
}

string rpcHandler::rpc_getTopoList(json params, string& error,RPC_SESSION session)
{
	string path = tds->conf->confPath + "/topo";
	path::normalization(path);
	vector<string> fl;
	fs::getFileList(fl,path);
	map<string, string> mapTopo; //按照层级排序
	vector<string> topoList;
	for (int i = 0; i < fl.size(); i++)
	{
		string topoName = str::trimSuffix(fl[i], ".svg");
		mapTopo[str::fromInt(TAG::getMoLevel(fl[i])) + topoName] = topoName;
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
			string& s = topoList[i];
			if (!userMng.checkTagPermission(session.user, s))
			{
				topoList.erase(topoList.begin() + i);
				i--;
			}
		}
	}

	json j = topoList;
	return j.dump();
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

	OBJ* pMo = prj.queryObj(rootTag);
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

	vector<ALARM_INFO*> vecAlarms = almSrv.tableCurrent.query(querier);
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

void rpcHandler::zlm_openStream(string tag,string srcUrl)
{
	string tagPinyin;
	str::hanZi2Pinyin(tag,tagPinyin);
	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string streamServerUrl = "http://127.0.0.1:" + sPort;
	//tag = httplib::detail::encode_url(charCodec::utf8toAnsi(tag));
	httplib::Client cli(streamServerUrl);
	httplib::Headers headers;
	httplib::Params params = {
		{ "vhost", "__defaultVhost__" },
		{"app","stream"},
		{"stream",tag},
		{"url",srcUrl},
		{"enable_hls","0"},  
		{"enable_ts","0"},
		{"enable_mp4","0"}
	};

	string uri = "/index/api/addStreamProxy";
	auto res = cli.Get(uri, params, headers);
	LOG("打开流媒体源,Get " + streamServerUrl + uri + ",tag=" + tag + ",媒体源=" + srcUrl);
	if (res != nullptr) {

	}
	else {
		LOG("[error]zlm stream server 未响应," +uri);
	}
}


void rpcHandler::rpc_getDevStatis(json params, RPC_RESP& resp,RPC_SESSION session)
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

string rpcHandler::rename(string orgName, json& renameMap) {
	string desName = orgName;
	if (renameMap[orgName].is_string()) {
		desName = renameMap[orgName];
	}
	return desName;
}



void rpcHandler::rpc_moList2Attrlist(Mo_Attr_Params& params,vector<OBJ*> moList,RPC_RESP& resp, RPC_SESSION session)
{
	string strList = "[";
	for (int i = 0; i < moList.size(); i++)
	{
		OBJ* pMo = moList[i];
		string sysTag = pMo->getTag();
		string queryTag = sysTag;

		//过滤用户权限
		if (session.user != "")
		{
			if (!userMng.checkTagPermission(session.user, sysTag))
				continue;
		}

		//根据位号选择器过滤
		queryTag = TAG::trimRoot(sysTag, params.tagSel.m_rootTag);

		nlohmann::ordered_json oneData;
		oneData[rename("位号", params.renameMap)] = queryTag;

		//自定义监测对象类型，都判断一下是否是智能设备，也就是和ioDev绑定
		if (pMo->m_type == MO_TYPE::customMo) {
			ioDev* piod = ioSrv.getIODevByTag(sysTag);
			if (piod)
			{
				oneData[rename("在线", params.renameMap)] = piod->m_bOnline;
			}
			else
			{
				oneData[rename("在线", params.renameMap)] = pMo->m_bOnline;
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
				oneData[rename(pmp->m_name, params.renameMap)] = jVal;
			}
			else {
				oneData[rename(pmp->getTag(sysTag), params.renameMap)] = jVal;
			}
		}
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
		str::hanZi2Pinyin(attrParam.moType, attrParam.moType);
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
	attrParam.tagSel.init(jTag, attrParam.rootTag);
	attrParam.tagSel.type = attrParam.moType;
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

	if(mode == "list")
		rpc_moList2Attrlist(attrParam, moList, resp, session);
	else
		rpc_moList2table(attrParam, moList, resp, session);
}

void rpcHandler::rpc_moList2table(Mo_Attr_Params& params, vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION session)
{
	json jTable = json::object();
	json jTableHead = json::array();
	json jTableBody = json::array();
	json jColTag = json::array();


	//根据对象模版生成列模版
	map<string, OBJ_TEMPLATE>::iterator it = prj.m_mapObjTempalte.find(params.moType);
	if (it != prj.m_mapObjTempalte.end()) {
		OBJ_TEMPLATE& ot = it->second;
		OBJ obj;
		obj.loadConf(ot.tplData);
		vector<MP*> mps;
		obj.GetAttriMp(mps);
		string parentTag = obj.getTag();
		if (mps.size() > 0) {
			jTableHead.push_back("位号");
			jColTag.push_back(nullptr);
			for (int j = 0; j < mps.size(); j++)
			{
				MP* pmp = mps[j];
				string tag = pmp->getTag(parentTag);
				jColTag.push_back(tag);
				if (params.columeLabel == "tag") {
					jTableHead.push_back(tag);
				}
				else {
					jTableHead.push_back(pmp->m_name);
				}
			}
			jTableHead.push_back("在线");
			jTableHead.push_back("更新时间");
			jColTag.push_back(nullptr);
			jColTag.push_back(nullptr);
			jTable["header"] = jTableHead;
			jTable["tag"] = jColTag;
		}
	}


	for (int i = 0; i < moList.size(); i++)
	{
		OBJ* pMo = moList[i];
		string moTag = pMo->getTag();
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

			jTableHead.push_back("位号");
			jColTag.push_back(nullptr);
			for (int j = 0; j < childMps.size(); j++)
			{
				MP* pmp = childMps[j];
				string tag = pmp->getTag(moTag);
				jColTag.push_back(tag);
				if (params.columeLabel == "tag") {
					jTableHead.push_back(tag);
				}
				else {
					jTableHead.push_back(pmp->m_name);
				}
			}
			jTableHead.push_back("在线");
			jTableHead.push_back("更新时间");
			jColTag.push_back(nullptr);
			jColTag.push_back(nullptr);
			jTable["header"] = jTableHead;
			jTable["tag"] = jColTag;
		}


		//数据行
		json jTableRow;
		jTableRow.push_back(tag);
		for (int j = 1; j < jColTag.size()-2; j++)
		{
			string tag = jColTag[j];
			tag = TAG::addRoot(tag, moTag);
			MP* pmp = pMo->GetMPByTag(tag);
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
	resp.result = jTable.dump();
}

string rpcHandler::rpc_getMpStatus(json params, string& error, RPC_SESSION session, bool bValOnly)
{
	bool getStatus = true;
	bool getConf = true;
	bool getStatusDesc = false;

	if (params.contains("getConf"))
	{
		getConf = params["getConf"].get<bool>();
	}
	if (params.contains("getStatus"))
		getStatus = params["getStatus"].get<bool>();


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
			string tag = pmp->getTag();

			if (session.user != "")
			{
				if (!userMng.checkTagPermission(session.user, tag))
					continue;
			}

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
			json j;
			pmp->toJson(j,q);
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
		return rpc_getconffile(params,error);
	}
	else if (type == "file-list")
	{
		string path = "";
		if (params.find("path") != params.end())
			path = params["path"].get<string>();
		if (path != "")
		{
			string conf = "";
			path = tds->conf->confPath + "/" + path;
			path::normalization(path);
			vector<string> fl;
			fs::getFileList(fl,path);
			json j = fl;
			return j.dump();
		}
		return string();
	}
	return "";
}

string rpcHandler::rpc_setconf(json params, string& error)
{
	//按照类型配置
	string type = "";
	if (params.find("type") != params.end())
		type = params["type"].get<string>();
	if (type == "file")
	{
		return rpc_setconffile(params,error);
	}

	return "";
}

string rpcHandler::rpc_getconffile(json params, string& error)
{
	string path = "";
	if (params.find("path") != params.end())
		path = params["path"].get<string>();
	if (path != "")
	{
		string conf = "";
		path = tds->conf->confPath + "/" + path;
		path::normalization(path);
		fs::readFile(path, conf);
		json j = conf;
		return j.dump();
	}
	return string();
}

string rpcHandler::rpc_setconffile(json params, string& error)
{
	string path = "";
	if (params.find("path") != params.end())
		path = params["path"].get<string>();
	if (path != "")
	{
		string conf = params["conf"].get<string>();
		path = tds->conf->confPath + "/" + path;
		fs::createFolderOfPath(path);
		fs::writeFile(path, conf);
		return "ok";
	}
	return string();
}

json rpcHandler::rpc_getStreamUrl(MP* pmp,string tag, bool isHttps, string hostname,int hostport)
{
	string ip = hostname; 
	int port = hostport;
	bool https = false;

	OBJ* childTds = pmp->getOwnerChildTds();
	//重定向到子服务
	CHILD_TDS_INFO childTdsInfo;
	bool isChildTds = false;
	if (childTds && pMasterDs) {
		string childTdsTag = childTds->getTag();
		if (!pMasterDs->getChildTdsInfo(childTdsTag, childTdsInfo))
		{
			return nullptr;
		}
		tag = TAG::trimRoot(tag, childTdsTag);

		ip = childTdsInfo.ip;
		if (isHttps) {
			port = childTdsInfo.httpsPort;
		}
		else {
			port = childTdsInfo.httpPort;
		}
		isChildTds = true;
	}

	json j;

	string tagPinyin;
	str::hanZi2Pinyin(tag, tagPinyin);
	string urlProto;
	string wsProto;
	if (isHttps) {
		urlProto = "https://";
		wsProto = "wss://";
		port = tds->conf->getInt("httpsMediaPort",668);
	}
	else {
		urlProto = "http://";
		wsProto = "ws://";
		port = tds->conf->getInt("httpMediaPort",669);
	}


	//https://github.com/zlmediakit/ZLMediaKit/wiki/%E6%92%AD%E6%94%BEurl%E8%A7%84%E5%88%99
	//zlmediakit的hls模式暂时不支持中文，因此此处转成拼音
	if (!childTds) {
		if (pmp->m_valType == VAL_TYPE::video) {
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
	//此处先发送到子服务的数据服务端口，让子服务再做一次重定向，使得子服务再收到该请求时可以启动码流。
	//实现url取流的时候可以触发向视频源拉流
	else {
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
		if (p)
			p->toJson(j, params);
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device with specified ioAddr not found");
		}
	}
	else if(params.contains("tag")){
		string tag = params["tag"];
		tag = TAG::addRoot(tag, session.org);
		p = ioSrv.getIODevByTag(tag);
		if (p)
			p->toJson(j, params);
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
	vector<ioDev*> ary = ioSrv.getChildren(IO_DEV_TYPE::GW::local_serial);
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
		jComInfo["callbackUser"] = (DWORD)pls->m_pCallbackUser;
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


void rpcHandler::notify(string method, json params, std::shared_ptr<TDS_SESSION> orgSession)
{
	if (method == "devOnline" || method == "devOffline") {
		if (params.contains("tag")) {
			string tag = params["tag"];
			OBJ* p = prj.queryObj(tag);
			if (p) {
				if (method == "devOnline")
					p->m_bOnline = true;
				else
					p->m_bOnline = false;
			}
			else {
			}
		}
	}


	string notify = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params.dump() + "}\n\n";

	WebServer::sendToAllWebsock(notify);
	ds.sendToAllSessions(notify);
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
