#include <common/dtwrecoge.h>
#include "pch.h"
#include "miniz.h"
#include "rpcHandler.h"
#include "prj.h"
#include "tAlmSrv.h"
#include "mp.h"
#include "tSockSrv.h"
#include "logger.h"
#include "database/tDatabase.h"
#include <json.hpp>
#include "ioSrv.h"
#include "ioChan.h"
#include "userMng.h"
#include "logServer.h"
#include "scriptManager.h"
#include "base64.h"
#include <fstream>
#include "webSrv.h"
#include "ioDev_camera.h"
#include "statusServer.h"
#include <io_server/ioDev_tdsp.h>
#include "tds_imp.h"
#include "proto_dlt645_2007.hpp"
#include "scriptEngine.h"
#include "scriptFunc.h"
#include "ioDev_onvif.h"
#include "rpcHandler_common.h"
#include "uplink_mqtt.h"
#include "uplinkManager.h"
#include "rsa_verify.h"
#include "video/streamServer.h"
#include "video/dtls_transport.h"

#ifdef _WIN32
	#include <shellapi.h>
	#include "memDiag.h"
#endif

rpcHandler rpcSrv;

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
}

rpcHandler::~rpcHandler()
{

}


bool rpcHandler::init()
{
	return true;
}

#ifdef _WIN32

bool createAutoStartService(const std::string& execPath, const std::string& execName) {
	std::string fullPath = execPath + "/" + execName + ".exe";
	std::replace(fullPath.begin(), fullPath.end(), '/', '\\');
	HKEY hKey;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
		RegSetValueEx(hKey, execName.c_str(), 0, REG_SZ, (const BYTE*)fullPath.c_str(), fullPath.size() + 1);
		RegCloseKey(hKey);
		std::cout << "success to write registry Software\\Microsoft\\Windows\\CurrentVersion\\Run: " << fullPath << std::endl;
		return true;
	}
	else {
		std::cerr << "failed to open registry Software\\Microsoft\\Windows\\CurrentVersion\\Run" << std::endl;
		return false;
	}
}

#else

bool createSystemdService(const std::string& execPath, const std::string& execName, const std::string& systemdPath= "/etc/systemd/system/tds.service") {
	std::string serviceContent =
		"[Unit]\n"
		"Description=tds\n"
		"After=network.target\n"
		"\n"
		"[Service]\n"
		"ExecStart=" + execPath + "/" + execName + "\n"
		"Restart=always\n"
		"User=root\n"
		"WorkingDirectory=" + execPath + "\n"
		"\n"
		"[Install]\n"
		"WantedBy=multi-user.target\n";

	std::ofstream serviceFile(systemdPath);
	if (!serviceFile) {
		std::cerr << "failed to write systemd file: " << systemdPath << std::endl;
		return false;
	}

	serviceFile << serviceContent;
	serviceFile.close();

	std::cout << "success to create systemd file: " << systemdPath << std::endl;
	return true;
}

#endif

bool rpcHandler::handleMethodCall_OSFunc(string method, json& params, RPC_RESP& rpcResp)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	//文件操作
	if (method == "setAutoStart") {
		if (!params["autoStart"].is_boolean()) {
			error = makeRPCError(TEC_FAIL, "param autoStart not specified");
		}
		else {
			bool autoStart = params["autoStart"].get<bool>();
			if (autoStart) {
				std::string appPath = fs::appPath();
				std::string appName = fs::appName();
#ifdef _WIN32
				result = RPC_FAIL;
				bool bCreate = createAutoStartService(appPath, appName);
				if (bCreate) {
					result = RPC_OK;
				}
				else {
					std::cerr << "create auto start service failed!" << std::endl;
					result = RPC_FAIL;
					error = makeRPCError(TEC_FAIL, "create auto start service failed!");
				}
#else
				bool bCreate = createSystemdService(appPath, appName);
				if (bCreate) {
					std::cout << "create systemd service success!" << std::endl;
					int res = system("systemctl enable tds.service");
					if (res == 0) {
						result = RPC_OK;
					}
					else {
						result = RPC_FAIL;
						error = makeRPCError(TEC_FAIL, "disable tds.service failed!, error code: " + std::to_string(res));
					}
				}
				else {
					std::cerr << "create tds.service failed!" << std::endl;
					result = RPC_FAIL;
					error = makeRPCError(TEC_FAIL, "create tds.service failed!");
				}
#endif
			}
			else {
#ifdef _WIN32
#else
				int res = system("systemctl disable tds.service");
				if (res != 0) {
					result = RPC_FAIL;
					error = makeRPCError(TEC_FAIL, "disable tds.service failed!, error code: " + std::to_string(res));
				}
				else {
					result = RPC_OK;
				}
#endif
			}
		}
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
bool rpcHandler::handleMethodCall_ptz_cloud(string method, MP* pmp, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
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
bool rpcHandler::handleMethodCall_ptz_ioDev(string method, string tag,json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
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
	if (method == "startPanTilt") {
		string dir = params["dir"];

		float panSpeed = 0;
		if (params.contains("panSpeed")) {
			panSpeed = params["panSpeed"].get<float>();
		}

		float tiltSpeed = 0;
		if (params.contains("tiltSpeed")) {
			tiltSpeed = params["tiltSpeed"].get<float>();
		}

		pCam->ptz_startMove(dir, panSpeed, tiltSpeed);

		LOG("移动云台,方向:%s,panSpeed:%.2f,tiltSpeed:%.2f", dir.c_str(), panSpeed, tiltSpeed);

		pCam->isMoving = true;
		pCam->startMoveTime = timeopt::now();

		//sync mode
		if (params.contains("time")) {
			int time = params["time"].get<int>();
			timeopt::sleepMilli(time);
			pCam->ptz_stopMove();
			pCam->isMoving = false;
		}

		//async mode
		//if (params.contains("time")) {
		//	int time = params["time"].get<int>();
		//	json paramAsynCall;
		//	paramAsynCall["tag"] = tag;
		//	tds->callAsyn("stopPanTilt", paramAsynCall, time);
		//}
	}
	else if (method == "stopPanTilt") {
		if (!pCam->isMoving) {
			timeopt::sleepMilli(400);  //解决前端点击按钮快速发送一个开始停止云台命令的情况，startPanTilt的代码处理逻辑多，会导致发送出去在 stopMove的后面
		}

		pCam->ptz_stopMove();
		pCam->isMoving = false;
	}
	else if (method == "startZoom") {
		string dir = params["dir"];

		float speed = 0;
		if (params.contains("speed")) {
			speed = params["speed"].get<float>();
		}

		pCam->ptz_startZoom(dir,speed);

		if (params.contains("time")) {
			int time = params["time"].get<int>();
			timeopt::sleepMilli(time);
			pCam->ptz_stopZoom();
		}

		//if (params.contains("time")) {
		//	int time = params["time"].get<int>();
		//	json paramAsynCall;
		//	paramAsynCall["tag"] = tag;
		//	tds->callAsyn("stopZoom", paramAsynCall, time);
		//}
	}
	else if (method == "stopZoom") {
		pCam->ptz_stopZoom();
	}
	else if (method == "startFocus") {
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
	else if (method == "stopFocus") {
		pCam->ptz_stopFocus();
	}
	else if (method == "pausePresetPatrol") {
		if (!params["parseTime"].is_number_integer()) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "int param parseTime must be specified");
			return true;
		}

		int parseTime = params["parseTime"].get<int>();
		pCam->ptz_pausePresetPatrol(parseTime);
	}
	else if (method.find("Preset") != string::npos) {
		if (params.contains("presetIndex")) {
			if (!params["presetIndex"].is_number_integer()) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "int param presetIndex must be specified");
				return true;
			}

			int idx = params["presetIndex"].get<int>();

			if (method == "gotoPreset") {
				pCam->ptz_gotoPreset(idx);
			}
			else if (method == "addPreset") {
				pCam->ptz_addPreset(idx);
			}
			else if (method == "deletePreset") {
				pCam->ptz_deletePreset(idx);
			}
		}
		else if(params.contains("preset")){
			string preset = params["preset"].get<string>();
			if (method == "gotoPreset") {
				pCam->ptz_gotoPreset(preset);
			}
		}
	}
	
	rpcResp.result = "\"ok\"";
	return true;
}

bool rpcHandler::handleMethodCall_video(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	string& result = rpcResp.result;
	string& error = rpcResp.error;

	bool bHandled = true;

	if (method == "getYsAccessInfo") {
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
			 method == "closeStream" ||
		     method == "pausePresetPatrol") {
		string tag, rootTag;
		if (!parseParam_tag(params, rpcResp, session, tag, rootTag)) {
			return true;
		}

		MP* pObj = (MP*)prj.queryObj(tag,session.language);
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
				if (params.contains("presetIndex")) {
					if (!params["presetIndex"].is_number_integer()) {
						rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "param presetIndex must be specified,and must be int type");
						return true;
					}

					string presetIndex = params["presetIndex"].dump();
					info = "调用预置位:" + presetIndex;
				}
				else if (params.contains("preset")) {
					string preset = params["preset"].get<string>();
					info = "调用预置位:" + preset;
				}
			}
			else if (method == "deletePreset") {
				if (!params["presetIndex"].is_number_integer()) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "param presetIndex must be specified,and must be int type");
					return true;
				}

				string presetIndex = params["presetIndex"].dump();
				info = "删除预置位:" + presetIndex;
			}
			else if (method == "pausePresetPatrol") {
				if (!params["parseTime"].is_number_integer()) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "param parseTime must be specified,and must be int type");
					return true;
				}

				string parseTime = params["parseTime"].dump();
				info = "暂停预置位时间:" + parseTime;
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
				method == "deletePreset") {

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
			     method == "deletePreset" ||
			     method == "pausePresetPatrol") {
			MP* pmp = pObj;
			if (!pmp) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "specified tag not found");
			}
			else {
				handleMethodCall_ptz_ioDev(method, tag, params, rpcResp, session);
			}
			return true;
		}
		else if (method == "keepStream") {
			TIME st;
			timeopt::now(&st);

			m_mapPullerActive[tag] = st;
			rpcResp.result = "\"ok\"";
		}
	}
	else {
		bHandled = false;
	}

	return bHandled;
}

//参见核心概念，位号表示法
//https://www.liangtusoft.com/doc/#/核心概念?id=位号表示法
//sysTag = session.org + rootTag + tag
bool rpcHandler::parseParam_tag(json& params, RPC_RESP& rpcResult, RPC_SESSION& session, string& tag, string& rootTag) {
	if (!params.contains("tag")) {
		rpcResult.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param : tag");
		return false;
	}

	tag = params["tag"];

	//获取查询根
	rootTag = "";
	if (params["rootTag"] != nullptr) {
		rootTag = params["rootTag"].get<string>();
	}

	rootTag = TAG::addRoot(rootTag, session.org);
	tag = TAG::addRoot(tag, rootTag);

	return true;
}

//老版本deepvision输出json转为新格式，暂用
void processJson(string& sParams) {

	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);

	yyjson_mut_doc* outDoc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* outRoot = yyjson_val_mut_copy(outDoc, yyjson_doc_get_root(doc));
	yyjson_mut_doc_set_root(outDoc, outRoot);
	auto infoVal = yyjson_mut_obj_get(outRoot, "info");
	string outInfoStr = "";
	if (infoVal)
	{
		outInfoStr = yyjson_mut_get_str(infoVal);
		if (outInfoStr != "")
		{
			yyjson_doc* infoDoc = yyjson_read(outInfoStr.c_str(), outInfoStr.size(), 0);
			yyjson_mut_doc* tmpInfoDoc = yyjson_mut_doc_new(nullptr);
			yyjson_mut_doc_set_root(tmpInfoDoc, yyjson_val_mut_copy(tmpInfoDoc, yyjson_doc_get_root(infoDoc)));
			yyjson_mut_val* tmpRoot = yyjson_mut_doc_get_root(tmpInfoDoc);

			yyjson_mut_val* objVal = yyjson_mut_obj_get(tmpRoot, "objContour");

			yyjson_mut_val* objValCopy = yyjson_mut_val_mut_copy(tmpInfoDoc, objVal);

			size_t idx = 0;
			size_t max = 0;
			yyjson_mut_val* item;

			yyjson_mut_arr_foreach(objValCopy, idx, max, item)
			{
				yyjson_mut_obj_add_strcpy(tmpInfoDoc, item, "name",
					yyjson_mut_get_str(yyjson_mut_obj_get(item, "type")));
				yyjson_mut_obj_remove_key(item, "type");

				yyjson_mut_val* oldBbox = yyjson_mut_obj_get(item, "bbox");
				yyjson_mut_val* newBbox = yyjson_mut_obj(tmpInfoDoc);

				yyjson_mut_obj_add_int(tmpInfoDoc, newBbox, "x", yyjson_mut_get_int(yyjson_mut_arr_get(oldBbox, 0)));
				yyjson_mut_obj_add_int(tmpInfoDoc, newBbox, "y", yyjson_mut_get_int(yyjson_mut_arr_get(oldBbox, 1)));
				yyjson_mut_obj_add_int(tmpInfoDoc, newBbox, "w", yyjson_mut_get_int(yyjson_mut_arr_get(oldBbox, 2)));
				yyjson_mut_obj_add_int(tmpInfoDoc, newBbox, "h", yyjson_mut_get_int(yyjson_mut_arr_get(oldBbox, 3)));

				yyjson_mut_obj_remove_key(item, "bbox");
				yyjson_mut_obj_add_val(tmpInfoDoc, item, "bbox", newBbox);
			}

			yyjson_mut_obj_add_val(tmpInfoDoc, tmpRoot, "objects", objValCopy);

			yyjson_mut_obj_remove_key(tmpRoot, "objContour");


			char* outInfoStrChanged = yyjson_mut_write(tmpInfoDoc, 0, 0);
			yyjson_mut_obj_remove_key(outRoot, "info");
			yyjson_mut_obj_add_strcpy(outDoc, outRoot, "info", outInfoStrChanged);
			free(outInfoStrChanged);
			char* outChar = yyjson_mut_write(outDoc, 0, 0);
			sParams= outChar;
			free(outChar);
			yyjson_doc_free(doc);
			yyjson_doc_free(infoDoc);
			yyjson_mut_doc_free(outDoc);
			yyjson_mut_doc_free(tmpInfoDoc);
		}
	}
	else {
		yyjson_mut_doc* tmpInfoDoc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* tmpInfoRoot = yyjson_mut_obj(tmpInfoDoc);
		yyjson_mut_doc_set_root(tmpInfoDoc, tmpInfoRoot);
		yyjson_mut_val* tmpInfoVal = yyjson_mut_obj(tmpInfoDoc);
		yyjson_mut_val* tmpObjVal = yyjson_mut_arr(tmpInfoDoc);
		yyjson_mut_obj_add_val(tmpInfoDoc, tmpInfoRoot, "objects", tmpObjVal);
		char* outInfoChar = yyjson_mut_write(tmpInfoDoc, 0, 0);
		yyjson_mut_obj_add_strcpy(outDoc, outRoot, "info", outInfoChar);
		free(outInfoChar);
		char* outChar = yyjson_mut_write(outDoc, 0, 0);
		sParams = outChar;
		free(outChar);
		yyjson_doc_free(doc);
		yyjson_mut_doc_free(tmpInfoDoc);
		yyjson_mut_doc_free(outDoc);
	}
}

bool rpcHandler::handleMethodCall_db(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	string& result = rpcResp.result;
	string& error = rpcResp.error;

	bool bHandled = true;
	
	if (method.find("db.") != string::npos) {
		if (method == "db.renameFolder") {
			string oldName = params["old"].get<string>();
			string newName = params["new"].get<string>();

			vector<fs::FILE_INFO> vec;
			fs::getFolderList(vec, db.m_path,true);
			try {
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
			catch (const std::exception& e) {
				json jErr = e.what();
				rpcResp.error = jErr.dump();
			}
		}
		else if (method == "db.saveImage") {
			string s = params.dump();
			processJson(s); // 老版本deepvision输出json转为新格式

			if (tds->conf->largeModelType < 0) {
				db.rpc_db_saveImage(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org, session.language);
			}
			else if (tds->conf->largeModelType == 0) {
				samAlarmCheck(s);
				db.rpc_db_saveImage(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org, session.language);
			}
			else if (tds->conf->largeModelType == 1) {
				db.rpc_db_saveImage(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org, session.language);
				vlmAlarmCheck(s);
			}
		}
		else if (!params.contains("time")) {
			error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param : time");
		}
		else if (method == "db.merge") {
			string s = params.dump();
			db.rpc_db_merge(s, rpcResp.result, rpcResp.error, rpcResp.info, session.org, session.language);
		}
		else {
			bHandled = false;
		}
	}
	else {
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


void rpcHandler::rpc_getApiSessionStatus(json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
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
#define TEST
bool rpcHandler::handleMethodCall_debugFunc(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
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
	else if(method == "getServerID" || method == "getsid")
	{ 
        rpcResp.result = "\"" + tds->getServerID() + "\"";
	}
	else if (method == "captureFrame")
	{

	}
	else if (method == "getSockSrvStatus") {
		rpcResp.result = "{"
			"\"parentTdsClient\":[";

		for (int i = 0; i < sockSrv.m_tcpClt_ParentTds.size();i++) {
			tcpClt* clt = sockSrv.m_tcpClt_ParentTds[i];
			rpcResp.result +=
				"{"
					"\"remoteIP\":\"" + clt->m_remoteIP + "\","
					"\"remotePort\":" + to_string(clt->m_remotePort) + ","
					"\"lastConnTime\":\"" + clt->lastConnTime + "\","
				    "\"connected\":" + string(clt->m_bConn?"true":"false") + ","
					"\"connectting\":" + string(clt->m_isConnectting == enTcpCltConnectStatus::CONNECTING ? "true" : "false") +
				"}";
			if (i < sockSrv.m_tcpClt_ParentTds.size() - 1) {
				rpcResp.result += ",";
			}
		}

		rpcResp.result += "]}";
	}
	else if (method == "getSessionBuff")
	{
		string remoteAddr = params["remoteAddr"].get<string>();
		shared_ptr<TDS_SESSION> pSession = ioSrv.getIOSession(remoteAddr);

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
		string fmt = params["fmt"].get<string>();
		string data = params["data"].get<string>();
		vector<unsigned char> vec;

		if (fmt == "gb2312") {
			data = charCodec::utf8_to_gb(data);
		}
		else if (fmt == "hexStr") {
			vec = str::hexStrToBytes(data);
		}

		shared_ptr<TDS_SESSION> pDestSession = ioSrv.getIOSession(tdsSession);
		if (pDestSession == nullptr)
		{
			rpcResp.error = "\"session not found," +  tdsSession +  "\"";
			return true;
		}
		else {
			size_t iSended = 0;
			if(vec.size()>0)
				iSended = pDestSession->send(vec.data(), vec.size());
			else
				iSended = pDestSession->send((char*)data.c_str(), data.length());
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

bool rpcHandler::handleMethodCall_IoMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getDev")
	{
		rpc_getDev(params, rpcResp,session);
	}
	//用于测试排查相对绑定的关系是否正确
	else if (method == "getTagBind") {
		string ioAddr = params["ioAddr"];
		ioDev* p = ioSrv.getIODev(ioAddr);
		if (p) {
			json j;
			j["tagBindRelative"] = p->m_strTagBind;
			j["tagBindAbsolute"] = p->getTagBind();
			result = j.dump();
		}
		else {
			error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "device of specified ioAddr not found");
		}
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
		ioSrv.saveConf();
		result = RPC_OK;
	}
	else if (method == "openAllCycleAcq") {
		ioSrv.openAllCycleAcq();
		ioSrv.saveConf();
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
		rpcResp.result = "[";
		for (auto& i : ioSrv.m_mapChanTempalte) {
			if (rpcResp.result != "[") {
                rpcResp.result += ",";
			}
			rpcResp.result += "{\"name\":\"" + i.first + "\"}";
		}
		rpcResp.result += "]";
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

			struct SendPkt {
				string sPkt;
				vector<unsigned char> pkt;
				int delay;
				SendPkt() {
					delay = 0;
				}
			};
			vector<SendPkt> pktList;

			if (params["data"].is_string()) {
				SendPkt sp;
				sp.sPkt = params["data"];
				if (!str::isValidHexString(sp.sPkt)) {
					rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "invalid hex string");
					return true;
				}
				sp.pkt = str::hexStrToBytes(sp.sPkt);
				pktList.push_back(sp);
			}
			else if (params["data"].is_array()) {
				json jData = params["data"];
				for (int i = 0; i < jData.size(); i++) {
					SendPkt sp;
					json& jOnePkt = jData[i];
					sp.sPkt = jOnePkt["pkt"];
					if (!str::isValidHexString(sp.sPkt)) {
						rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "invalid hex string");
						return true;
					}
					sp.pkt = str::hexStrToBytes(sp.sPkt);
					if (jOnePkt["delay"].is_number_integer()) {
						sp.delay = jOnePkt["delay"].get<int>();
					}
					pktList.push_back(sp);
				}
			}

			for (int i = 0; i < pktList.size(); i++) {
				SendPkt& sp = pktList[i];
				LOG("[warn][sendToDev] 发送到设备:%s,数据包:%s,发送后延时:%dms", p->getIOAddrStr().c_str(), sp.sPkt.c_str());
				p->sendData(sp.pkt.data(), sp.pkt.size());

				if (sp.delay > 0) {
					timeopt::sleepMilli(sp.delay);
				}
			}

			rpcResp.result = RPC_OK;
		}
	}
	else if (method == "parseIOPkt" || method == "parseIoPkt") {
		string s = params["data"];
		vector<unsigned char> vec = str::hexStrToBytes(s);
		unsigned char* p = vec.data();
		size_t l = vec.size();
		
		json jResp;
		if (isValidPkt_DLT645_2007(p, l)) {
			DLT645_2007_PKT pkt;
			bool unpackRet = pkt.unpack(p, l);
			jResp["proto"] = "dlt645-2007";
			jResp["unpack"] = unpackRet ? "success" : "fail";
			jResp["preambleCount"] = pkt.preamble;
			jResp["ctrlCode"] = str::format("%02X", pkt.ctrlCode);
			jResp["addr"] = str::bytesToHexStr(pkt.addr, 6);
		}
		else {
			jResp["proto"] = "unknown";
		}

		rpcResp.result = jResp.dump(2);
	}
	else if (method == "onvifSnapshot") {
		string user = params["user"].get<string>();
		string pwd = params["pwd"].get<string>();
		string ip = params["ip"].get<string>();
		int port = params["port"].get<int>();
		DIGIST_INFO di;
		onvifSnapshot(ip, port, user, pwd, di);
		json j;
		j["authorization"] = di.authorization;
		j["realm"] = di.realm;
		j["nouce"] = di.nouce;
		rpcResp.result = j.dump();
	}
	else if (method == "generateDigestAuth") {
		string user = params["user"].get<string>();
		string pwd = params["pwd"].get<string>();
		string uri = params["uri"].get<string>();
		string realm = params["realm"].get<string>();
		string nonce = params["nonce"].get<string>();
		string cnonce = params["cnonce"].get<string>();
		string method = params["method"].get<string>();
		string s = ioDev_onvif::generateAuthStr(user, pwd,method, uri, realm, nonce, cnonce);
		json j = s;
		rpcResp.result = j.dump();
	}
	else
	{
		bHandled = false;
	}

	return bHandled;
}

bool rpcHandler::handleMethodCall_audioPlayer(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
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

std::string getFileNameFromURL(const std::string& url) {
	size_t found = url.find_last_of("/\\");
	if (found != std::string::npos) {
		return url.substr(found + 1);
	}
	return "";
}

#include <filesystem>
namespace stdfs = std::filesystem;


bool renameFile(std::string filePath, const std::string& oldName, const std::string& newName) {
	std::wstring wsFilePath = charCodec::utf8_to_utf16(filePath);
	std::wstring wsOldName = charCodec::utf8_to_utf16(oldName);
	std::wstring wsNewName = charCodec::utf8_to_utf16(newName);

	try {
		stdfs::path oldPath = stdfs::path(wsFilePath) / wsOldName;
		stdfs::path newPath = stdfs::path(wsFilePath) / wsNewName;
		stdfs::rename(oldPath, newPath);
		return true;
	}
	catch (const stdfs::filesystem_error& e) {
		std::cerr << "Error renaming file: " << e.what() << std::endl;
		return false;
	}
}


// 解压缩文件
#ifdef _WIN32
bool extract_zip( std::string zip_path,  std::string dest_dir) {
	mz_zip_archive zip_archive;
	memset(&zip_archive, 0, sizeof(zip_archive));

	if (!mz_zip_reader_init_file(&zip_archive, zip_path.c_str(), 0)) {
		std::cerr << "Failed to open zip file." << std::endl;
		return false;
	}

	int num_files = mz_zip_reader_get_num_files(&zip_archive);
	for (int i = 0; i < num_files; ++i) {
		mz_zip_archive_file_stat file_stat;
		if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
			std::cerr << "Failed to get file info." << std::endl;
			mz_zip_reader_end(&zip_archive);
			return false;
		}

		std::string file_path = dest_dir + "/" + file_stat.m_filename;

		if (mz_zip_reader_is_file_a_directory(&zip_archive, i)) {
			// 创建目录
			fs::createFolderOfPath(file_path);
		}
		else {
			// 创建文件所在的目录
			size_t last_slash = file_path.find_last_of('/');
			if (last_slash != std::string::npos) {
				std::string dir_path = file_path.substr(0, last_slash);
				fs::createFolderOfPath(dir_path);
			}

			// 解压文件
			if (!mz_zip_reader_extract_to_file(&zip_archive, i, file_path.c_str(), 0)) {
				std::cerr << "Failed to extract file: " << file_path << std::endl;
				mz_zip_reader_end(&zip_archive);
				return false;
			}
		}
	}

	mz_zip_reader_end(&zip_archive);
	return true;
}
#endif

// 解析URL并提取协议、主机、端口和路径
bool parse_url(const std::string& url, std::string& protocol, std::string& host, std::string& port, std::string& path) {
	std::regex url_regex(R"((http|https)://([^/:]+)(:([0-9]+))?(/.*)?)");
	std::smatch url_match_result;

	if (std::regex_match(url, url_match_result, url_regex)) {
		protocol = url_match_result[1].str();
		host = url_match_result[2].str();
		port = url_match_result[4].str().empty() ? (protocol == "https" ? "443" : "80") : url_match_result[4].str();
		path = url_match_result[5].str().empty() ? "/" : url_match_result[5].str();
		return true;
	}
	else {
		return false;
	}
}

struct mg_http_data {
	std::string head;
	std::string body;
	bool done = false;
	int status = 0;

	void reset() {
		head.clear();
		body.clear();
		done = false;
		status = 0;
	}
};

static void mg_connect_fn(struct mg_connection* connect, int ev, void* ev_data) {
	mg_http_data* data = (mg_http_data*)connect->fn_data;
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;

		data->head.assign(hm->head.ptr, hm->head.len);
		data->body.assign(hm->body.ptr, hm->body.len);
		data->status = mg_http_status(hm);

		data->done = true;
		connect->is_closing = 1;
	}
	else if (ev == MG_EV_ERROR) {
		data->done = true;
		connect->is_closing = 1;
	}
}

bool g_bTdsUpgradeThreadRunning = false;
void thread_tds_upgrade(string packageUrl,string packageType) {
#ifdef _WIN32
	LOG("[warn]startServerUpgrade,升级线程开始");
	g_bTdsUpgradeThreadRunning = true;

	std::string protocol, host, port, path, ip, url;
	if (parse_url(packageUrl, protocol, host, port, path)) {
		std::string hostTmp = host;

		size_t pos = hostTmp.find("://");
		if (pos != std::string::npos) {
			hostTmp = hostTmp.substr(pos + 3);
		}

		pos = hostTmp.find(":");
		if (pos != std::string::npos) {
			hostTmp = hostTmp.substr(0, pos);
		}

		ip = hostTmp;
		url = "http://" + ip + ":" + port + path;

		struct mg_mgr mgr;
		mg_mgr_init(&mgr);

		mg_http_data data;
		struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

		if (connect) {
			mg_printf(connect,
				"GET %s HTTP/1.0\r\n"
				"Host: %s\r\n"
				"Connection: close\r\n"
				"\r\n",
				path.c_str(), ip.c_str()
			);

			TIME tStart = timeopt::now();
			while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
				mg_mgr_poll(&mgr, 100);
			}
		}

		mg_mgr_free(&mgr);

		if (data.status == 200) {
			//保存升级包
			fs::createFolderOfPath("../packages");

			string fileName = getFileNameFromURL(packageUrl);
			string packagePath = fs::toAbsolutePath("../packages/") + "/" + fileName;

			if (!fs::writeFile(packagePath.c_str(), data.body.data(), data.body.size())) {
				json j;
				j["serverUpgradeStatus"] = "保存升级包失败," + packagePath;

				rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
				LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

				goto UPGRADE_END;
			}

			string tdsPath = fs::toAbsolutePath("./"); //末尾不带斜杠

			//删除重命名文件
			string appName = fs::appName();
			string tmpExeName = appName + "_old.exe";
			string tmpUIName = "ui_old";

			if (fs::fileExist(tdsPath + "/" + tmpExeName)) {
				if (!fs::deleteFile(tdsPath + "/" + tmpExeName)) {
					json j;
					j["serverUpgradeStatus"] = "删除文件失败," + tdsPath + "/" + tmpExeName;

					rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
					LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

					goto UPGRADE_END;
				}
			}

			if (fs::fileExist(tdsPath + "/" + tmpUIName)) {
				if (!fs::deleteFolder(tdsPath + "/" + tmpUIName)) {
					json j;
					j["serverUpgradeStatus"] = "删除文件失败," + tdsPath + "/" + tmpUIName;

					rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
					LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

					goto UPGRADE_END;
				}
			}

			//重命名当前版本
			string exeName = fs::appName() + ".exe";
			string uiName = "ui";

			if (!renameFile(tdsPath + "/", exeName, tmpExeName)) {
				json j;
				j["serverUpgradeStatus"] = "重命名文件失败," + exeName;

				rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
				LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

				goto UPGRADE_END;
			}

			//向tKeep发送请求,停止服务,然后来进行文件替换
			json jReq;
			jReq["method"] = "suspendGuard";
			jReq["params"] = "";

			string keepIP = "127.0.0.1";
			string keepPort = "6007";
			string keepPath = "/rpc";
			string keepUrl = "http://" + keepIP + ":" + keepPort + keepPath;
			string keepBody = jReq.dump();

			struct mg_mgr keepMgr;
			mg_mgr_init(&keepMgr);

			mg_http_data keepData;
			struct mg_connection* keepConnect = mg_http_connect(&keepMgr, keepUrl.c_str(), mg_connect_fn, &keepData);

			if (keepConnect) {
				mg_printf(connect,
					"POST %s HTTP/1.0\r\n"
					"Host: %s\r\n"
					"Content-Type: application/json\r\n"
					"Content-Length: %u\r\n"
					"\r\n"
					"%s",
					keepPath.c_str(), keepIP.c_str(), (unsigned int)keepBody.size(), keepBody.c_str()
				);

				TIME keepStart = timeopt::now();
				while (!keepData.done && timeopt::calcTimePassMilliSecond(keepStart) / 1000.0 < 10.0) {
					mg_mgr_poll(&keepMgr, 100);
				}
			}			

			mg_mgr_free(&keepMgr);

			if (keepData.status == 200) {
				json result = json::parse(keepData.body);
				if (result["error"] != nullptr) {
					json j;
					j["serverUpgradeStatus"] = "tKeep停止服务失败";

					rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
					LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

					goto UPGRADE_END;
				}
				else if (result["result"] != nullptr) {
					LOG("[服务升级]tKeep停止服务成功");
				}
			}
			else {
				json j;
				j["serverUpgradeStatus"] = "向tKeep请求超时,请确认tKeep版本或运行情况";

				rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
				LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

				goto UPGRADE_END;
			}

			string extractPath = tdsPath;
			if (packageType == "complete") {
				extractPath = fs::toAbsolutePath("../");
			}

			//解压缩包到程序路径.miniz使用utf8路径
			if (!extract_zip(packagePath.c_str(), extractPath.c_str())) {
				json j;
				j["serverUpgradeStatus"] = "解压缩升级包失败," + packagePath + "->" + extractPath;

				rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
				LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

				goto UPGRADE_END;
			}

			//升级结束后检查是否存在 tds.exe，否则tds启动不了，目标机器就断了
			if (!fs::fileExist(tdsPath + "/" + exeName)) {
				json j;
				j["serverUpgradeStatus"] = "压缩包中未包含" + exeName;

				rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
				LOG("[warn]升级失败," + j["serverUpgradeStatus"]);

				if (!renameFile(tdsPath + "/", tmpExeName,exeName)) {
					json j;
					j["serverUpgradeStatus"] = "压缩包中未包含" + exeName + ",恢复重命名文件失败, " + tmpExeName;

					rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
					LOG("[warn]升级失败," + j["serverUpgradeStatus"]);
				}

				goto UPGRADE_END;
			}

			//向tKeep发送请求,重新开启服务
			jReq["method"] = "beginGuard";
			jReq["params"] = "";

			keepIP = "127.0.0.1";
			keepPort = "6007";
			keepPath = "/rpc";
			keepUrl = "http://" + keepIP + ":" + keepPort + keepPath;
			keepBody = jReq.dump();

			mg_mgr_init(&keepMgr);

			keepData.reset();
			keepConnect = mg_http_connect(&keepMgr, keepUrl.c_str(), mg_connect_fn, &keepData);

			if (keepConnect) {
				mg_printf(connect,
					"POST %s HTTP/1.0\r\n"
					"Host: %s\r\n"
					"Content-Type: application/json\r\n"
					"Content-Length: %u\r\n"
					"\r\n"
					"%s",
					keepPath.c_str(), keepIP.c_str(), (unsigned int)keepBody.size(), keepBody.c_str()
				);

				TIME keepStart = timeopt::now();
				while (!keepData.done && timeopt::calcTimePassMilliSecond(keepStart) / 1000.0 < 10.0) {
					mg_mgr_poll(&keepMgr, 100);
				}
			}

			mg_mgr_free(&keepMgr);

			if (data.status == 200) {
				LOG("[服务升级]tKeep重新开启服务成功");
			}

			//返回升级成功状态
			{
				json j;
				j["serverUpgradeStatus"] = "升级成功,等待重启," + timeopt::nowStr(false);

				rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
				LOG("[warn]" + j["serverUpgradeStatus"]);
			}

			//退出程序，等待tKeep重启
			tds->stop();
			exit(0);
		}
		else {
			json j;
			j["serverUpgradeStatus"] = "下载升级包失败," + packageUrl;

			rpcSrv.notify("onServerUpgradeStatusChange", j.dump());
			LOG("[warn]startServerUpgrade,下载升级包失败");
		}
	}

UPGRADE_END:
	g_bTdsUpgradeThreadRunning = false;
	LOG("[warn]startServerUpgrade,升级线程结束");
#endif
}

bool rpcHandler::handleMethodCall_edgeDev(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
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
		OBJ_PROP_SEL query;
		query.getConf = false;
		query.getStatus = true;
		query.getChild = true;
		query.getMp = true;

		YY_OBJ_VAL yyObj;
		prj.toJson(yyObj, query, nullptr, session.user);
		result = yyObj.dump();
	}
	else if (method == "upgradeTds" || method == "startServerUpgrade") {
		string packageUrl = params["packageUrl"];
		string packageType = "main";
		if (params["packageType"]!=nullptr) packageType = params["packageType"];
		LOG("[warn]startServerUpgrade,升级包地址:" + packageUrl);
		if (packageUrl == "") {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL,"未传入有效的升级包地址");
			LOG("[warn]startServerUpgrade,未传入有效的升级包地址");
		}
		else {
			if (g_bTdsUpgradeThreadRunning) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "升级正在进行中");
				LOG("[warn]startServerUpgrade,升级正在进行中");
			}
			else {
				thread t(thread_tds_upgrade, packageUrl, packageType);
				t.detach();
				rpcResp.result = RPC_OK;
			}
		}


	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}

bool rpcHandler::handleMethodCall_gamePad(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method.find("gamepad")!= string::npos)
	{
		json p;
		p["user"] = session.user;
		rpcSrv.notify(method, p.dump());
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}

string getRefCurvePath(json& params) {
	string name;
	if (params.contains("name"))
		name = params["name"];
	string tag = params["tag"];

	string path = tds->conf->confPath + "/refCurve/";
	string subPath = str::replace(tag, ".", "/");
	path += subPath;
	if (name.size() > 0) {
		path += "/" + name + ".json";
	}
	else {
		path += "/refCurve.json";
	}

	return path;
}

bool rpcHandler::handleMethodCall_utils(const string& method, yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	bool bHandled = true;

	if (method == "generateWebRtcKeyPair") {
		// 在配置目录的 webRtcKey 子目录下生成私钥和证书
		std::string keyDir = tds->conf->confPath + "/webRtcKey";
		std::string certPath = keyDir + "/cert.pem";
		std::string keyPath = keyDir + "/key.pem";

		// 创建目录（如不存在）
		fs::createFolderOfPath(keyDir + "/.placeholder");

		// 生成 ECDSA P-256 自签证书
		string certPem, keyPem;
		DtlsTransport::generateSelfSignedCert("TDS WebRTC Server", certPem, keyPem);

		if (certPem.empty() || keyPem.empty()) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "Certificate generation failed");
			return true;
		}

		// 写入证书文件
		bool certOk = fs::writeFile(certPath,certPem);
		bool keyOk = fs::writeFile(keyPath, keyPem);

		if (!certOk || !keyOk) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "Failed to write key files");
			return true;
		}

		// 计算指纹用于确认
		mbedtls_x509_crt cert;
		mbedtls_x509_crt_init(&cert);
		std::string fingerprint;
		int ret = mbedtls_x509_crt_parse(&cert,
			(const unsigned char*)certPem.c_str(), certPem.size() + 1);
		if (ret == 0) {
			fingerprint = DtlsTransport::getFingerprint(cert);
		}
		mbedtls_x509_crt_free(&cert);

		json jResult;
		jResult["status"] = "ok";
		jResult["dir"] = keyDir;
		jResult["fingerprint"] = fingerprint;
		rpcResp.result = jResult.dump();

		LOG("[WebRTC] generateWebRtcKeyPair: cert=%s, key=%s, fingerprint=%s",
			certPath.c_str(), keyPath.c_str(), fingerprint.c_str());

		return true;
	}

	return false;
}

bool rpcHandler::handleMethodCall_MoMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	string& result = rpcResp.result;
	string& error = rpcResp.error;

	bool bHandled = true;

	//配置的使用与配置的修改之间不允许并发。使用读写锁保护
	if (method == "setRefCurve") {
		string tag = params["tag"];
		string path = getRefCurvePath(params);
		fs::createFolderOfPath(path);

		if (params.contains("curve")) {
			json& curve = params["curve"];
			if (!curve.is_object()) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL,"curve should be an object");
				return true;
			}
			string s = curve.dump(2);
			fs::writeFile(path, s);
		}
		else if(params.contains("curveIdx")){
			json jcurveIdx = params["curveIdx"];
			if (!jcurveIdx.is_string()) {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "curveIdx should be a string");
				return true;
			}
			string curveIdx = jcurveIdx.get<string>();
			string src = db.getPath_dbFile(tag, curveIdx, "curve");
			string s;
			fs::readFile(src, s);
			if (str::isGB2312(s)) {
				s = charCodec::gb_to_utf8(s);
			}
			fs::writeFile(path, s);
		}

		rpcResp.result = RPC_OK;
	}
	else if (method == "getRefCurve") {
		string path = getRefCurvePath(params);
		string s;
		fs::readFile(path, s);
		if (s != "") {
			params["curve"] = json::parse(s);
			rpcResp.result = params.dump();
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "ref curve not found");
		}
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
	else if (method == "setObjAttr") {
		prj.loadConf(params, false);
		prj.saveConfFile();
		rpcResp.result = RPC_OK;
		std::thread(updateStreamNodeConfig).detach();
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
	else if (method == "onObjOnline" || method == "objOnline" || method == "setOnline") {
		rpc_onObjOnline(params,rpcResp,session);
	}
	else if (method == "onObjOffline" || method == "objOffline" || method == "setOffline") {
		rpc_onObjOffline(params, rpcResp, session);
	}
	else {
		shared_lock<shared_mutex> lock(prj.m_csPrj);

		//以下配置使用 mo conf 和 io conf
		if (method == "input") {
			rpc_input(params, rpcResp, session);
		}
		else if (method == "output") {
			rpc_output(params, rpcResp,session);
		}
		else if (method == "getMoOnlineStatus") {//智能设备在线状态
			result = rpc_getMoOnlineStatus(params, error);
		}
		else if (method == "getMoStatis") {
			rpc_getMoStatis(params, rpcResp, session);
		}
		else if (method == "getObjStatis") {
			rpc_getObjStatis(params, rpcResp, session);
		}
		else if (method == "getMpStatis") {
			rpc_getMpStatis(params, rpcResp, session);
		}
		else if (method == "getMoAttri" || method == "getMoAttr") {
			rpc_getMoAttr_list(params, rpcResp, session);
		}
		else if (method == "getconf") {
			result = rpc_getconf(params, error);
		}
		else if (method == "getMpTypeList") {
			json list;
			prj.getMpTypeList(list);
			result = list.dump();
		}
		else if ( method == "getCustomTypes") {
			OBJ* pmo = nullptr;
			if (params != nullptr && params.contains("tag"))
			{
				string tag = params["tag"].get<string>();
				tag = TAG::addRoot(tag, session.org);

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
				db.rpc_db_select(sParams, err, rlt, queryInfo,"",session.language);

				if (rlt != "") {
					json j = json::parse(rlt);
					dbSum = j["val"].get<double>();
				}
			}
			//对所有位号的当前值聚合,相当于 time=last
			else {
				for (int i = 0; i < mpList.size(); i++) {
					MP* pmp = mpList[i];
					if (JSON_STR::is_num(pmp->m_curVal)) {
						double val = JSON_STR::get_num(pmp->m_curVal);
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
				if (JSON_STR::is_num(pmp->m_curVal)) {
					double val = JSON_STR::get_num(pmp->m_curVal);
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
		else {
			bHandled = false;
		}
	}

	return bHandled;
}


bool rpcHandler::handleMethodCall_alarmMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	return almSrv.handleRpc(method, params, rpcResp, session);
}

bool rpcHandler::handleMethodCall_userMng(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
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
	else if (method == "setUser")
	{
		userMng.rpc_setUser(params,rpcResp,session);
	}
	else if (method == "setUsers")
	{
		userMng.rpc_setUsers(params, rpcResp, session);
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


void createObjRecursive(string path, OBJ* pParent) {
	vector<fs::FILE_INFO>  folderList;
	fs::getFolderList(folderList, path);
	for (int i = 0; i < folderList.size(); i++) {
		string folderName = folderList[i].name;
		OBJ* pC = new OBJ();
		pC->m_name = folderName;
		createObjRecursive(path + "/" + folderName, pC);

		if (pC->m_childObj.size() == 0) //pC为监控点
		{
			delete pC;
			MP* pmp = new MP();
			pmp->m_name = folderName;
			pParent->m_childObj.push_back(pmp);
		}
		else {
			pParent->m_childObj.push_back(pC);
		}
	}
}

bool rpcHandler::handleMethodCall_unclassified(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	bool bHandled = true;

	//可完全并发的命令
	if (method == "xiaot")
	{
		rpcResp.result = tds->xiaoT->getReply(params);
	}
	//else if (method == "getScriptMngerStatus") {
	//	scriptManager.rpc_getScriptMngStatus(params, rpcResp, session);
	//}
	else if (method == "subNotify") {
		LOG("[warn]subNotify,%s,remoteAddr=%s:%d", params.dump().c_str(), session.remoteIP.c_str(), session.remotePort);

		json j = params["method"];
		if (j.is_string()) {
			string m = j.get<string>();
			session.subTag.push_back(m);
			if (m == "*") {
				session.subAllMethod = true;
			}
		}
		else if (j.is_array()) {
			for (auto& i : j) {
				session.subMethod.push_back(i.get<string>());
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "param method should be a string or an array of strings");
			return true;
		}

		if (params.contains("rootTag")) {
			j = params["rootTag"];
			if (j.is_array()) {
				for (auto& i : j) {
					session.subRootTag.push_back(i.get<string>());
				}
			}
			else {
				rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "param rootTag should be a string or an array of strings");
				return true;
			}
		}
		
		j = params["tag"];
		if (j.is_string()) {
			string tag = j.get<string>();
			session.subTag.push_back(tag);
			if (tag == "*") {
				session.subAllTag = true;
			}
		}
		else if (j.is_array()) {
			for (auto& i : j) {
				session.subTag.push_back(i.get<string>());
			}
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "param rootTag should be a string or an array of strings");
			return true;
		}

		rpcResp.result = RPC_OK;
	}
	else if (method == "verifySignature") {
		string signStr = params["signStr"];
		string signature = params["signature"];
		string publicKey = 
			"-----BEGIN PUBLIC KEY-----\n"
			"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA3Sc9JJ926Qk035DOPZ7C\n"
			"zgXd57RyEcZGJzGuGGr93+b85SFcmAYTli643Y0xfJ8wTPiHYoSFeckzQtWcmYso\n"
			"hCSleMMVul/70ZBATY1esO7mhYqPemmREDTIHobDeQcrer4Lsjeoet2f4Ttf8+Rt\n"
			"rAmtv9rO9AUTuy4o2e8XKxRD51bYiLVQsiZBBmRKkcrp+Sy7UyFD3CdEmAg6dyeS\n"
			"w2l8PvWaFhtUdVRzbqeld3NTKOVefvUYJPtEfrXyk7yhTNPgBTk6fzwDhZqIZriH\n"
			"a0+Qo1EjeD64Guha7TV+/mG50Uun+57snNbNEvdceo3lksH7Vi+6QUIU05Dq/VXl\n"
			"cQIDAQAB\n"
			"-----END PUBLIC KEY-----";
	
		rsa2048_public_key_t pKey;
		rsa_verify_result_t rlt = rsa2048_load_public_key_from_string(&pKey, publicKey.c_str());
		if (rlt != RSA_VERIFY_SUCCESS) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, getVerifyRltInfo(rlt));
			return true;
		}
		rlt = rsa2048_verify_signature_base64(&pKey, (uint8_t*)signStr.data(), signStr.length(), signature.c_str());
		if (rlt != RSA_VERIFY_SUCCESS) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, getVerifyRltInfo(rlt));
			return true;
		}
		rpcResp.result = "\"ok\"";
		return true;
	}
	else if (method == "time2unix") {   //跨国项目，测试时间造成的一些问题
		string st = params["time"];
		json j;
		TIME dbt;
		dbt.fromStr(st);
		j["unix"] = timeopt::SysTime2Unix(dbt);
		rpcResp.result = j.dump();
	}
	else if (method == "unix2time") {
		time_t unix_time = params["unix"].get<time_t>();
		json j;
		TIME dbt;
		dbt.fromUnixTime(unix_time);
		j["time"] = dbt.toStr();
		rpcResp.result = j.dump();
	}
	else if (method == "calcTimeDiffSecond") {
		string s1 = params["time1"];
		string s2 = params["time2"];
		TIME t1; t1.fromStr(s1);
		TIME t2; t2.fromStr(s2);
		time_t diff = timeopt::CalcTimeDiffSecond(t1, t2);
		json j;
		j = diff;
		rpcResp.result = j.dump();
	}
	else if (method == "generateObjTreeFromDB") {
		string path = params["path"];
		prj.clear();
		createObjRecursive(path, &prj);
		rpcResp.result = RPC_OK;
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
	else if (method == "enableIOSession") {
		string ip = params["ip"];
		int port = params["port"].get<int>();
		shared_ptr<TDS_SESSION> p = ioSrv.getIOSession(ip,port);
		p->m_bEnableIO = true;
		if (p->pTcpSession) {
			p->pTcpSession->bEnable = true;
		}
		if (p->pTcpSessionClt) {
			p->pTcpSessionClt->bEnable = true;
		}
		rpcResp.result = "\"ok\"";
	}
	else if (method == "disableIOSession") {
		string ip = params["ip"];
		int port = params["port"].get<int>();
		shared_ptr<TDS_SESSION> p = ioSrv.getIOSession(ip, port);
		p->m_bEnableIO = false;
		if (p->pTcpSession) {
			p->pTcpSession->bEnable = false;
		}
		if (p->pTcpSessionClt) {
			p->pTcpSessionClt->bEnable = false;
		}
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
#ifdef ENABLE_QJS
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
		rpc_startStreamPush(params, rpcResp, session);
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
		string s = params.dump();
		tds->smsServer->send(s, phoneNum);
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

		auto pReadFun = [](json &jRlt, const string &confName) -> void
		{
			string sVal = tdsConf.project_ini.getValStr(confName, "查找失败");
			if (sVal != "查找失败")
			{
				if (sVal == "") {
					jRlt[confName] = sVal;
				}
				else if (str::isInteger(sVal)) {
					jRlt[confName] = atoi(sVal.c_str());
				}
				else {
					jRlt[confName] = sVal;
				}
			}
		};
		if (params.is_string()) {
			string confName = params.get<string>();
			if (confName == "*" || confName == "") {
				json jsconf;
				vector<KV_INI_LINE>& maps = tdsConf.project_ini.mapConf;
				bool exist = false;
				for (auto& line : maps) {
					if (line.type == CONF_ITEM) {
						jsconf[line.key] = line.val;
					}
				}
				rpcResp.result = jsconf.dump();
			}
			else 
			{
				json jRlt;
				pReadFun(jRlt, confName);

				if (!jRlt.is_null())
				{
					rpcResp.result = jRlt.dump();
				}
				else 
				{
					rpcResp.result = RPC_NULL;
				}
			}
		}
		else if (params.is_array())
		{
			json jRlt;
			for (int i = 0; i < params.size(); i++)
			{
				string confName = params[i].get<string>();
				pReadFun(jRlt, confName);
			}
			if (!jRlt.is_null())
			{
				rpcResp.result = jRlt.dump();
			}
			else
			{
				rpcResp.result = RPC_NULL;
			}
		}
		else {
			json jsconf;
			vector<KV_INI_LINE>& maps = tdsConf.project_ini.mapConf;
			bool exist = false;
			for (auto& line : maps) {
				if (line.type == CONF_ITEM) {
					jsconf[line.key] = line.val;
				}
			}
			rpcResp.result = jsconf.dump();
		}
	}
	else if (method == "setProjectConf") {	 
		for (auto itm: params.items())
		{
			const string& key = itm.key();

#ifdef ENABLE_ALM_SRV_HOOK_SCRIPT
			if (key == "scriptBeforeUpdateAlarm") {
				almSrv.m_scriptBeforeUpdateAlarm = itm.value().get<string>();
			}
#endif

			if (itm.value().is_string())
			{
				tds->conf->setStr(key, itm.value().get<string>());
			}
			else
			{
				tds->conf->setInt(key, itm.value().get<int>());
			}
		}
		rpcResp.result = RPC_OK;
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
	else if (method == "CalDTW") {
		//参数错误直接返回空数组了
		/*
	   "params": {
		  "tag": "5#.导曲轨段1.多频曲线S",
		  "time": ["2020-10-10 10:10:10", "2020-10-10 10:10:11", "2020-10-10 10:10:12"], 
		  或
		   "time": [["2020-10-10 10:10:10", "2020-10-10 10:10:11"],["2020-10-10 10:10:14", "2020-10-10 10:10:16"],...]
	   },
		*/
		string res0;
		int _once = 1; while (_once--) {
			string tag = params["tag"];
			json& timeAry = params["time"];
			string type ="dtw";
			if(params.contains("type"))
				type = params["type"];//dtw、 aes
			int si = timeAry.size();
			bool  hasSubAry = true;
			if (si > 0) {
				if (timeAry[0].is_string())
					hasSubAry = false;
				//timeAry[0].is_array()
			}
			else {
				res0 = "[]";
				break;
			}
			if (!hasSubAry) {
				vector<json> vecCurves;
				if (si == 1) {
					res0 = "[]";
					break;
				}
				for (int i = 0; i < si; i++) {
					string ti = timeAry[i];
					string path = db.getPath_dbFile(tag, ti, "curve");
					//DB_FS::readFile(path, data);

					//string apiJsonPath = fs::appPath() + "/blacklist.api.json";
					string strJo;
					fs::readFile(path, strJo);
					json jsFile = json::parse(strJo);

					vecCurves.push_back(jsFile);
				}
				json list1 = json::array();
				for (int i = 0; i < si - 1; i++) {
					json& curve1 = vecCurves[i];
					json& curve2 = vecCurves[i + 1];

					//curve2["data"].size();

					vector<double> p1, p2;
					jsonToList(curve1, p1);
					jsonToList(curve2, p2);

					float dis = -1;
					if(type == "dtw")
						dis = CalDTWDist(p1, p2);
					else if (type == "aes" || type == "aea") {
						double sum = 0;
						for (int i = 0; i < p1.size() && i < p2.size(); i++) {
							sum += abs(p1[i] - p2[i]);
						}
						if (type == "aea")
						{
							if (p1.size() > 0 && p2.size() > 0)
								dis = sum * 1.0 / MIN(p1.size(), p2.size());
						}
						else
							dis = sum;
					}
					
					json one;
					one["time"] = curve2["time"];
					one["val"] = to_string(dis);

					list1.push_back(one);
				}
				res0 = list1.dump();
			}
			else {
				vector<vector<json>> vecCurves;
				for (int i = 0; i < si; i++) {
					json& subAry = timeAry[i];
					if (subAry.size() != 2) {
						res0 = "[]";
						break;
					}
					vector<json> vecOne;
					for (int j = 0; j < subAry.size(); j++) {
						string ti = subAry[j];
						string path = db.getPath_dbFile(tag, ti, "curve");
						string strJo;
						fs::readFile(path, strJo);
						json jsFile = json::parse(strJo);
						vecOne.push_back(jsFile);
					}
					vecCurves.push_back(vecOne);
				}
				json list1 = json::array();
				for (int i = 0; i < si; i++) {
					json& curve1 = vecCurves[i][0];
					json& curve2 = vecCurves[i][1];

					vector<double> p1, p2;
					jsonToList(curve1, p1);
					jsonToList(curve2, p2);
					float dis = -1;
					if (type == "dtw")
						dis = CalDTWDist(p1, p2);
					else if (type == "aes" || type == "aea") {
						double sum = 0;
						for (int i = 0; i < p1.size() && i < p2.size(); i++) {
							sum += abs(p1[i] - p2[i]);
						}
						if (type == "aea")
						{
							if (p1.size() > 0 && p2.size() > 0)
								dis = sum * 1.0 / MIN(p1.size(), p2.size());
						}
						else
							dis = sum;
					}


					json one;
					one["time"] = curve2["time"];
					one["val"] = to_string(dis);

					list1.push_back(one);
				}
				res0 = list1.dump();
			}
		}
		rpcResp.result = res0;
	}
	else if (method == "getSystemInfo")
	{
		json resultObj;
		resultObj["tdsVersion"] = tds->getSvnVersion();

		resultObj["computerStartupTime"] = g_ComputerStartupTime.c_str();
		resultObj["systemStartupTime"] = g_strStartupTime.c_str();
		#ifdef _WIN32
		resultObj["cpu"] = statusSrv.m_srvStatus.cpu;
		resultObj["mem"] = statusSrv.m_srvStatus.mem;
		resultObj["handle"] = statusSrv.m_srvStatus.handle;
		resultObj["thread"] = statusSrv.m_srvStatus.thread;
		#else
		// TODO: add Linux
		resultObj["cpu"] = 0.0;
		resultObj["mem"] =  0.0;
		resultObj["handle"] =  0;
		resultObj["thread"] = 0;
		#endif
		rpcResp.result = resultObj.dump();
	}
	else if (method == "imageReco") {
		string tag = params["tag"];
		string strTime = params["time"];

		string imagePath = db.getPath_dbFile(tag, strTime, "image");
		if (db.fileExist(imagePath)) {
			//if (dv_predict)
			//{
			//	string imageBuff = "";
			//	DB_FS::readFile(imagePath, imageBuff);
			//	size_t temp = imageBuff.size();
			//	string strFileEnd = imageBuff.substr(imageBuff.size() - 4, 4);
			//	//judge xxxxxx.image.jpg file inside,info does it exist
			//	if (strFileEnd == "jjpg")
			//	{
			//		string strJsonSize = imageBuff.substr(imageBuff.size() - 8, 4);
			//		size_t jsonSize = 0;
			//		memcpy(&jsonSize, strJsonSize.c_str(), 4);
			//		imageBuff = imageBuff.substr(0, imageBuff.size() - jsonSize - 8);
			//	}

			//	string info = "";
			//	char* c_info = new char[1000000];
			//	dv_predict((const unsigned char*)imageBuff.c_str(), imageBuff.size(), c_info);
			//	info = c_info;
			//	delete[] c_info;

			//	if (!info.empty())
			//	{
			//		auto doc_info = yyjson_read(info.c_str(), info.size(), 0);
			//		auto root_info = yyjson_doc_get_root(doc_info);

			//		auto mut_doc_info = yyjson_mut_doc_new(nullptr);
			//		auto mut_root_info = yyjson_mut_obj(mut_doc_info);
			//		yyjson_mut_doc_set_root(mut_doc_info, mut_root_info);

			//		yyjson_mut_obj_add_strcpy(mut_doc_info, mut_root_info, "tag", tag.c_str());
			//		yyjson_mut_obj_add_strcpy(mut_doc_info, mut_root_info, "time", strTime.c_str());
			//		auto objContourArr = yyjson_mut_arr(mut_doc_info);
			//		yyjson_mut_obj_add_val(mut_doc_info, mut_root_info, "objContour", objContourArr);

			//		auto objectsObj = yyjson_obj_get(root_info, "objects");
			//		yyjson_val* val;
			//		size_t indx = 0, max = 0;
			//		yyjson_arr_foreach(objectsObj, indx, max, val)
			//		{
			//			auto oneObjContour = yyjson_mut_obj(mut_doc_info);
			//			auto nameObj = yyjson_obj_get(val, "name");
			//			yyjson_mut_obj_add_val(mut_doc_info, oneObjContour, "type", yyjson_val_mut_copy(mut_doc_info, nameObj));
			//			auto confidenceObj = yyjson_obj_get(val, "confidence");
			//			yyjson_mut_obj_add_val(mut_doc_info, oneObjContour, "confidence", yyjson_val_mut_copy(mut_doc_info, confidenceObj));

			//			auto bboxArr = yyjson_mut_arr(mut_doc_info);
			//			yyjson_mut_obj_add_val(mut_doc_info, oneObjContour, "bbox", bboxArr);
			//			auto bbox_yuanObj = yyjson_obj_get(val, "bbox");
			//			auto bbox_x_yuanObj = yyjson_obj_get(bbox_yuanObj, "x");
			//			yyjson_mut_arr_add_val(bboxArr, yyjson_val_mut_copy(mut_doc_info, bbox_x_yuanObj));
			//			auto bbox_y_yuanObj = yyjson_obj_get(bbox_yuanObj, "y");
			//			yyjson_mut_arr_add_val(bboxArr, yyjson_val_mut_copy(mut_doc_info, bbox_y_yuanObj));
			//			auto bbox_w_yuanObj = yyjson_obj_get(bbox_yuanObj, "w");
			//			yyjson_mut_arr_add_val(bboxArr, yyjson_val_mut_copy(mut_doc_info, bbox_w_yuanObj));
			//			auto bbox_h_yuanObj = yyjson_obj_get(bbox_yuanObj, "h");
			//			yyjson_mut_arr_add_val(bboxArr, yyjson_val_mut_copy(mut_doc_info, bbox_h_yuanObj));
			//			auto maskObj = yyjson_obj_get(val, "mask");
			//			yyjson_mut_obj_add_val(mut_doc_info, oneObjContour, "mask", yyjson_val_mut_copy(mut_doc_info, maskObj));

			//			yyjson_mut_arr_add_val(objContourArr, oneObjContour);
			//		}

			//		char* temp = yyjson_mut_write(mut_doc_info, 0, 0);
			//		info = temp;
			//		delete temp;
			//		yyjson_doc_free(doc_info);
			//		yyjson_mut_doc_free(mut_doc_info);
			//	}
			//	rpcResp.result = info;
			//}
			//else
			//{
			//	rpcResp.error = R"("onnx未加载或加载失败,请检查")";
			//}
		}
		else {
			rpcResp.error = R"("cannot find file, imagePath:)" + imagePath+R"(")";
		}
	}
	else if (method == "getSimuConf") {
		// 创建 yyjson_mut_doc 和根数组
		yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* mut_arr = yyjson_mut_arr(mut_doc);

		// 获取所有 MP 对象
		map<string, MP*> mapAllMP;
		prj.getMpList(mapAllMP);

		// 遍历所有 MP 对象
		for (map<string, MP*>::iterator it = mapAllMP.begin(); it != mapAllMP.end(); ++it) {
			MP* pmp = it->second;
			if (pmp) {
				// 创建 JSON 对象
				yyjson_mut_val* obj = yyjson_mut_obj(mut_doc);

				// 添加 tag、lowLimit、highLimit 和 enable 到 JSON 对象
				yyjson_mut_obj_add_strcpy(mut_doc, obj, "tag", it->first.c_str());
				yyjson_mut_obj_add_real(mut_doc, obj, "lowLimit", pmp->m_simuConf.lowLimit);
				yyjson_mut_obj_add_real(mut_doc, obj, "highLimit", pmp->m_simuConf.highLimit);
				yyjson_mut_obj_add_bool(mut_doc, obj, "enable", pmp->m_simuConf.enable);
				yyjson_mut_obj_add_int(mut_doc, obj, "interval", pmp->m_simuConf.interval);

				// 将对象添加到数组
				yyjson_mut_arr_add_val(mut_arr, obj);
			}
		}

		// 设置根节点
		yyjson_mut_doc_set_root(mut_doc, mut_arr);

		// 将 JSON 转换为字符串
		size_t len;
		char* json_str = yyjson_mut_write(mut_doc, YYJSON_WRITE_PRETTY, &len);
		if (json_str) {
			rpcResp.result = json_str; // 将 JSON 字符串赋值给 rpcResp.result
			free(json_str);           // 释放 JSON 字符串内存
		}
		else {
			rpcResp.error = "Failed to generate JSON string.";
		}

		// 释放 yyjson_mut_doc
		yyjson_mut_doc_free(mut_doc);
	}
	else if (method == "setSimuConf") {
		for (int i = 0; i < params.size(); i++) {
			string tag = params[i]["tag"];
			double lowLimit = params[i]["lowLimit"];
			double highLimit = params[i]["highLimit"];
			bool enable = params[i]["enable"];
			int interval = params[i]["interval"];

			// 写入监控点配置
			MP* pmp = prj.GetMPByTag(tag, "zh");
			if (pmp) {
				pmp->m_simuConf.lowLimit = lowLimit;
				pmp->m_simuConf.highLimit = highLimit;
				pmp->m_simuConf.enable = enable;
				pmp->m_simuConf.interval = interval;
			}
		}

		yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(nullptr);
		yyjson_mut_val* mut_arr = yyjson_mut_arr(mut_doc);

		// 获取所有 MP 对象
		map<string, MP*> mapAllMP;
		prj.getMpList(mapAllMP);

		// 遍历所有 MP 对象
		for (map<string, MP*>::iterator it = mapAllMP.begin(); it != mapAllMP.end(); ++it) {
			MP* pmp = it->second;
			if (pmp) {
				// 创建 JSON 对象
				yyjson_mut_val* obj = yyjson_mut_obj(mut_doc);

				// 添加 tag、lowLimit、highLimit 和 enable 到 JSON 对象
				yyjson_mut_obj_add_strcpy(mut_doc, obj, "tag", it->first.c_str());
				yyjson_mut_obj_add_real(mut_doc, obj, "lowLimit", pmp->m_simuConf.lowLimit);
				yyjson_mut_obj_add_real(mut_doc, obj, "highLimit", pmp->m_simuConf.highLimit);
				yyjson_mut_obj_add_bool(mut_doc, obj, "enable", pmp->m_simuConf.enable);
				yyjson_mut_obj_add_int(mut_doc, obj, "interval", pmp->m_simuConf.interval);

				// 将对象添加到数组
				yyjson_mut_arr_add_val(mut_arr, obj);
			}
		}

		yyjson_mut_doc_set_root(mut_doc, mut_arr);

		size_t len;
		char* p = yyjson_mut_val_write(mut_arr, YYJSON_WRITE_PRETTY_NO_SPACES | YYJSON_WRITE_PRETTY, &len);
		if (p) {
			fs::writeFile(tds->conf->confPath + "/DataSimu.json", p, len);
			free(p);
		}

		yyjson_mut_doc_free(mut_doc);

		rpcResp.result = RPC_OK;
	}
	else {
		bHandled = false;
	}

	return bHandled;
}

void jsonToList(json& curve, vector<double>& p)
{
	json& list = curve["data"];
	int si = list.size();
	for (int i = 0; i < si; i++) {
		json &aa = list[i][1];
		int val = aa.get<int>();
		p.push_back(val);
	}
}

float CalDTWDist(const vector<double>& vecRef, const vector<double>& vecCur)
{
	int curSize = vecCur.size();

	double* ref = new double[curSize]; //这里ref是被对比的含义
	for (int j = 0; j < curSize; j++) {
		ref[j] = vecRef[j];
	}
	double* Current = new double[curSize];
	for (int j = 0; j < curSize; j++) {
		Current[j] = vecCur[j];
	}

	float dVal = DTWDistanceFun(ref, curSize, Current, curSize, curSize / 10);
	delete[]ref; delete[]Current;

	return dVal;
}

bool rpcHandler::handleMethodCall_fileDownload(const std::string& method, yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	if (method != "downloadFile") return false;

	yyjson_val* yyv_url = yyjson_obj_get(params, "url");
	yyjson_val* yyv_chunkSize = yyjson_obj_get(params, "chunkSize");
	if (!yyv_url || !yyv_chunkSize) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "url and chunkSize required");
		return true;
	}

	std::string url = yyjson_get_str(yyv_url);
	int chunkSize = yyjson_get_int(yyv_chunkSize);
	int chunkIndex = 0;
	yyjson_val* yyv_chunkIndex = yyjson_obj_get(params, "chunkIndex");
	if (yyv_chunkIndex) {
		chunkIndex = yyjson_get_int(yyv_chunkIndex);
	}

	// url -> filesystem path
	// /db/record/xxx.h264 -> {dbPath}/record/xxx.h264
	// /record/xxx.mp4      -> {dbPath}/record/xxx.mp4
	std::string filePath;
	if (url.find("/db/record/") == 0) {
		filePath = tds->conf->dbPath + "/record/" + url.substr(strlen("/db/record/"));
	}
	else if (url.find("/record/") == 0) {
		filePath = tds->conf->dbPath + "/record/" + url.substr(strlen("/record/"));
	}
	else {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "invalid url format: " + url);
		return true;
	}

	// open file
	FILE* fp = fopen(filePath.c_str(), "rb");
	if (!fp) {
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::OS_fileNotExist, "file not found: " + filePath);
		return true;
	}

	// get file size
	fseek(fp, 0, SEEK_END);
	long fileSize = ftell(fp);
	if (fileSize <= 0) {
		fclose(fp);
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::OS_fileNotExist, "empty file: " + filePath);
		return true;
	}

	int chunkCount = (int)(fileSize / chunkSize);
	if (fileSize % chunkSize != 0) chunkCount++;

	// seek to chunk offset
	long offset = (long)chunkIndex * chunkSize;
	if (offset >= fileSize) {
		fclose(fp);
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_WrongParamFmt, "chunkIndex out of range");
		return true;
	}

	int bytesToRead = chunkSize;
	if (offset + bytesToRead > fileSize) {
		bytesToRead = (int)(fileSize - offset);
	}
	fseek(fp, offset, SEEK_SET);

	// read chunk data
	unsigned char* chunkData = new unsigned char[bytesToRead];
	size_t bytesRead = fread(chunkData, 1, bytesToRead, fp);
	fclose(fp);

	if (bytesRead <= 0) {
		delete[] chunkData;
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "read file error");
		return true;
	}
	int actualLen = (int)bytesRead;

	// base64 encode
	char* b64Out = new char[BASE64_ENCODE_OUT_SIZE(actualLen)];
	base64_encode(chunkData, actualLen, b64Out);
	delete[] chunkData;

	std::string b64Data(b64Out);
	delete[] b64Out;

	// build response
	yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* root = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, root, "url", url.c_str());
	yyjson_mut_obj_add_int(doc, root, "chunkSize", chunkSize);
	yyjson_mut_obj_add_int(doc, root, "chunkIndex", chunkIndex);
	yyjson_mut_obj_add_int(doc, root, "chunkCount", chunkCount);
	yyjson_mut_obj_add_strcpy(doc, root, "data", b64Data.c_str());
	yyjson_mut_doc_set_root(doc, root);

	size_t jsonLen = 0;
	char* jsonStr = yyjson_mut_write(doc, 0, &jsonLen);
	rpcResp.result = std::string(jsonStr, jsonLen);
	free(jsonStr);
	yyjson_mut_doc_free(doc);

	return true;
}

bool rpcHandler::handleMethodCall(string method, yyjson_val* params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	bool bHandled = true;

	if (prj.handleRpc(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else if (db.handleRpc(method, params, rpcResp.result, rpcResp.error, rpcResp.dbQueryInfo, session.org, session.language)) {
		bHandled = true;
	}
	else if (ioSrv.handleRpc(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else if (scriptManager.handleRpc(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else if (streamSrv.handleRpc(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else if (handleMethodCall_utils(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else if (handleMethodCall_fileDownload(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else if (uplinkMnger.handleRpc(method, params, rpcResp, session)) {
		bHandled = true;
	}
	else {
		bHandled = false;
	}

	return bHandled;
}

bool rpcHandler::handleMethodCall(string method, json params, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	if (method.find("set") == 0) {
		if (params == nullptr) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "rpc params cannot be null when setXXX method is called");
			return true;
		}
	}

	if (rpcHandler_common.handleRpc(method, params, rpcResp, session)) {
		return true;
	}

	// 内部调用（如脚本 val/sum/avg/input/output 等函数通过 tds->call）走的是 json 路径，
	// 而 prj.handleRpc、db.handleRpc、ioSrv.handleRpc、scriptManager.handleRpc、
	// streamSrv.handleRpc 等方法路由仅在 yyjson_val* 重载中实现。
	// 此处将 json params 转为 yyjson_val* 后转发给 yyjson 重载，无需逐个列举方法名。
	if (params != nullptr && !params.is_null()) {
		string sParams = params.dump();
		yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
		if (doc) {
			yyjson_val* yyParams = yyjson_doc_get_root(doc);
			if (handleMethodCall(method, yyParams, rpcResp, session)) {
				yyjson_doc_free(doc);
				return true;
			}
			yyjson_doc_free(doc);
		}
	}

	if (handleMethodCall_unclassified(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_edgeDev(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_gamePad(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_audioPlayer(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_MoMng(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_alarmMng(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_userMng(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_OSFunc(method, params, rpcResp)) {
		return true;
	}

	if (handleMethodCall_db(method, params, rpcResp, session)) {
		return true;
	}

	if (handleMethodCall_IoMng(method, params, rpcResp,session)) {
		return true;
	}

	if (handleMethodCall_video(method, params, rpcResp, session)) {
		return true;
	}

	if (method == "getLocalTime") {
		json nowtime;
		nowtime["time"] = timeopt::nowStr();
		rpcResp.result = nowtime.dump();
	}
	
	if (rpcResp.result != "" || rpcResp.error != "") {
		return true;
	}
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

bool rpcHandler::handleRpcRoute(yyjson_val* jReq, RPC_RESP& rpcResp, RPC_SESSION& session)
{
	string method = yyjson_get_str(yyjson_obj_get(jReq,"method"));
	yyjson_val* yyIOSession = yyjson_obj_get(jReq, "ioSession");
	yyjson_val* yyioAddr = yyjson_obj_get(jReq, "ioAddr");
	yyjson_val* yytag = yyjson_obj_get(jReq, "tag");
	yyjson_val* yyObj = yyjson_obj_get(jReq, "object");
	yyjson_val* yyChildTds = yyjson_obj_get(jReq, "childTds");
	yyjson_val* yyv_params = yyjson_obj_get(jReq, "params");

	if (yyIOSession) {//使用tdsSession进行io透传
		string ioSession = yyjson_get_str(yyIOSession);
		shared_ptr<TDS_SESSION> pDestSession = ioSrv.getIOSession(ioSession);
		
		if (pDestSession == nullptr) {
			return true;
		}

		string str;

		size_t len;
		char* p = yyjson_val_write(jReq, 0, &len);
		if (p) {
			str = p;
			free(p);
		}

		json j = json::parse(str);
		j["clientId"] = session.remoteAddr;
		j.erase("user");
		j.erase("token");

		string s = j.dump() + "\n\n";
		pDestSession->send((char*)s.c_str(), s.length());

		return true;
	}
	else if (yyioAddr) {
		ioDev* pIoDev = nullptr;

		string strIoAddr = yyjson_get_str(yyioAddr);
		session.route_ioAddr = strIoAddr;

		pIoDev = ioSrv.getIODev(strIoAddr);
		if (!pIoDev) {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到指定IO地址的IO设备");
			return true;
		}

		string s;

		size_t len; 
		char* p = yyjson_val_write(jReq, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		json j = json::parse(s);
		pIoDev->handleDevRpcCall(j,rpcResp);

		logRPCRoute(method, yyv_params,rpcResp, session, "", pIoDev->getIOAddrStr());

		return true;
	}
	else if (yytag) {
		string tag = yyjson_get_str(yytag);
		tag = TAG::addRoot(tag, session.org);

		session.route_tag = tag;

		string s;
		
		size_t len;
		char* p = yyjson_val_write(jReq, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		json j = json::parse(s);

		//查找是否有直接绑定的设备
		ioDev* pIoDev = ioSrv.getIODevByTag(tag);
		if (pIoDev) {
			j.erase("tag");
			session.route_ioAddr = pIoDev->getIOAddrStr();
			pIoDev->handleDevRpcCall(j, rpcResp);

			logRPCRoute(method, yyv_params,rpcResp, session, tag, pIoDev->getIOAddrStr());
			return true;
		}

		//查看是否属于子服务.转变为对子服务的路由请求
		ioDev* pChildTds = ioSrv.getOwnerChildTdsDev(tag);
		if (pChildTds) {
			string childTdsTag = pChildTds->m_strTagBind;
			string tagInChild = TAG::trimRoot(tag, childTdsTag);

			j["tag"] = tagInChild;
			pChildTds->handleDevRpcCall(j, rpcResp);

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
	else if (yyObj) {
		TAG_SELECTOR tagSel;
		OBJ_SELECTOR objSel;
		prj.getTagSel(tagSel, method, yyObj, session);
		prj.getObjSel(objSel, method, yyObj, session);
		vector<OBJ*> objList;
		prj.getObjByTagSelector(objList, tagSel);
		objList = prj.filterByObjSel(objList, objSel);
		string routeInfo;
		int routeObjCount = 0;
		int routeDevCount = 0;
		for (int i = 0; i < objList.size(); i++) {
			OBJ* pObj = objList[i];
			if (pObj) {
				string tag = pObj->getTag();
				ioDev* pIoDev = ioSrv.getIODevByTag(tag);
				if (pIoDev) {
					char* p = yyjson_val_write(jReq, 0, nullptr);
					if (p) {
						string ioAddr = pIoDev->getIOAddrStr();
						routeInfo += TAG::trimRoot(tag, session.org) + "," + ioAddr + ";";
						routeObjCount++;
						routeDevCount++;
						string s = p;
						thread t([s, method, session, pIoDev,tag,ioAddr]() {
							yyjson_doc* doc = yyjson_read(s.c_str(), s.length(), 0);
							yyjson_val* yyv_req = yyjson_doc_get_root(doc);
							yyjson_val* yyv_params = yyjson_obj_get(yyv_req, "params");
							RPC_RESP resp;
							pIoDev->handleDevRpcCall(yyv_req, resp);
							rpcSrv.logRPCRoute(method, yyv_params, resp,session,tag,ioAddr);
							yyjson_doc_free(doc);
							});
						t.detach();
						free(p);
					}
				}
				else {
					routeInfo += TAG::trimRoot(tag, session.org) + ",未绑定;";
					routeObjCount++;
				}
			}
		}

		rpcResp.result = RPC_OK;
		rpcResp.info = str::format("选中%d个对象,转发绑定的%d个设备,%s",routeObjCount,routeDevCount,routeInfo.c_str());
	}
	else if (yyChildTds)
	{
		string s = yyjson_val_write(jReq,0,nullptr);
		json jReq = json::parse(s);
		jReq.erase("user");
		jReq.erase("token");
		jReq.erase("childTds");
		session.route_childTds = yyjson_get_str(yyChildTds);
		ioDev* pIoDev = ioSrv.getIODevByTag(session.route_childTds);
		if (pIoDev && pIoDev->m_devSubType == TDSP_SUB_TYPE::childTds)
		{
			pIoDev->handleDevRpcCall(jReq, rpcResp);
		}
		else {
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "未找到与该位号绑定的 TDS下级服务 设备");
		}
		return true;
	}
	return false;
}

void logOutput(const string& tag,json& j) {
	string sDe = j.dump();
	TDB* pOutputLogDB = db.getChildDB("ctrlLog");
	pOutputLogDB->Insert(tag, sDe);
}

void rpcHandler::logRPCRoute(string method,yyjson_val* params,const RPC_RESP& rpcResp,const RPC_SESSION& session,string routeTag,string routeIoAddr) {
	json logParams;
	if (method == "startRepel") {
		logParams["tag"] = routeTag;
		logParams["user"] = "用户:" + session.user;
		logParams["type"] = "探驱联动开始";
		logParams["org"] = session.org;
		logParams["remoteAddr"] = session.remoteAddr;
		logParams["timeCost"] = rpcResp.timeCost;

		string pan = "null";
		yyjson_val* yyv = yyjson_obj_get(params, "pan");
		if (yyv != nullptr && yyjson_is_num(yyv))
			pan = str::fromFloat(yyjson_get_num(yyv));
		string tilt = "null";
		yyjson_val* yyvTilt = yyjson_obj_get(params, "tilt");
		if (yyvTilt != nullptr && yyjson_is_num(yyvTilt))
			tilt = str::fromFloat(yyjson_get_num(yyvTilt));
		logParams["info"] = "设备名称:" + routeTag + ",设备地址:" + routeIoAddr + ",水平角:" + pan + ",俯仰角:" + tilt;
		logParams["success"] = rpcResp.result != "";
		//logSrv.rpc_addLog(logParams, session);
		logOutput(routeTag, logParams);
	}	
	else if (method == "stopRepel") {
		logParams["tag"] = routeTag;
		logParams["user"] = "用户:" + session.user;
		logParams["type"] = "探驱联动结束";
		logParams["org"] = session.org;
		logParams["remoteAddr"] = session.remoteAddr;
		logParams["timeCost"] = rpcResp.timeCost;
		logParams["info"] = "设备名称:" + routeTag + ",设备地址:" + routeIoAddr;
		logParams["success"] = rpcResp.result != "";
		//logSrv.rpc_addLog(logParams, session);
		logOutput(routeTag, logParams);
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


void thread_handleRpcCallAsyn(string str, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl) {
	RPC_RESP resp;
	rpcSrv.handleRpcCall(str, resp, pSession, bAccessCtrl);
	pSession->send((unsigned char*)resp.strResp.data(), resp.strResp.length(), false);
}

void thread_handleSockSrvRpcCallAsyn(string req, std::shared_ptr<SOCK_SESSION> ss) {
	RPC_RESP resp;
	T_ASSERT(ss->appLayerSession != nullptr);
	if (ss->appLayerSession == nullptr)
		return;

	std::shared_ptr<TDS_SESSION> p = std::static_pointer_cast<TDS_SESSION>(ss->appLayerSession);
	rpcSrv.handleRpcCall(req, resp, p);
	sockSrv.sendToSockSession(ss, (unsigned char*) resp.strResp.c_str(), resp.strResp.length());
}

void onSockSrvCallback(char* p, size_t l, std::shared_ptr<SOCK_SESSION> sockSess) {
	string req = str::fromBuff((char*)p,l);
	if (req.find("params") != string::npos) { 
		thread t(thread_handleSockSrvRpcCallAsyn, req, sockSess);
		t.detach();
	}
	else if (req.find("result") != string::npos){// 本地向中心端请求之后的回包，非中心端请求，无需处理
		//此处如何处理未来研究
	}
}

void onSockSrvStatusCallback(bool conn,std::shared_ptr<SOCK_SESSION> sockSess) {
	if (conn) {
		std::shared_ptr<TDS_SESSION> p = make_shared<TDS_SESSION>();
		sockSess->appLayerSession = std::static_pointer_cast<void>(p);
		p->Init();
		p->bConnected = true;
		p->sock = sockSess->sock;
		p->remotePort = sockSess->remotePort;
		p->remoteIP = sockSess->remoteIP;
	}
}

void rpcHandler::handleRpcCallAsyn(string& strReq, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl) {
	thread t(thread_handleRpcCallAsyn, strReq, pSession, bAccessCtrl);
	t.detach();
}

bool rpcHandler::parseSessionUser(yyjson_val* jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession){
	yyjson_val* yyUser = yyjson_obj_get(jReq, "user");
	json jUser;
	if (yyUser && yyjson_is_str(yyUser))
	{
		pSession->user = yyjson_get_str(yyUser);
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
			return false;
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
			return false;
		}
	}

	return true;
}


bool rpcHandler::userAccessAuthentication(yyjson_val* jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession) {
	yyjson_val* yytoken = yyjson_obj_get(jReq, "token");
	yyjson_val* yypwd = yyjson_obj_get(jReq, "pwd");
	
	if (yytoken && yyjson_is_str(yytoken)){
		pSession->token = yyjson_get_str(yytoken);
	}
	if(yypwd && yyjson_is_str(yypwd)){
		pSession->pwd = yyjson_get_str(yypwd);
	}

	if (pSession->token == "" && pSession->pwd == "")
	{
		rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "access denied, please set access token or password");
		return false;
	}

	//user token to access
	if (pSession->token != "")
	{
		string token = pSession->token;
		string user = pSession->user;
		if (!userMng.checkToken(user, token))
		{
			//LOG("[warn]认证失败，token验证未通过,user=%s,token=%s,method=%s",user.c_str(),token.c_str(),method.c_str());
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "access denied, invalid access token");
			return false;
		}
	}

	//use password to access, for api testing, do not use in a productin enviroment
	if (pSession->pwd != "")
	{
		string pwd = pSession->pwd;
		string user = pSession->user;
		if (!userMng.checkPwd(user, pwd))
		{
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_tokenError, "access denied, invalid password");
			return false;
		}
	}

	return true;
}

bool rpcHandler::isDebugMethod(string method) {
	if (method == "getApiSessions") {
		return true;
	}
	return false;
}

void rpcHandler::handleRpcCall_single(yyjson_val* jReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl) {
	yyjson_val* yyMethod = yyjson_obj_get(jReq, "method");
	yyjson_val* yyParams = yyjson_obj_get(jReq, "params");

	json params;

	if (yyMethod == nullptr) {
		string s;

		size_t len;
		char* p = yyjson_val_write(jReq, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		LOG("[error][TDS-RPC]协议数据包必须包含method字段\n" + s);
		rpcResp.strResp = R"({"jsonrpc":"2.0","error":"missing field method", "id" : null})";

		return;
	}

	if (yyParams == nullptr) {
		string s;

		size_t len;
		char* p = yyjson_val_write(jReq, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		LOG("[error][TDS-RPC]协议数据包必须包含params字段\n" + s);
		rpcResp.strResp = R"({"jsonrpc":"2.0","error":"missing field params", "id" : null})";

		return;
	}

	string method = yyjson_get_str(yyMethod);

	yyjson_val* id = yyjson_obj_get(jReq, "id");

	string jStrId;
	if (id == nullptr) {
		rpcResp.isNotification = true;
		pSession->isNotification = true;
		jStrId = "null";
	}
	else {	
		char* p= yyjson_val_write(id, 0, 0);
		if (p) {
			jStrId = p;
			free(p);
		}
	}

	statisCall(method);

	if (m_mapDisableMethod.find(method) != m_mapDisableMethod.end()) {
		string s;

		size_t len;
		char* p = yyjson_val_write(id, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		rpcResp.strResp = str::format(R"({"jsonrpc":"2.0","error":"method disabled", "id" : %s})", s);
		return;
	}

	yyjson_val* yydbPath = yyjson_obj_get(jReq, "dbPath");
	if (yydbPath) {
		pSession->dbpath = yyjson_get_str(yydbPath);
	}

	yyjson_val* yylanguage = yyjson_obj_get(jReq, "language");
	if (yylanguage) {
		pSession->language = yyjson_get_str(yylanguage);
	}

	//调试命令会话不纳入统计
	if (!pSession->isDebug) {
		m_csRpcSessions.lock();

		std::map<string, RPC_SESSION>::iterator iter = m_mapRpcSessions.find(pSession->remoteAddr);
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

	//命令请求日志，日志记录在 handle前，方便观察执行时间
	bool needLog = false;
	if (method == "output" || method == "openStream") {
		needLog = true;

		string s;

		size_t len; 
		char* p = yyjson_val_write(jReq, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		LOG("[warn]RPC请求:\r\n" + s + "\r\n");
	}

	//心跳最先处理
	if (method == "heartbeat") {
		json j;
		j["tdsVersion"] = tds->getSvnVersion();
		j["startTime"] = g_strStartupTime;
		#ifdef _WIN32
		j["serverStatus"]["cpu"] = statusSrv.m_srvStatus.cpu;
		j["serverStatus"]["mem"] = statusSrv.m_srvStatus.mem;
		j["serverStatus"]["handle"] = statusSrv.m_srvStatus.handle;
		#else
		// TODO: add Linux
		j["serverStatus"]["cpu"] = 0;
		j["serverStatus"]["mem"] = 0;
		j["serverStatus"]["handle"] = 0;
		#endif

		rpcResp.result = j.dump();
		goto HANDLE_END;
	}

	//setObj走yyjson handler,其他的走json.hpp handler,逐步重构
	if (method != "setObj") {
		string s;

		size_t len; 
		char* p = yyjson_val_write(yyParams, 0, &len);
		if (p) {
			s = p;
			free(p);
		}

		params = json::parse(s);
	}

	//调试类命令
	if (handleMethodCall_debugFunc(method, params, rpcResp, pSession->getRpcSession())) {
		goto HANDLE_END;
	}

	//访问控制
	if (method == "login") {
		userMng.rpc_login(params, rpcResp, pSession->getRpcSession());
		goto HANDLE_END;
	}
	else if (method == "logout") {
		userMng.rpc_logout(params, rpcResp, pSession->getRpcSession());
		goto HANDLE_END;
	}

	//没有打开API鉴权，数据包也可以携带user，用于监控对象范围、读写等权限控制
	if (!parseSessionUser(jReq, rpcResp, pSession)) {
		goto HANDLE_END;
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
			rpcResp.error = makeRPCError(RPC_ERROR_CODE::AUTH_noPermission, "no permission", "没有权限");
			goto HANDLE_END;
		}
	}

	if (bAccessCtrl && tds->conf->enableAccessCtrl) {
		if (!userAccessAuthentication(jReq, rpcResp, pSession)) {
			goto HANDLE_END;
		}
	}

	//设备模式不开启中继转发处理.返回true表示是中继命令.放在用户认证前面处理.
	if (handleRpcRoute(jReq, rpcResp, pSession->getRpcSession())) {
		goto HANDLE_END;
	}

	//先使用外部注册的handler受理请求
	if (m_pluginHandler) {
		string sReq;
		string error;

		char* p = yyjson_val_write(jReq, 0, nullptr);
		if (p) {
			sReq = p;
			free(p);
		}

		bool bHandled = m_pluginHandler(sReq, rpcResp, error);
		if (bHandled) {
			goto HANDLE_END;
		}
	}

	//yyjson handler模式，提高性能，后续的handler逐步重构到这里
	if (handleMethodCall(method, yyParams, rpcResp, pSession->getRpcSession())) {
		goto HANDLE_END;
	}

	//tds自身受理
	if (!handleMethodCall(method, params, rpcResp, pSession->getRpcSession())) {
		string strReq;
		char* writeResult = yyjson_val_write(jReq, 0, 0);
		if (writeResult) {
			strReq = writeResult;
			free(writeResult);
		}

		LOG("[warn]调用不存在的rpc方法\r\n" + strReq);
		rpcResp.strResp = str::format(R"({"jsonrpc": "2.0", "error": {"code": -32601, "message": "Method not found"}, "id": %s})", jStrId.c_str());
		return;
	}

HANDLE_END:

	//组装jsonRPC
	rpcResp.strResp = "{\"server\":\"" + prj.m_name + "," + tds->m_sTitle + "\",\"method\":\"" + method;
	if (rpcResp.error != "") {
		rpcResp.strResp += "\",\"error\":" + rpcResp.error + ",\"id\":" + jStrId;
	}
	else if (rpcResp.result != "") {
		rpcResp.strResp += "\",\"id\":" + jStrId;

		//info放在result前面打印，因为result可能比较长，info短，放前面测试观察方便
		if (rpcResp.params != "") {
			rpcResp.strResp += ",\"params\":" + rpcResp.params;
		}

		if (rpcResp.info != "") {
			rpcResp.strResp += ",\"info\":\"" + rpcResp.info + "\"";
		}

		if (rpcResp.dbQueryInfo != "") {
			rpcResp.strResp += ",\"dbLog\":" + rpcResp.dbQueryInfo;
		}

		rpcResp.strResp += ",\"result\":" + rpcResp.result;
	}
	else {
		string se = "internal error!! request not correctly handled ,no result or error returned";

		string strReq;
		char* writeResult = yyjson_val_write(jReq, 0, 0);
		if (writeResult) {
			strReq = writeResult;
			free(writeResult);
		}

		LOG("[error]" + se + " " + strReq);
		rpcResp.strResp += "\",\"error\":\"" + se + "\"" + ",\"id\":" + jStrId;
	}

	//route参数，路由请求的回包包含请求中的路由参数
	if (pSession->route_ioAddr != "") {
		rpcResp.strResp += ",\"ioAddr\":\"" + pSession->route_ioAddr + "\"";
	}

	if (pSession->route_tag != "") {
		rpcResp.strResp += ",\"tag\":\"" + pSession->route_tag + "\"";
	}

	if (pSession->route_childTds != "") {
		rpcResp.strResp += ",\"childTds\":\"" + pSession->route_childTds + "\"";
	}

	if (pSession->tStartHandle != 0) {
		pSession->tEndCall = getTick();
		double tWait = (pSession->tStartHandle - pSession->tStartCall)/1000.0;
		double tHandle = (pSession->tEndCall - pSession->tStartHandle)/1000.0;
		string s = str::format(",\"timeCost\":{\"waitLock\":%lf,\"handle\":%lf}", tWait, tHandle);
		rpcResp.strResp += s;
	}

	rpcResp.strResp += "}\n\n";

	if (tds->conf->m_apiAdaptorScript != "") {
		apiAdaptorScript(rpcResp.strResp);
	}

	//数据过长或者频率过高的命令不记录日志
	if (rpcResp.result != "" && needLog) {
		//更多的api请求响应日志，使用 apiDebug页面分析
		LOG("[warn]RPC响应:\r\n" + rpcResp.result + "\r\n");
	}
}

long long rpcHandler::getTick() {
	auto now = std::chrono::high_resolution_clock::now();
	auto microsec = std::chrono::time_point_cast<std::chrono::microseconds>(now);
	auto epoch = microsec.time_since_epoch();
	long long microseconds = epoch.count();
	return microseconds;
}

void rpcHandler::handleRpcCall(string& strReq, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession, bool bAccessCtrl) {
	pSession->tStartCall = getTick();

	string error = "";
	string method = "";

	json id = nullptr;

	bool bGB2312 = false;
	bool bNeedLog = true;

	std::map<string, RPC_SESSION>::iterator iter;

	if (strReq.length() == 0)  {
		rpcResp.strResp = R"({"jsonrpc":"2.0","error":"invalid request format. request length is 0", "id" : null})";
		return;
	}

	try {
		yyjson_doc* yydoc = yyjson_read(strReq.c_str(), strReq.size(), 0);
		yyjson_val* yyReq = yyjson_doc_get_root(yydoc);

		if (yyReq) {
			if (yyjson_is_arr(yyReq)) {
				rpcResp.strResp = "[";
				size_t idx, max;
				yyjson_val* yySingleReq;
				yyjson_arr_foreach(yyReq, idx, max, yySingleReq) {
					RPC_RESP singleResp;
					handleRpcCall_single(yySingleReq, singleResp, pSession, bAccessCtrl);

					rpcResp.strResp += singleResp.strResp;

					if (idx != max - 1) {
						rpcResp.strResp += ",";
					}
				}

				rpcResp.strResp += "]";
			}
			else {
				handleRpcCall_single(yyReq, rpcResp, pSession, bAccessCtrl);
			}
		}
		else {
			LOG("[error]RPC请求包处理异常,无法解析的json格式:\r\n错误信息:\r\n" + strReq);
			rpcResp.strResp = str::format(R"({"jsonrpc": "2.0", "error" : {"code": -32700, "message" : "%s"}, "id" : null})", "无法解析的json格式");
		}

		yyjson_doc_free(yydoc);
	}
	catch (std::exception& e) {
		string errorType = e.what();

		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("[error]RPC请求包处理异常:\r\n错误信息:" + errorType + "\r\n数据包:\r\n" + strReq);
		rpcResp.strResp = str::format(R"({"jsonrpc": "2.0", "error" : {"code": -32700, "message" : "%s"}, "id" : null})",errorType.c_str());
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

void rpcHandler::rpc_output(json params, RPC_RESP& resp, RPC_SESSION& session)
{
	json val = nullptr;

	//获取输出参数
	if (params.is_object()) {
		if (params["val"] != nullptr) {
			val = params["val"];
		}
	}
	else {
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
	if (!pmp) {
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
	if (pmp->m_valType == VAL_TYPE::boolean) {
		if (!val.is_boolean()) {
			if (val.is_string() && val.get<string>() == "取反") {
				if (JSON_STR::is_bool(pmp->m_curVal)) {
					val = !JSON_STR::get_bool(pmp->m_curVal);
				}
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::MO_currentValIsNull, "current value is null");
					LOG("[warn]output请求错误," + resp.error);
					return;
				}
			}
			else if (val.is_null()) { //开关量输出值省略 表示输出当前值取反
				if (JSON_STR::is_bool(pmp->m_curVal)) {
					val = !JSON_STR::get_bool(pmp->m_curVal);
				}
				else {
					resp.error = makeRPCError(RPC_ERROR_CODE::MO_currentValIsNull, "current value is null");
					LOG("[warn]output请求错误," + resp.error);
					return;
				}
			}
			else {
				resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeBool, "output val should be bool type");
				LOG("[warn]output请求错误," + resp.error);
				return;
			}
		}
	}
	else if (pmp->m_valType == VAL_TYPE::Float) {
		if (!val.is_number()) {
			resp.error = makeRPCError(RPC_ERROR_CODE::MO_outputValShouldBeNumber, "output val should be number type");
			LOG("[warn]output请求错误," + resp.error);
			return;
		}
	}
	else if (pmp->m_valType == VAL_TYPE::integer) {
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
			else if (val.is_string() && val.get<string>() == "取反" && JSON_STR::is_int(pmp->m_curVal)) {
				//modbus寄存器在工程实践中，常常用03保持寄存器0，1数值代表一个bool量
				int iVal = JSON_STR::get_int(pmp->m_curVal);
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
	if (session.isNotification) {
		syncCall = false;
	}

	if (params["waitResp"].is_boolean() && params["waitResp"].get<bool>() == false) {
		syncCall = false;
	}

	pmp->output(val, rlt, err, syncCall);
	if (syncCall) {
		if(rlt!=nullptr) {
			json jDe = params; //返回输出成功的结果
			jDe["val"] = val;
			resp.result = rlt.dump();

			pmp->input(val);
			pmp->saveToDB();
		}
		else {
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
	logParams["info"] = "设置为:" + valDesc;

	logSrv.rpc_addLog(logParams, session);
}

struct INPUT_DE {
	string tag;
	json val;
	json online;
	json file;
	json ioAddr;
	json valAttr;
	bool hasAttr;
	string sTime;
	TIME time;
};


json getValAttr(json de) {
	de.erase("time");
	de.erase("val");
	de.erase("tag");
	de.erase("rootTag");
	de.erase("file");
	de.erase("online");
	return de;
}

/*对象模式输入,输入对象的位号,内部监控点只要输入相对位号就行,类似树形结构输入,但是只有对象和监控点2级
"params": {
	"tag":"客厅",   //需要输入的对象
	"data":[    //对象下的监控点
		{
			"tag":"温度",
			"val":26.5
		},{
			"tag":"湿度",
			"val":23.5
		}
	]
}*/
//单位号模式输入
/*
"params": {
	"tag": "客厅.温度",
	"time": "2020-02-14 20:20:20",
	"val": 35
}
*/
//多位号模式 （采集时间相同）
/*
"params": {
	"tag": ["客厅.温度","客厅.湿度"],
	"time": "2020-02-14 20:20:20",
	"val": [26.5,65.1]
}
*/
void rpcHandler::rpc_input(json params, RPC_RESP& resp, RPC_SESSION& session) {
	if (params.is_object() && params.contains("name")) { //tdsp接收到子服务的数据后，会进入到此处
		string rootTag;
		if (params.contains("rootTag")) {
			rootTag = params["rootTag"];
		}

		json jDeList = json::array();
		map<string,bool> jOnlineStatusList;

		//params为子服务上送的对象树，rootTag为子服务在上级服务中的位号
		//数据值和在线状态都来自于io设备采集，因此统一用input接口输入
		OBJ::treeStatus2ListStatus(params, jDeList, jOnlineStatusList, rootTag);

		//对于对象，更新在线状态
		for (auto& i: jOnlineStatusList) {
			bool online = i.second;
			string tag = i.first;
			if (online) {
				prj.setObjOnline(tag);
			}
			else{
				prj.setObjOffline(tag);
			}
		}

		params = jDeList;
	}

	//监测点组将忽略不同的保存间隔，其中有1个点要保存就都保存
	bool isMpGroup = false;
	if (params.contains("isMpGroup")) {
		isMpGroup = params["isMpGroup"].get<bool>();
	}

	//输入 位号，值，文件数据，时间 四元组。 文件不一定有
	string rootTag = "";
	vector<INPUT_DE> inputDEList;

	if (params.is_object()) {
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
					de.online = j["online"];
					de.valAttr = getValAttr(j);
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
			json online = params["online"];
			json file = params["file"];
			json time = params["time"];

			if (params.contains("rootTag")) {
				rootTag = params["rootTag"].get<string>();
			}

			if (tag.is_string()) {
				INPUT_DE de;
				de.tag = tag;
				de.val = val;
				de.online = online;
				de.file = file;
				de.valAttr= getValAttr(params);
				de.hasAttr = de.valAttr.size() > 0;

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
			else if (tag.is_array()) {
				for (int i = 0; i < tag.size(); i++) {
					INPUT_DE de;
					de.tag = tag[i];
					de.val = val[i];

					if (online.is_array()) {
						de.online = online[i];
					}

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
			de.online = jDe["online"];
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
		vector<MP*> vecMps;

		for (int i = 0; i < inputDEList.size(); i++) {
			INPUT_DE& de = inputDEList[i];

			string tag = de.tag;
			json& val = de.val;
			json& file = de.file;

			tag = TAG::addRoot(tag, rootTag);
			tag = TAG::addRoot(tag, session.org);

			if (tag != "") {
				MP* pmp = prj.GetMPByTag(tag, session.language);
				if (pmp) {
					pmp->input(val, &file, &de.time);
					if (pmp->m_bEnableIO) {
						if (de.hasAttr) {
							for (auto& iter : de.valAttr.items()) {
								pmp->m_curValAttr[iter.key()] = iter.value().dump();
							}
						}
						vecMps.push_back(pmp);
					}
				}
				else if(file != nullptr){
					//曲线文件会在此处存储，本地端可能推送中心端不存在的监控点
					//某些情况下，监控点只有历史数据，没有实时数据
					//约定使用监控点名称，则中心端不需要创建监控点
					json jDe;
					jDe["tag"] = tag;
					jDe["val"] = val;
					jDe["file"] = file;

					DB_TIME dbTime;
					dbTime.fromStr(de.sTime);

					string sDe = jDe.dump();
					db.Insert(tag, sDe, &dbTime);
				}
			}
		}

		if (vecMps.size() > 0) {
			//此处的设计尚不完善，modbus的寄存器有时希望作为监测点组，有时不希望
			//如果发现设置了长的保存周期，依然很短时间就保存了，那就是被监测点组机制影响了
			//监测点组中有任意一个点需要保存，则全部保存
			//可能某些监测点发生了值变化需要保存，有些点没有变化。统一保存。因为某些可视化页面必须同一个时间点，两个位号的数据都有
			if (isMpGroup) {
				bool needSave = false;
				for (int i = 0; i < vecMps.size(); i++) {
					MP* pmp = vecMps[i];
					if (pmp->needSaveToDB()) {
						needSave = true;
						break;
					}
				}

				if (needSave) {
					for (int i = 0; i < vecMps.size(); i++) {
						MP* pmp = vecMps[i];
						pmp->saveToDB();
					}
				}
			}
			else {
				for (int i = 0; i < vecMps.size(); i++) {
					MP* pmp = vecMps[i];
					if (pmp->needSaveToDB()) {
						pmp->saveToDB();
					}
				}
			}

			//发送状态更新通知
			json jStatusNotify = json::array();
			for (int i = 0; i < vecMps.size(); i++) {
				MP* pmp = vecMps[i];

				json jDe;
				jDe["tag"] = pmp->getTag("",session.language);
				jDe["val"] = json::parse(pmp->m_curVal);
				jDe["file"] = pmp->m_curFileData;
				jDe["time"] = pmp->m_stDataLastUpdate.toStr();
				jDe["valDesc"] = pmp->getValDesc(false);

				jStatusNotify.push_back(jDe);
			}

			rpcSrv.notify("onDataUpdate", jStatusNotify.dump());
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


string rpcHandler::rpc_getTopoList(json params, string& error,RPC_SESSION& session)
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

void rpcHandler::rpc_getMpStatis(json params, RPC_RESP& resp, RPC_SESSION& session) {
	IO_TYPE_STATIS ioTypeStatis;
	VAL_TYPE_STATIS valTypeStatis;
	vector<MP*> allMp;
	prj.GetAllChildMp(allMp);
	for (int i = 0; i < allMp.size(); i++){
		MP* pmp = allMp[i];
		if (pmp->m_ioType == "v") {
			ioTypeStatis.V++;
		}
		else if (pmp->m_ioType == "c") {
			ioTypeStatis.C++;
		}
		else if (pmp->m_ioType == "i") {
			ioTypeStatis.I++;
		}
		else if (pmp->m_ioType == "o") {
			ioTypeStatis.O++;
		}
		else if (pmp->m_ioType == "io") {
			ioTypeStatis.IO++;
		}
		else {
			ioTypeStatis.Unknown++;
		}

		if (pmp->m_valType == VAL_TYPE::Float) {
			valTypeStatis.Float++;
		}
		else if(pmp->m_valType == VAL_TYPE::boolean) {
			valTypeStatis.Bool++;
		}
		else if (pmp->m_valType == VAL_TYPE::integer) {
			valTypeStatis.Int++;
		}
		else if (pmp->m_valType == VAL_TYPE::str) {
			valTypeStatis.String++;
		}
		else if (pmp->m_valType == VAL_TYPE::video) {
			valTypeStatis.Video++;
		}
		else {
			valTypeStatis.Unknown++;
		}
	}


	resp.result = "{"
		"\"total\":" + to_string(allMp.size()) + ","
		"\"ioType\":{"
			"\"v\":" + to_string(ioTypeStatis.V) + ","
			"\"c\":" + to_string(ioTypeStatis.C) + ","
			"\"i\":" + to_string(ioTypeStatis.I) + ","
			"\"o\":" + to_string(ioTypeStatis.O) + ","
			"\"io\":" + to_string(ioTypeStatis.IO) +
		"},"
		"\"valType\":{"
			"\"float\":" + to_string(valTypeStatis.Float) + ","
			"\"bool\":" + to_string(valTypeStatis.Bool) + ","
			"\"int\":" + to_string(valTypeStatis.Int) + ","
			"\"string\":" + to_string(valTypeStatis.String) + ","
			"\"video\":" + to_string(valTypeStatis.Video) +
		"}" +
	"}";
}

void rpcHandler::rpc_getObjStatis(json params, RPC_RESP& resp, RPC_SESSION& session)
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
			j["count"] = total.count;
			j["online"] = total.online;
			j["offline"] = total.offline;
			j["alarm"] = total.alarm;
			j["fault"] = total.fault;
			j["normal"] = total.normal;

			{
				json jList = json::array();
				for (auto& iter : rlt) {
					json jt;
					jt["customType"] = iter.second.customType;
					jt["count"] = iter.second.count;
					jt["online"] = iter.second.online;
					jt["offline"] = iter.second.offline;
					jt["alarm"] = iter.second.alarm;
					jt["fault"] = iter.second.fault;
					jt["normal"] = iter.second.normal;
					jList.push_back(jt);
				}

				j["typeStatis"] = jList;
			}

			if (rootTagList.size() == 1) {
				jStatisRlt = j;
			}
			else
				jStatisRlt[tagOrg] = j;
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "没有找到需要统计的根对象" + tag);
			return;
		}
	}

	resp.result = jStatisRlt.dump();
}


void rpcHandler::rpc_getMoStatis(json params, RPC_RESP& resp, RPC_SESSION& session)
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

json rpcHandler::getAlarmStatis(string rootTag, RPC_SESSION& session) {

	return nullptr;

	//json querier = nullptr;
	//if (session.user != "")
	//	querier["user"] = session.user;
	//if (rootTag != "")
	//	querier["rootTag"] = rootTag;

	//almServer* pAlmSrv = &almSrv;

	//vector<ALARM_INFO*> vecAlarms = pAlmSrv->tableCurrent.query(querier);
	//int iAlarmCount = 0;
	//int iWarnCount = 0;
	//for (int i = 0; i < vecAlarms.size(); i++)
	//{
	//	ALARM_INFO* pai = vecAlarms[i];
	//	if (pai->isRecover)
	//		continue;

	//	if (pai->level == "alarm")
	//		iAlarmCount++;
	//	if (pai->level == "warn")
	//		iWarnCount++;
	//}


	////全局报警禁用功能
	//if (!tds->conf->enableGlobalAlarm)
	//{
	//	iAlarmCount = 0;
	//	iWarnCount = 0;
	//}

	//json jAlmStatis;
	//jAlmStatis["alarm"] = iAlarmCount;
	//jAlmStatis["warn"] = iWarnCount;

	//return jAlmStatis;
}



void rpcHandler::rpc_getDevStatis_old(json params, RPC_RESP& resp,RPC_SESSION& session)
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

void rpcHandler::toMoAttr(Mo_Attr_Params& params, OBJ* pMo, nlohmann::ordered_json& attrInfo,RPC_SESSION& session) {
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
			jVal = json::parse(pmp->m_curVal);
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

void rpcHandler::rpc_moList2Attrlist(Mo_Attr_Params& params,vector<OBJ*> moList,RPC_RESP& resp, RPC_SESSION& session)
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


void rpcHandler::rpc_getMoAttr_list(json params, RPC_RESP& resp,RPC_SESSION& session) {
	Mo_Attr_Params attrParam;

	if (!params.contains("tag")) { //获取子树
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_paramMissing, "missing param: tag");
		return;
	}

	//支持中文直接输入moType;
	if (params["type"].is_string()) {
		attrParam.moType = params["type"].get<string>();
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
	if (params["rootTag"] != nullptr) {
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
	attrParam.tagSel.selLanguage = session.language;

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
	else{
		attrParam.jColumes = params["columes"];
		rpc_moList2table(attrParam, moList, resp, session);
	}
}

//返回1个具有 header,body,tag3个字段的结果
//header根据对象模版生成，位号为所有的mp，递归平铺
void rpcHandler::rpc_moList2table(Mo_Attr_Params& params, vector<OBJ*> moList, RPC_RESP& resp, RPC_SESSION& session) {
	json jTable = json::object();
	json jTableHead = json::array();
	json jTableBody = json::array();
	json jColTag = json::array();

	//根据对象模版生成列模版
	map<string, OBJ_TEMPLATE*>::iterator it = prj.m_mapObjTempalte.find(params.moType);

	bool bStandardObj = false;

	if (params.jColumes != nullptr) {
		if (moList.size() == 0) {
			resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "no object found");
			return;
		}

		/*
 		 head:   tag   online   温度          湿度          online updateTime
		 tag:    null  null     温控器.温度   温控器.湿度   null  null
		 row1:   1号   true     23            65            true    2023-01-03 23:11:11
		 row2:   2号   true     33            ...
		*/
		jTableHead.push_back("tag");
		jColTag.push_back(nullptr);

		for (auto& iter : params.jColumes) {
			string colName = iter["name"].get<string>();
			string tag = iter["tag"].get<string>();

			jTableHead.push_back(colName);
			jColTag.push_back(tag);
		}

		jTableHead.push_back("online");
		jColTag.push_back(nullptr);

		jTableHead.push_back("updateTime");
		jColTag.push_back(nullptr);

		for (int i = 0; i < moList.size(); i++) {
			OBJ* pMo = moList[i];

			string moTag = pMo->getTag("", session.language);
			string tag = moTag;

			tag = TAG::trimRoot(tag, params.tagSel.m_rootTag);

			//数据行
			json jTableRow;
			jTableRow.push_back(tag);

			string updateTime = "-";
			for (int j = 1; j < jColTag.size() - 2; j++) {
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
					else { // (valFmt == "val") 
						jTableRow.push_back(json::parse(pmp->m_curVal));
					}

					OBJ* pParentMo = pmp->getParentMo();
					if (pParentMo) {
						if (pParentMo->getUpdateTimeDesc() == "-") {
							// nothing
						}
						else {
							if (updateTime == "-") {
								updateTime = pParentMo->getUpdateTimeDesc();
							}
							else {
								if (pParentMo->getUpdateTimeDesc() > updateTime) {
									updateTime = pParentMo->getUpdateTimeDesc();
								}
							}
						}
					}
				}
				else {
					jTableRow.push_back("-");
				}
			}

			jTableRow.push_back(pMo->m_bOnline);
			jTableRow.push_back(updateTime);

			jTableBody.push_back(jTableRow);
		}

		jTable["header"] = jTableHead;
		jTable["tag"] = jColTag;
		jTable["body"] = jTableBody;
		jTable["standardObj"] = bStandardObj;
	}
	else if (it != prj.m_mapObjTempalte.end()) {
		bStandardObj = true;

		OBJ_TEMPLATE* ot = it->second;

		vector<MP*> mps;
		ot->obj.GetAttriMp(mps);

		string parentTag = ot->obj.getTag("",session.language);

		if (mps.size() > 0) {
			if (session.language == "en") {
				jTableHead.push_back("Tag");
			}
			else {
				jTableHead.push_back("位号");
			}

			jColTag.push_back(nullptr);

			//所有监控点的名称全部相同，则取父节点作为列名称。一般在编辑对象树时，会有这种用法，例如 1楼.温度  2楼.温度  3楼.温度
			map<string, string> mpNames;
			for (int j = 0; j < mps.size(); j++){
				MP* pmp = mps[j];
				mpNames[pmp->getName(session.language)] = pmp->getName(session.language);
			}

			bool mpNameTheSame = false;
			if (mpNames.size() == 1) {
				mpNameTheSame = true;
			}

			for (int j = 0; j < mps.size(); j++) {
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
						if (pmp->m_pParentMO) {
							jTableHead.push_back(pmp->m_pParentMO->getName(session.language));
						}
					}
				}
			}
			if (session.language == "en") {
				jTableHead.push_back("Online");
			}
			else {
				jTableHead.push_back("在线");
			}

			if (session.language == "en") {
				jTableHead.push_back("UpdateTime");
			}
			else {
				jTableHead.push_back("更新时间");
			}

			jColTag.push_back(nullptr);
			jColTag.push_back(nullptr);

			jTable["header"] = jTableHead;
			jTable["tag"] = jColTag;
		}

		for (int i = 0; i < moList.size(); i++) {
			OBJ* pMo = moList[i];

			string moTag = pMo->getTag("",session.language);
			string tag = moTag;

			//过滤用户权限
			if (session.user != ""){
				if (!userMng.checkTagPermission(session.user, tag)) {
					continue;
				}
			}

			tag = TAG::trimRoot(tag, params.tagSel.m_rootTag);

			//表头与列位号，如果没有模版，根据第一个设备生成
			if (jTableHead.size() == 0){
				//下属所有监控点列表
				vector<MP*> childMps;
				pMo->GetAttriMp(childMps);

				if (session.language == "en") {
					jTableHead.push_back("Tag");
				}
				else {
					jTableHead.push_back("位号");
				}

				jColTag.push_back(nullptr);

				for (int j = 0; j < childMps.size(); j++) {
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

			for (int j = 1; j < jColTag.size() - 2; j++) {
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
					else { // (valFmt == "val") 
						jTableRow.push_back(json::parse(pmp->m_curVal));
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

		for (int i = 0; i < moList.size(); i++) {
			OBJ* pMo = moList[i];

			string moTag = pMo->getTag("",session.language);
			string tag = moTag;

			//过滤用户权限
			if (session.user != ""){
				if (!userMng.checkTagPermission(session.user, tag)) {
					continue;
				}
			}

			tag = TAG::trimRoot(tag, params.tagSel.m_rootTag);

			//数据行
			json jTableRow;
			jTableRow.push_back(pMo->m_bOnline);
			jTableRow.push_back(tag);
			jTableRow.push_back(pMo->getChildObjStatis());
			pMo->m_mxAlarmStatus.lock();
			json jAlmStatus = pMo->m_jAlarmStatus;
			pMo->m_mxAlarmStatus.unlock();
			jTableRow.push_back(jAlmStatus);

			jTableBody.push_back(jTableRow);
		}

		jTable["body"] = jTableBody;
		jTable["standardObj"] = bStandardObj;
	}

	resp.result = jTable.dump();
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

json rpcHandler::rpc_getStreamUrl(MP* pmp,string tag, bool isHttps, string localIP,int LocalPort)
{
	json j;
	//萤石云前端直连萤石云服务
	if (pmp->m_valType == VAL_TYPE::video && pmp->m_mediaSrcType == "ezviz") {
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

string rpcHandler::rpc_heartbeat(json params, string& error , RPC_SESSION& session)
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


void rpcHandler::rpc_getDev(json params, RPC_RESP& resp, RPC_SESSION& session)
{
	yyjson_mut_doc* mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* j = yyjson_mut_obj(mdoc);
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
			DEV_QUERIER query;
			query.parseQueryOpt(params);
			p->toJson(j,mdoc, query);
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device with specified ioAddr not found");
		}
	}
	else if (params.contains("id")) {
		string id = params["id"];
		p = ioSrv.getIODevById(id);
		if (p) {
			DEV_QUERIER query;
			query.parseQueryOpt(params);
			p->toJson(j,mdoc, query);
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device with specified id not found");
		}
	}
	else if(params.contains("tag")){
		string tag = params["tag"];
		tag = TAG::addRoot(tag, session.org);
		p = ioSrv.getIODevByTag(tag);
		if (p) {
			DEV_QUERIER query;
			query.parseQueryOpt(params);
			p->toJson(j,mdoc, query);
		}
		else {
			resp.error = makeRPCError(RPC_ERROR_CODE::IO_devNotFound, "io device with specified bindTag not found");
		}
	}
	else {
		ioSrv.toJson(j,mdoc, params);
	};

	char* pRlt = yyjson_mut_val_write(j, 0, nullptr);
	if(pRlt){
		resp.result = pRlt;
		free(pRlt);
	}	 
	yyjson_mut_doc_free(mdoc);
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

void rpcHandler::rpc_onObjOnline(json params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	string tag = params["tag"];
	bool ok = prj.setObjOnline(tag);
	if (ok) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = "\"tag not found\"";
	}
}

void rpcHandler::rpc_onObjOffline(json params, RPC_RESP& rpcResp, RPC_SESSION& session) {
	string tag = params["tag"];
	bool ok = prj.setObjOffline(tag);
	if (ok) {
		rpcResp.result = RPC_OK;
	}
	else {
		rpcResp.error = "\"tag not found\"";
	}
}

void rpcHandler::rpc_startStreamPush(json params, RPC_RESP& resp, RPC_SESSION& session)
{
	if (params["srcTag"] == nullptr) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param srcTag missing");
		return;
	}
	if (params["destTag"] == nullptr) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param destTag missing");
		return;
	}
	if (!params["port"].is_number_integer()) {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "param port error");
		return;
	}
	string srcTag = params["srcTag"];
	string destTag = params["destTag"];
	int port = params["port"].get<int>();

	ioChannel* pChan = ioSrv.getChanByTag(srcTag);
	if (pChan == nullptr) {
		resp.error = makeRPCError(RPC_ERROR_CODE::MO_specifiedTagNotFound, "channel bind specified tag not found");
		return;
	}

	tcpClt* pClt = new tcpClt();
	if (pClt->connect(&sockSrv, session.remoteIP, port)) {

		tcpSessionClt* pTcpSess = &pClt->m_session;

		//创建TDS Session
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION(pTcpSess));
		pTcpSess->pALSession = p;
		p->name = "数据流推流客户端";
		p->type = TDS_SESSION_TYPE::dataStream;

		//相对于本地io通道来说是拉流
		pChan->m_csStreamPuller.lock();
		pChan->m_vecStreamPuller.push_back(p);
		pChan->m_csStreamPuller.unlock();

		LOG("[数据流   ]  推流服务连接成功,服务地址:%s:%d,源位号:%s,目标位号:%s", session.remoteIP.c_str(), port, srcTag.c_str(), destTag.c_str());
		sendStreamPusherRegPkt(p, destTag);

		resp.result = RPC_OK;
		sockSrv.m_tcpClt_streamPusher[pClt] = pClt;
	}
	else {
		resp.error = makeRPCError(RPC_ERROR_CODE::TEC_FAIL, "connect fail");
		delete pClt;
	}
}


string rpcHandler::getChildTdsRegPkt()
{
	json jReg;
	jReg["method"] = "devRegister";
	json jParams;
	jParams["devType"] = "childTds";
	jParams["httpPort"] = tds->conf->httpPort;
	jParams["httpsPort"] = tds->conf->httpsPort;
	jReg["params"] = jParams;
	jReg["addr"] = prj.m_name;
	string s = jReg.dump() + "\n\n";
	return s;
}

void rpcHandler::sendStreamPusherRegPkt(std::shared_ptr<TDS_SESSION> p, string tag)
{
	json jReg;
	jReg["method"] = "devRegister";
	json jParams;
	jParams["devType"] = "streamPusher";
	jParams["tag"] = tag;
	jReg["params"] = jParams;
	string s = jReg.dump() + "\n\n";
	p->sendStr(s);
}

void rpcHandler::notify(string method, string notifyParams, bool specialNotify, std::shared_ptr<TDS_SESSION> orgSession) {
	string notify = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + notifyParams + "}\n\n";

	//apiAdaptorScript(notify);

	string tag = "";  
	WebServer::notifyAllSrvAllWs(method, tag, notify);

	sockSrv.m_mutexSessions.lock();
	for (auto& i : sockSrv.m_sockSessions) {
		std::shared_ptr<TDS_SESSION> pSession = std::static_pointer_cast<TDS_SESSION>(i.second->appLayerSession);

		if (pSession && pSession->isSubscribed(method, tag)) {
			sockSrv.sendToSockSession(i.second, (unsigned char*)notify.c_str(), notify.size());
		}
	}
	sockSrv.m_mutexSessions.unlock();

	mqttUplink.onTdsNotify(method, notifyParams);
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

bool rpcHandler::apiAdaptorScript(json& jResult)
{
#ifdef ENABLE_QJS
	if (tds->conf->m_apiAdaptorScript != "") {
		
		string temp = jResult["method"];
		bool bFind = false;
		for (auto& item : tds->conf->m_apiAdaptorMethod)
		{
			if (item == temp)
			{
				bFind = true;
				break;
			}
		}

		if (!bFind) return true;

		ScriptEngine se;

#ifdef TDS
		se.m_engineInitFuncList.push_back(initTdsFunc);
#endif

		se.m_globalObj["inputJson"] = jResult;

		SCRIPT_INFO si;
		scriptManager.getScript(tds->conf->m_apiAdaptorScript, si);
		if (se.runScript(si,si.lastRunInfo))
		{
			if (se.m_scriptRet != nullptr)
				jResult = se.m_scriptRet;
			return true;
		}
		return false;
	}
#endif

	return true;
}

bool rpcHandler::apiAdaptorScript(string& strResult)
{
	json jResult = json::parse(strResult);
	bool bResult = apiAdaptorScript(jResult);
	strResult = jResult.dump();
	strResult += "\n\n";

	return bResult;
}

void rpcHandler::vlmAlarmCheck(string& sParams) {
	string url = tds->conf->getStr("vlmAlarmCheck", "");
	if (url.empty()) {
		LOG("vlmAlarmCheck url not found");
		return;
	}

	string model = tds->conf->getStr("vlmAlarmModel", "");
	if (model.empty()) {
		LOG("vlmAlarmModel mode not found");
		return;
	}

	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);
	yyjson_val* yyv_params = yyjson_doc_get_root(doc);

	yyjson_val* yyv_tag = yyjson_obj_get(yyv_params, "tag");
	if (yyv_tag == nullptr) {
		yyjson_doc_free(doc);
		LOG("vlmAlarmCheck tag not found");
		return;
	}

	yyjson_val* yyv_time = yyjson_obj_get(yyv_params, "time");
	if (yyv_time == nullptr) {
		yyjson_doc_free(doc);
		LOG("vlmAlarmCheck time not found");
		return;
	}

	yyjson_val* yyv_img = yyjson_obj_get(yyv_params, "data");
	if (yyv_img == nullptr) {
		yyjson_doc_free(doc);
		LOG("vlmAlarmCheck data not found");
		return;
	}

	string tag = yyjson_get_str(yyv_tag);
	string time = yyjson_get_str(yyv_time);
	string img = yyjson_get_str(yyv_img);

	if (img.find("data:") == string::npos) {
		img = "data:image/jpg;base64," + img;
	}

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		std::string protocol, ip, port, path;
		if (parse_url(url, protocol, ip, port, path)) {
			// 文件路径
			const std::string filePath = fs::appPath() + "/vlm_command.json";

			// 打开文件
			std::ifstream file(filePath);
			if (file.is_open()) {
				// 读取文件内容
				std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

				// 关闭文件
				file.close();

				auto mutDoc = yyjson_mut_doc_new(nullptr);
				auto mutRoot = yyjson_mut_obj(mutDoc);

				yyjson_mut_doc_set_root(mutDoc, mutRoot);

				yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "model", model.c_str());
				yyjson_mut_obj_add_bool(mutDoc, mutRoot, "stream", false);

				//
				auto messageArr = yyjson_mut_arr(mutDoc);
				yyjson_mut_obj_add_val(mutDoc, mutRoot, "messages", messageArr);

				auto messageObj = yyjson_mut_obj(mutDoc);
				yyjson_mut_arr_add_val(messageArr, messageObj);

				yyjson_mut_obj_add_strcpy(mutDoc, messageObj, "role", "user");

				//
				auto contentArr = yyjson_mut_arr(mutDoc);
				yyjson_mut_obj_add_val(mutDoc, messageObj, "content", contentArr);

				//
				auto contentObj1 = yyjson_mut_obj(mutDoc);
				yyjson_mut_arr_add_val(contentArr, contentObj1);


				yyjson_mut_obj_add_strcpy(mutDoc, contentObj1, "type", "text");
				yyjson_mut_obj_add_strcpy(mutDoc, contentObj1, "text", fileContent.c_str());

				//
				auto contentObj2 = yyjson_mut_obj(mutDoc);
				yyjson_mut_arr_add_val(contentArr, contentObj2);

				yyjson_mut_obj_add_strcpy(mutDoc, contentObj2, "type", "image_url");

				auto imageUrlObj = yyjson_mut_obj(mutDoc);
				yyjson_mut_obj_add_val(mutDoc, contentObj2, "image_url", imageUrlObj);

				yyjson_mut_obj_add_strcpy(mutDoc, imageUrlObj, "url", img.c_str());

				string body;

				char* writeResult = yyjson_mut_write(mutDoc, 0, 0);
				if (writeResult) {
					body = writeResult;
					free(writeResult);
				}
				else {
					LOG("vlmAlarmCheck yyjson_mut_write failed");
				}

				yyjson_mut_doc_free(mutDoc);

				mg_printf(connect,
					"POST %s HTTP/1.0\r\n"
					"Host: %s\r\n"
					"Content-Type: application/json\r\n"
					"Content-Length: %u\r\n"
					"\r\n"
					"%s",
					path.c_str(), ip.c_str(), (unsigned int)body.size(), body.c_str()
				);

				TIME tStart;
				tStart.setNow();
				while (!data.done && TIME::calcTimePassSecond(tStart) < 60.0) {
					mg_mgr_poll(&mgr, 100);
				}
			}
			else {
				LOG("vlmAlarmCheck read vlm_command.json failed");
			}
		}
		else {
			LOG("vlmAlarmCheck url parse failed");
		}
	}
	else {
		LOG("vlmAlarmCheck mongoose connect failed");
	}

	if (data.status == 200) {
		string result = data.body;

		yyjson_doc* docResult = yyjson_read(result.c_str(), result.length(), 0);
		yyjson_val* resultRoot = yyjson_doc_get_root(docResult);

		yyjson_val* choicesVal = yyjson_obj_get(resultRoot, "choices");
		if (choicesVal != nullptr && yyjson_is_arr(choicesVal)) {
			yyjson_val* choicesObjVal = yyjson_arr_get(choicesVal, 0);
			if (choicesObjVal != nullptr && yyjson_is_obj(choicesObjVal)) {
				yyjson_val* messageVal = yyjson_obj_get(choicesObjVal, "message");
				if (messageVal != nullptr && yyjson_is_obj(messageVal)) {
					yyjson_val* contentVal = yyjson_obj_get(messageVal, "content");
					if (contentVal != nullptr && yyjson_is_str(contentVal)) {
						string content = yyjson_get_str(contentVal);

						// 步骤1: 找到并删除标签</think>及其前面的所有内容
						size_t pos = content.find("</think>");
						if (pos != string::npos) {
							content = content.substr(pos + 8);
						}

						//
						string contentTmp = content;

						// 步骤2: 根据"\n\n"分割字符串为3部分
						vector<string> parts;
						string delimiter = "\n\n";
						size_t start = 0;
						size_t end = contentTmp.find(delimiter);

						while (end != string::npos) {
							parts.push_back(contentTmp.substr(start, end - start));
							start = end + delimiter.length();
							end = contentTmp.find(delimiter, start);
						}

						// 添加最后一部分
						parts.push_back(contentTmp.substr(start));

						if (parts.size() == 1) {
							parts.clear();
							contentTmp = content;

							delimiter = "\n";
							start = 0;
							end = contentTmp.find(delimiter);

							while (end != string::npos) {
								parts.push_back(contentTmp.substr(start, end - start));
								start = end + delimiter.length();
								end = contentTmp.find(delimiter, start);
							}

							// 添加最后一部分
							parts.push_back(contentTmp.substr(start));
						}

						LOG("vlmAlarmCheck result yyjson content parts size %d", parts.size());

						if (parts.size() == 3) {
							LOG("%s", parts[0]);
							if (parts[0].find("正常") == string::npos) {
								bool isSamAlarmOpen = tds->conf->getInt("enableDVSAlarm", 0) > 0 ? true : false;
								if (isSamAlarmOpen) {
									ALARM_INFO ai;
									ai.tag = tag;
									ai.type = parts[0];
									ai.desc = parts[1] + parts[2];
									ai.level = ALARM_LEVEL::alarm;
									ai.time = time;
									almSrv.Add(ai);
								}
							}
						}					
					}
					else {
						LOG("vlmAlarmCheck result yyjson content not found");
					}
				}
				else {
					LOG("vlmAlarmCheck result yyjson message not found");
				}
			}
			else {
				LOG("vlmAlarmCheck result yyjson choicesObj not found");
			}
		}
		else {
			LOG("vlmAlarmCheck result yyjson choices not found");
		}

		yyjson_doc_free(docResult);
	}
	else {
		LOG("vlmAlarmCheck mongoose callback failed, dataStatus %d", data.status);
	}

	mg_mgr_free(&mgr);
	yyjson_doc_free(doc);
}

void rpcHandler::samAlarmCheck(string& sParams) {
	string url = tds->conf->getStr("samAlarmCheck", "");
	if (url.empty()) {
		LOG("samAlarmCheck url not found");
		return;
	}

	yyjson_doc* doc = yyjson_read(sParams.c_str(), sParams.length(), 0);

	yyjson_mut_doc* outDoc = yyjson_mut_doc_new(NULL);
	yyjson_mut_val* outRoot = yyjson_val_mut_copy(outDoc, yyjson_doc_get_root(doc));
	yyjson_mut_doc_set_root(outDoc, outRoot);
	yyjson_mut_val* outInfo = yyjson_mut_obj_get(outRoot, "info");
	


	yyjson_mut_val* yyv_params = yyjson_mut_doc_get_root(outDoc);

	yyjson_mut_val* yyv_tag = yyjson_mut_obj_get(yyv_params, "tag");
	if (yyv_tag == nullptr) {
		yyjson_doc_free(doc);
		yyjson_mut_doc_free(outDoc);
		LOG("samAlarmCheck tag not found");
		return;
	}

	yyjson_mut_val* yyv_time = yyjson_mut_obj_get(yyv_params, "time");
	if (yyv_time == nullptr) {
		yyjson_doc_free(doc);
		yyjson_mut_doc_free(outDoc);
		LOG("samAlarmCheck time not found");
		return;
	}

	yyjson_mut_val* yyv_img = yyjson_mut_obj_get(yyv_params, "data");
	if (yyv_img == nullptr) {
		yyjson_doc_free(doc);
		yyjson_mut_doc_free(outDoc);
		LOG("samAlarmCheck data not found");
		return;
	}

	yyjson_mut_val* yyv_info = yyjson_mut_obj_get(yyv_params, "info");
	if (yyv_info == nullptr) {
		yyjson_doc_free(doc);
		yyjson_mut_doc_free(outDoc);
		LOG("samAlarmCheck info not found");
		return;
	}
	
	string tag = yyjson_mut_get_str(yyv_tag);
	string time = yyjson_mut_get_str(yyv_time);
	string img = yyjson_mut_get_str(yyv_img);



	if (img.find("data:") == string::npos) {
		//img = "data:image/jpg;base64," + img;
	}

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		std::string protocol, ip, port, path;
		if (parse_url(url, protocol, ip, port, path)) {
			// 文件路径
			const std::string filePath = fs::appPath() + "/sam_command.json";

			// 打开文件
			std::ifstream file(filePath);
			if (file.is_open()) {
				// 读取文件内容
				std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

				yyjson_doc* samConf = yyjson_read(fileContent.c_str(), fileContent.size(), 0);
				yyjson_val* samRoot = yyjson_doc_get_root(samConf);
				yyjson_val* objModel = yyjson_obj_get(samRoot, "model");
				yyjson_val* objPrompt = yyjson_obj_get(samRoot, "prompt");
				yyjson_val* objConfidence = yyjson_obj_get(samRoot, "confidence");
		
				auto mutDoc = yyjson_mut_doc_new(nullptr);
				auto mutRoot = yyjson_mut_obj(mutDoc);

				yyjson_mut_doc_set_root(mutDoc, mutRoot);

				yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "method", "detectService");

				if (objModel)
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "model", yyjson_get_str(objModel));
				else
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "model", "");

				if(objPrompt)
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "prompt", yyjson_get_str(objPrompt));
				else
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "prompt", "");

				if (objConfidence)
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "confidence", yyjson_get_str(objConfidence));
				else
					yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "confidence", "");

				yyjson_mut_obj_add_strcpy(mutDoc, mutRoot, "image", img.c_str());

						// 关闭文件
				file.close();

				string body;

				char* writeResult = yyjson_mut_write(mutDoc, 0, 0);
				if (writeResult) {
					body = writeResult;
					free(writeResult);
				}
				else {
					LOG("samAlarmCheck yyjson_mut_write failed");
				}

				yyjson_mut_doc_free(mutDoc);
				yyjson_doc_free(samConf);
				mg_printf(connect,
					"POST %s HTTP/1.0\r\n"
					"Host: %s:%s\r\n"
					"Content-Type: application/json\r\n"
					"Content-Length: %u\r\n"
					"\r\n"
					"%s",
					path.c_str(), ip.c_str(), port.c_str(),(unsigned int)body.size(), body.c_str()
				);

				TIME tStart;
				tStart.setNow();
				while (!data.done && TIME::calcTimePassSecond(tStart) < 60.0) {
					mg_mgr_poll(&mgr, 100);
				}
			}
			else {
				LOG("samAlarmCheck read sam_command.json failed");
				mg_mgr_free(&mgr);
				yyjson_doc_free(doc);
				yyjson_mut_doc_free(outDoc);
				return;
			}
		}
		else {
			mg_mgr_free(&mgr);
			yyjson_doc_free(doc);
			yyjson_mut_doc_free(outDoc);
			return;
			LOG("samAlarmCheck url parse failed");
		}
	}
	else {
		mg_mgr_free(&mgr);
		yyjson_doc_free(doc);
		yyjson_mut_doc_free(outDoc);
		return;
		LOG("samAlarmCheck mongoose connect failed");
	}

	if (data.status == 200) {
		string result = data.body;

		yyjson_doc* samResult = yyjson_read(result.c_str(), result.length(), 0);
		yyjson_val* samResultRoot = yyjson_doc_get_root(samResult);
		int samStatus = yyjson_get_int(yyjson_obj_get(samResultRoot,"code"));
		if (samStatus != 200){
			yyjson_doc_free(samResult);
			LOG("sam server connect faild");
		}
		else {

			yyjson_val* samResultArr = yyjson_obj_get(samResultRoot, "objects");
			char* tmpInfo = nullptr;
			yyjson_mut_val* outInfo = yyjson_mut_obj_get(outRoot, "info");

			if (samResultArr && yyjson_arr_size(samResultArr) > 0 && outInfo) {
				LOG("samAlarmCheck find anomal");
				size_t idx = 0;
				size_t max = 0;
				yyjson_val* item;
				

				string outInfoStr = yyjson_mut_get_str(outInfo);
				yyjson_doc* tmpDoc = yyjson_read(outInfoStr.c_str(), outInfoStr.size(), 0);
				yyjson_mut_doc* tmpInfoDoc = yyjson_mut_doc_new(nullptr);
				yyjson_mut_doc_set_root(tmpInfoDoc, yyjson_val_mut_copy(tmpInfoDoc, yyjson_doc_get_root(tmpDoc)));
				yyjson_mut_val* tmpRoot = yyjson_mut_doc_get_root(tmpInfoDoc);

				yyjson_mut_val* objects = yyjson_mut_obj_get(tmpRoot, "objects");
				yyjson_arr_foreach(samResultArr, idx, max, item) {
					yyjson_mut_arr_add_val(objects, yyjson_val_mut_copy(outDoc, item));
				}

				//MP* pmp = prj.GetMPByTag(tag, "zh");
				//if (pmp && pmp->m_isSamAlarmOpen) {
				//	ALARM_INFO ai;
				//	ai.tag = tag;
				//	ai.type = "视频异物";
				//	ai.desc = "疑似存在异物";
				//	ai.level = ALARM_LEVEL::L3;
				//	ai.time = time;
				//	almSrv.Add(ai);
				//}

				bool isSamAlarmOpen = tds->conf->getInt("enableDVSAlarm", 0) > 0 ? true : false;
				if (isSamAlarmOpen) {
					ALARM_INFO ai;
					ai.tag = tag;
					ai.type = "视频异物";
					ai.desc = "疑似存在异物";
					ai.level = ALARM_LEVEL::L3;
					ai.time = time;
					almSrv.Add(ai);
				}

				yyjson_doc_free(tmpDoc);

				tmpInfo = yyjson_mut_write(tmpInfoDoc, 0, 0);
				yyjson_mut_doc_free(tmpInfoDoc);
			}
			
			
			yyjson_mut_obj_remove_key(outRoot, "info");
			yyjson_mut_obj_add_strcpy(outDoc, outRoot, "info", tmpInfo);
			char* temp = yyjson_mut_write(outDoc, 0, 0);

			sParams = temp;

			free(tmpInfo);
			free(temp);	
		}
		yyjson_doc_free(samResult);
	}
	else {
		LOG("samAlarmCheck mongoose callback failed, dataStatus %d", data.status);
	}

	mg_mgr_free(&mgr);
	yyjson_doc_free(doc);
	yyjson_mut_doc_free(outDoc);
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


void streamPusherMng_thread() {
	while (1) {
		timeopt::sleepMilli(2000);
		vector<string> toErase;
		prj.m_csPrj.lock_shared();
		for (auto i : rpcSrv.m_mapPullerActive) {
			TIME st = i.second;
			if (timeopt::CalcTimePassSecond(st) > 5) {
				MP* pmp = prj.GetMPByTag(i.first, "zh");
				string src = "?";
				string status = "";
				if (pmp) {

				}
				else {
					status = "位号未找到";
				}
				LOG("[流媒体]5秒没有活动的媒体客户端，断开媒体源，tag=%s,src=%s,status=%s", i.first.c_str(), src.c_str(), status.c_str());
				toErase.push_back(i.first);
			}
		}
		prj.m_csPrj.unlock_shared();

		for (int i = 0; i < toErase.size(); i++) {
			rpcSrv.m_mapPullerActive.erase(toErase[i]);
		}
	}
}
