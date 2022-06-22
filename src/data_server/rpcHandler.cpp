#include "pch.h"
#include "rpcHandler.h"
#include "prj.h"
#include "as.h"
#include "mp.h"
#include "commSrv.h"
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
#include "xiaot/scriptHost.h"
#include "audioPlayer.h"

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
	m_DB = prj.DB;
	return true;
}

string rpcHandler::parseDataSelector(json params,TIME_SELECTOR& timeSelector, TAG_SELECTOR& tagSelector)
{
	//parse time param
	std::string strTime = "";
	std::string strStartDate, strEndDate;
	SYSTEMTIME stStartDate, stEndDate;
	if(params["time"].is_null()){return RPCError(TEC_PARAM_MISSING,"param missing:\"time\"");}
	try{strTime = params["time"].get<string>();}
	catch(...)
	{ return RPCError(TEC_WRONG_PARAM_FMT,"wrong param format:\"time\" param should be a string");}
	if(!timeSelector.init(strTime))
		return RPCError(TEC_TIME_SELECTOR_FMT_ERROR,"time selector format error:" + timeSelector.error);

	//parse tag param
	std::string strTag, strTagTmp;
	if(params["tag"].is_null()){return RPCError(TEC_PARAM_MISSING,"param missing:\"tag\"");}
	try{
		strTag = params["tag"].get<string>();
		if (params["root"] != nullptr)
		{
			string strRoot = params["root"].get<string>();
			if (strRoot != "")
			{
				strTag = strRoot + "." + strTag;
			}
		}
	}
	catch(...)
	{return RPCError(TEC_WRONG_PARAM_FMT,"wrong param format:\"tag\" param should be a string");}
	if (0 == strTag.length()) {
		return RPCError(TEC_WRONG_PARAM_FMT,"wrong param format:\"tag\" param can not be empty");
	}
	str::trimPrefix(strTag,prj.m_strName+".");
	if(!tagSelector.init(strTag))
		return RPCError(TEC_TAG_SELECTOR_FMT_ERROR,"tag selector format error:" + tagSelector.error);

	return "";
}


string rpcHandler::rpc_db_select(json params,string& error, RPC_SESSION session)
{
	TIME_SELECTOR timeSelector;
	TAG_SELECTOR tagSelector;

	if (session.user != "")
	{
		params["root"] = session.org;
	}
	error = parseDataSelector(params,timeSelector,tagSelector);
	if(error != "") return "";

	//parse type filter
	string typeFilter;
	if (params["type"] != nullptr)
		typeFilter = params["type"].get<string>();

	//parse attr filter
	string filter;
	if(params["filter"] != nullptr)
	 	filter = params["filter"].get<string>();
	
	//down sampling interval  针对高密度数据的下采样间隔。无需则为0
	DOWN_SAMPLING_PARAM dsp;
	json jDsi = params["dsi"];
	if (jDsi.is_number())
	{
		dsp.type = DST_Count;
		dsp.dsi = jDsi.get<int>();
	}
	else if (jDsi.is_string())
	{
		string sDsti = params["dsi"].get<string>();
		dsp.dsti = timeopt::dhmsSpan2Seconds(sDsti);
		if (dsp.dsti > 0)
			dsp.type = DST_Time;
	}
		

	
	
	//获取需要加载数据的位号集合
	vector<string> tags;
	if (tagSelector.singleMode)
	{
		tags.push_back(tagSelector.tagExp);
	}
	else
	{
		vector<MP*> tagSet;
		vector<MP*> tagSetTmp;
		prj.GetMPByTag(&tagSetTmp, tagSelector.tagExp);
		if (typeFilter != "")//has type filter //load from database 监测点类型过滤
		{
			for (auto& it : tagSetTmp)
			{
				if (it->getMpType() == typeFilter)
				{
					tagSet.push_back(it);
				}
			}
		}
		else
		{
			tagSet = tagSetTmp;
		}
		for (auto& i : tagSet)
		{
			tags.push_back(i->getTag());
		}
	}
	

	string result = "";
	try
	{
		db.Select_yyjson(tags, timeSelector, filter, result,dsp);
	}
	catch (std::exception& e)
	{
		json jerror = e.what();
		error = jerror.dump();
	}

	return result;
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
					error = RPCError(OS_fileNotExist, "file not exist");
				}
				else
					error = RPCError(TEC_FAIL, "fail");
			}
		}
	}
	else if (method == "fs.writeFile")
	{
		string p = params["path"].get<string>();

		if (params["data"] != nullptr)
		{
			string d = params["data"].get<string>();
			if (fs::writeFile(p, d))
			{
				result = "\"ok\"";
			}
			else
			{
				error = RPCError(TEC_FAIL, "fail");
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

bool rpcHandler::handleMethodCall_video(string method, json& params, RPC_RESP& rpcResp)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getStreamInfo")
	{
	result = rpc_getStreamInfo(params, error);
	}
	else if (method == "setStream")
	{
	result = rpc_setStream(params, error);
	}
#ifdef ENABLE_GENICAM
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
	else
	{
		bHandled = false;
	}

	return bHandled;
}


bool rpcHandler::handleMethodCall_db(string method, json& params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;

	
	if (method.find("db.") != string::npos)
	{
		if (!params.contains("tag"))
		{
			error = RPCError(RPC_ERROR::TEC_PARAM_MISSING, "missing param : tag");
		}
		else if (!params.contains("time"))
		{
			error = RPCError(RPC_ERROR::TEC_PARAM_MISSING, "missing param : time");
		}
		else if (method == "db.select")
		{
			result = rpc_db_select(params, error, session);
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
			db.Delete(tag, timeopt::str2st(time));
			result = "\"ok\"";
		}
		else if (method == "db.insert")
		{
			if (!params.contains("val"))
			{
				error = RPCError(RPC_ERROR::TEC_PARAM_MISSING, "missing param : val");
			}
			else
			{
				string tag = params["tag"].get<string>();
				string time = params["time"].get<string>();
				db.Insert(tag, timeopt::str2st(time), params["val"]);
				result = "\"ok\"";
			}
		}
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}



void threadAdd() {
	while (1)
	{
		std::shared_ptr<TDS_SESSION> t(new TDS_SESSION);
		ds.m_vecTdsSession.push_back(t);
	}
}

void threadErase() {
	while (1)
	{
		ds.m_vecTdsSession.erase(ds.m_vecTdsSession.begin());
	}
}

bool rpcHandler::handleMethodCall_debugFunc(string method, json& params, RPC_RESP& rpcResp)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	bool bHandled = true;
	if (method == "getSessions")
	{
		result = ds.getSessionStatus(params);
	}
	else if (method == "captureFrame")
	{

	}
	else if (method == "getSessionBuff")
	{
		string remoteAddr = params["remoteAddr"].get<string>();
		shared_ptr<TDS_SESSION> pSession = ds.getTDSSession(remoteAddr);

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
		shared_ptr<TDS_SESSION> pDestSession = ds.getTDSSession(tdsSession);
		if (pDestSession == nullptr)
		{
			return true;
		}
		pDestSession->send((char*)data.c_str(), data.length());
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
	if (method == "ioTree" || method == "iotree" || method == "getIOTree")
	{
		rpc_getIOTree(params, rpcResp,session);
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
	else if (method == "getDevStatus")
	{
		rpc_getDevStatus(params, rpcResp);
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
	else if (method == "getIoDevStatis")
	{
		rpc_getIoDevStatis(params, rpcResp);
	}
	else if (method == "getDevList")
	{
		rpc_getDevList(params, rpcResp);
	}
	else if (method == "addDev")
	{
		ioSrv.rpc_addDev(params,rpcResp);
	}
	else if (method == "deleteDev")
	{
		ioSrv.rpc_deleteDev(params, rpcResp);
	}
	else if (method == "modifyDev")
	{
		ioSrv.rpc_modifyDev(params, rpcResp);
	}
	else if (method == "disposeDev")
	{
		ioSrv.rpc_disposeDev(params, rpcResp);
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
				rpcResp.error = RPCError(RPC_ERROR::IO_devNotFound,"ioDev with specified ioAddr not found");
			}
		}
		else
		{
			rpcResp.error = RPCError(RPC_ERROR::IO_ioAddrNotSpecified, "ioAddr not specified in params");
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
				rpcResp.error = RPCError(RPC_ERROR::IO_devNotFound, "ioDev with specified ioAddr not found");
			}
		}
		else
		{
			rpcResp.error = RPCError(RPC_ERROR::IO_ioAddrNotSpecified, "ioAddr not specified in params");
		}
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
	string& result = rpcResp.result;
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
	return bHandled;
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
	else if (method == "setMOTree")
	{
		unique_lock<shared_mutex> lock(prj.m_csPrj);
		prj.clear();
		prj.loadConf(params);
		prj.saveConf();
		ioSrv.updateTag2IOAddrBinding();
		result = "\"ok\"";
	}
	else
	{
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		//以下配置使用 mo conf 和 io conf
		if (method == "input")
		{
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
		else if (method == "getMoStatus")
		{
			result = rpc_getMoStatus(params, error, session);
		}
		else if (method == "getMoOnlineStatus") //智能设备在线状态
		{
			result = rpc_getMoOnlineStatus(params, error);
		}
		else if (method == "getMoStatis")
		{
			rpc_getMoStatis(params, rpcResp, session);
		}
		else if (method == "getMoStatusTable")
		{
			rpc_getMoStatusTable(params, rpcResp,session);
		}
		else if (method == "getTopoList")
		{
			result = rpc_getTopoList(params, error,session);
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
		else if (method == "getMoTree" || method == "getMOTree")
		{
			string subTreeRoot = "";
			//如果指定了root，按照root取子树
			if (params != nullptr && params["root"] != nullptr && params["root"].get<string>() != "") //获取子树
			{
				subTreeRoot = params["root"].get<string>();
			}
			//没有root按照用户权限取子树
			else if (session.user != "")
			{
				json jUser = userMng.getUser(session.user);
				if (jUser != nullptr)
				{
					subTreeRoot = jUser["org"].get<string>();
				}
			}

			if (subTreeRoot != "")
			{
				subTreeRoot = TAG::trimRoot(subTreeRoot);
				params["root"] = subTreeRoot;
				MO* pmo = prj.GetMOByTag(subTreeRoot);
				if (pmo)
				{
					json j;
					pmo->toJson(j, params);
					j["root"] = subTreeRoot; //子树的根节点有root属性，表示根在总的mo树中的位号
					result = j.dump(4);
				}
			}
			else
			{
				json j;
				prj.toJson(j, params); //不包含通用mp的树，例如开关量，模拟量；但包含自定义值类型mp，例如 车闸，人闸，测试结果
				result = j.dump(4);
			}
		}
		else if (method == "getMoConf")
		{
			if (params["tag"] == nullptr)
			{
				rpcResp.error = RPCError(RPC_ERROR::TEC_FAIL, "请求中缺少tag字段");
				return true;
			}

			string tag = params["tag"].get<string>();
			if (session.org != "")
			{
				tag = TAG::addRoot(tag, session.org);
			}

			MO* pmo = prj.GetMOByTag(tag);
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
				rpcResp.error = RPCError(RPC_ERROR::TEC_FAIL, "没有找到位号");
			}
		}
		else if (method == "getMoCustomType" || method == "getMoTypes")
		{
			MO* pmo = nullptr;
			if (params != nullptr && params.contains("tag"))
			{
				string tag = params["tag"].get<string>();
				if (tag == "")
				{
					pmo = &prj;
				}
				else
					pmo = prj.GetMOByTag(tag);
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
				error = RPCError(RPC_ERROR::MO_specifiedTagNotFound, "未找到指定位号");
			}
		}
		else if (method == "getmplist")//or getMpList or get_mp_list
		{
			json list;
			prj.getMpList(list);
			result = list.dump(2);
		}
		else if (method == "getMOBindDev") {
			string tag = params["tag"].get<string>();
			ioDev* p = ioSrv.getIODevByTag(tag);
			if (p)
			{
				json j;
				json opt;
				opt["recursive"] = false;
				p->toJson(j,opt);
				result = j.dump(4);
			}
			else
			{
				json jErr = "device not found";
				error = jErr.dump();
			}
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
	else if (method == "getAlarmStatus")
	{
	json jFilter;
	result = almSrv.rpc_getStatus(jFilter, session);
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
		json j = userMng.getUsers(session.user);
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
		userMng.setUsers(params);
	}
	else if (method == "getUiTree")
	{
		result = userMng.m_jUI.dump(4);
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

bool rpcHandler::handleMethodCall(string method, json params, RPC_RESP& rpcResp, RPC_SESSION session)
{
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	//可完全并发的命令
	if (method == "xiaot")
	{
		result = rpc_xiaot(params, error);
	}
	else if (method == "addLog")
	{
		if (params["host"] != nullptr)
		{
			params["host"] = session.remoteAddr + ";" + params["host"].get<string>();
		}
		logSrv.rpc_addLog(params,session);
		result = "\"ok\"";
	}
	else if (method == "queryLog")
	{
		result = logSrv.rpc_queryLog(params,session);
	}
#ifdef ENABLE_JERRY_SCRIPT
	else if (method == "runScript")
	{
		sHost.rpc_runScript(params, rpcResp, session);
	}
	else if (method == "getScriptList")
	{
		sHost.rpc_getScriptList(params, rpcResp, session);
	}
	else if (method == "getScriptFile")
	{
		sHost.rpc_getScript(params, rpcResp, session);
	}
	else if (method == "setScriptFile")
	{
		sHost.rpc_setScript(params, rpcResp, session);
	}
#endif
	else if (method == "callDevMethod")
	{
		string tag = params["tag"].get<string>();
		tag = TAG::addRoot(tag, session.rootTag);
		ioDev* pd = ioSrv.getIODevByTag(tag);
		if(pd && pd->pIOSession)
		{	
			json jReq;
			jReq["jsonrpc"] = "2.0";
			jReq["method"] = params["method"];
			jReq["params"] = params["params"];
			jReq["id"] = 0;
			jReq["ioAddr"] = pd->getIOAddrStr();
			jReq["clientId"] = "tds";
			string sReq = jReq.dump() + "\n\n";

			pd->pIOSession->send((char*)sReq.c_str(), sReq.length());
		}
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
	if (handleMethodCall_debugFunc(method, params, rpcResp))
	{
		return true;
	}

	
	if (rpcResp.iBinLen > 0 || rpcResp.result != "" || error!="")
		return true;
	return false;
}




bool rpcHandler::needLog(string method)
{
	if (method == "fs.writeFile" ||
		method == "heartbeat" ||
		method == "getSessions"||
		method == "getMpStatus" ||
		method == "getMoStatus" ||
		method == "getMoStatusList" ||
		method == "getChanVal" ||
		method == "acq")
		return false;
	return true;
}

bool rpcHandler::handleDevRpcDispatch(string& strReq,json& jReq, RPC_RESP& rpcResp,std::shared_ptr<TDS_SESSION> pSession)
{
	string method = jReq["method"].get<string>();
	if (jReq.contains("tdsSession")) //使用tdsSession进行io透传
	{
		string tdsSession = jReq["tdsSession"].get<string>();
		shared_ptr<TDS_SESSION> pDestSession = ds.getTDSSession(tdsSession);
		
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
	//使用clientId进行透传机制取消。 不需要clientId.不再视为一种透传，视为转发
	/*else if (jReq.contains("ioAddr"))
	{
		string strIoAddr = jReq["ioAddr"].get<string>();
		ioDev* pIoDev = ioSrv.getIODev(strIoAddr);
		if (!pIoDev || pIoDev->pIOSession == nullptr)
		{
			return true;
		}
		jReq["clientId"] = pSession->getRemoteAddr();
		jReq.erase("user");
		jReq.erase("token");
		string s = jReq.dump() + "\n\n";
		pIoDev->pIOSession->send((char*)s.c_str(), s.length());
		LOG("[设备透传]客户端->设备:\r\n" + s + "\r\n");
		return true;
	}*/
	//设备TDSP命令转发
	else if (jReq.contains("ioAddr"))
	{
		string strIoAddr = jReq["ioAddr"].get<string>();
		pSession->ioAddr = strIoAddr;
		ioDev* pIoDev = ioSrv.getIODev(strIoAddr);
		if (!pIoDev)
		{
			rpcResp.error = RPCError(RPC_ERROR::IO_devNotFound, "未找到指定IO地址的IO设备");
			return true;
		}
		if (pIoDev->pIOSession == nullptr)
		{
			rpcResp.error = RPCError(RPC_ERROR::IO_devOffline, "设备离线");
			return true;
		}
		if (pIoDev->m_devType != IO_DEV_TYPE::DEV::tdsp_device)
		{
			rpcResp.error = RPCError(RPC_ERROR::IO_devTypeError, "IO设备类型错误");
			return true;
		}

		jReq["clientId"] = "tds";
		jReq.erase("user");
		jReq.erase("token");
		string method = jReq["method"].get<string>();
		json jParams = jReq["params"];
		json jId = jReq["id"];

		json jRlt,jErr;
		//发起同步请求，此处阻塞
		LOG("[TDSP转发]客户端->设备\r\n");
		if (pIoDev->call(method, jParams, jRlt, jErr))
		{
			if(jRlt!=nullptr)
				rpcResp.result = jRlt.dump();
			else if (jErr != nullptr)
				rpcResp.error = jErr.dump();
			else
			{
				LOG("[error][TDSP]TDSP响应数据包缺少result或者error字段");
			}
			LOG("[TDSP转发]设备->客户端\r\n");
		}
		else
		{
			rpcResp.error = RPCError(RPC_ERROR::IO_reqTimeout,"IO设备响应超时");
		}
		return true;
	}

	return false;
}

bool rpcHandler::isGB2312Pkt(string& req)
{
	//如果jsonRPC的json结构的第一个字段是charset，根据charset的参数决定编码类型
	int pos = req.find("GB2312");
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



void rpcHandler::handleRpcCall(string& strReq, string& strResp,char*& binResp,int& iBinLen,bool bNeedLog, std::shared_ptr<TDS_SESSION> pSession)
{
	string error = "";
	RPC_RESP rpcResp;
	string method = "";
	json id = nullptr;
	json clientId = nullptr;
	bool bGB2312 = false;

	strReq = str::trim(strReq);
	if (strReq.length() == 0) 
	{
		json jError = {
		{"code", -32700},
		{"message" , "Parse error"}
		};
		error = jError.dump();
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
		clientId = jReq["clientId"]; //tds edge模式使用
		pSession->lastMethodCalled = method;
			
		//对部分命令日志记录
		bNeedLog = needLog(method);
		if (bNeedLog)
			LOG("[trace]RPC请求:\r\n" + strReq + "\r\n");

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

		//设备类命令中继转发处理.返回true表示是设备中继命令.放在用户认证前面处理.
		if (!tds->conf->edge) //tds edge模式无需转发
		{
			if (handleDevRpcDispatch(strReq, jReq, rpcResp, pSession))
			{
				goto HANDLE_END;
			}
		}
		


		//访问控制
		if (method == "login")
		{
			rpcResp.result = rpc_login(params, error);
			if (error != "")
				rpcResp.error = error;
			goto HANDLE_END;
		}
		else if (method == "logout")
		{
			rpcResp.result = rpc_logout(params, error);
			if (error != "")
				rpcResp.error = error;
			goto HANDLE_END;
		}

		//没有打开权限控制，数据包也可以携带user，不进行验证，但是有权限控制。用于测试场景
		json jUser;
		if (jReq["user"] != nullptr)
		{
			pSession->user = jReq["user"].get<string>();
			jUser = userMng.getUser(pSession->user);
			if(jUser!=nullptr)
				pSession->org = jUser["org"].get<string>();
		}
			
		//用户认证
		if (tds->conf->enableAccessCtrl)
		{
			if (jReq["user"] == nullptr || jReq["token"] == nullptr)
			{
				//LOG("[warn]无效请求，未携带用户名与token,method=" + method);
				rpcResp.error = RPCError(RPC_ERROR::TEC_FAIL, "access denied, set user and token.");
				goto HANDLE_END;
			}
			string user = jReq["user"].get<string>();
			string token = jReq["token"].get<string>();

			if (!userMng.checkToken(user, token))
			{
				//LOG("[warn]认证失败，token验证未通过,user=%s,token=%s,method=%s",user.c_str(),token.c_str(),method.c_str());
				rpcResp.error = RPCError(RPC_ERROR::TEC_FAIL, "access denied; please login to get access token");
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
		bool bHandled = handleMethodCall(method, params, rpcResp,pSession->getRpcSession());
		if(!bHandled)
		{
			json jError = {
				{"code", -32601},
				{"message" , "Method not found"},
				{"method", method}
			};
			rpcResp.error = jError.dump();
			goto HANDLE_END;
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("[error]handleRpcCall异常" + errorType);
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
		strResp = "{\"jsonrpc\":\"2.0\",\"error\":" + rpcResp.error + ",\"id\":" + id.dump();
		if (tds->conf->edge)
		{
			strResp += ",\"ioAddr\":\"" + tds->conf->deviceID + "\"";
			if (clientId != nullptr)
			{
				strResp += ",\"clientId\":" +  clientId.dump();
			}
		}
		strResp += "}\n\n";
	}
	else if (rpcResp.result != "")
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":" + rpcResp.result;
		if (tds->conf->edge)
		{
			strResp += ",\"ioAddr\":\"" + tds->conf->deviceID + "\"";
			if (clientId != nullptr)
			{
				strResp += ",\"clientId\":" + clientId.dump();
			}
		}
		if (pSession->ioAddr != "")
		{
			strResp += ",\"ioAddr\":\"" + pSession->ioAddr + "\"";
		}
		strResp += "}\n\n";


		if (method == "fs.readFile")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$fileLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}
		else if (method == "getMoTree")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":\"$MoTreeJsonLen = " + str::fromInt(rpcResp.result.length()) + "$\"}";
		}
		else if (method == "getMoTree")
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
		binResp = rpcResp.binResult;
		iBinLen = rpcResp.iBinLen;
		rpcResp.binResult = NULL;
		rpcResp.iBinLen = 0;
		LOG("[trace]RPC响应: 二进制数据 len = " + str::fromInt(iBinLen));
	}
}


void rpcHandler::saveDataFromUrl(string& strUrl, SYSTEMTIME& stTime, string& strTag, string suffix)
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

	SYSTEMTIME stDateTime = stTime;

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
		resp.error = RPCError(RPC_ERROR::MO_specifiedTagNotFound, "specified tag not found:" + tag);
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
					resp.error = RPCError(RPC_ERROR::MO_currentValIsNull, "current value is null");
					return;
				}
			}
			else
			{
				resp.error = RPCError(RPC_ERROR::MO_outputValShouldBeBool, "output val should be bool type");
				return;
			}
		}
	}
	else if (!val.is_number() && pmp->m_valType == VAL_TYPE::Float)
	{
		resp.error = RPCError(RPC_ERROR::MO_outputValShouldBeNumber, "output val should be number type");
		return;
	}
	
	json rlt,err;
	if (pmp->output(val, rlt,err,true))
	{
		resp.result = rlt.dump();
	}
	else
	{
		resp.error = err.dump();
		LOG("[warn]输出失败," + resp.error);
	}
}


void rpcHandler::rpc_input(json params,RPC_RESP& resp, RPC_SESSION session)
{
	//parse param
	SYSTEMTIME stTimeStamp;
	string tag = "", ioAddr = "",time="";
	json dataFile;
	json val = "";
	if (params.find("val") != params.end())
		val = params["val"];
	else
	{
		resp.error = RPCError(TEC_PARAM_MISSING, "param val must be specified");
		return;
	}
	if (params.find("dataFile") != params.end())
		dataFile = params["dataFile"];
	if (params.find("tag") != params.end())
		tag = params["tag"].get<string>();
	if (params.find("ioAddr") != params.end())
		ioAddr = params["ioAddr"].get<string>();
	if (tag == "" && ioAddr == "")
	{
		resp.error = RPCError(TEC_PARAM_MISSING, "param ioAddr or tag must be specified");
		return;
	}

	if (tag != "")
	{
		MP* pmp = prj.GetMPByTag(tag);
		if (!pmp)
		{
			resp.error = RPCError(MO_specifiedTagNotFound, "tag not exist");
			return;
		}
		if (params.find("time") != params.end())
		{
			time = params["time"];
			stTimeStamp = timeopt::str2st(time);
		}
		else
		{
			GetLocalTime(&stTimeStamp);
		}
		pmp->input(val, &stTimeStamp, dataFile);
	}
	else if (ioAddr != "")
	{
		ioChannel* pC = ioSrv.getChanByIOAddr(ioAddr);
		pC->input(val);
	}
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

void rpcHandler::rpc_getMoStatis(json params, RPC_RESP& resp,RPC_SESSION session)
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
			tagBind = TAG::trimRoot(tagBind);
			rootTag = TAG::trimRoot(rootTag);

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
	json querier = nullptr;
	if (session.user != "")
		querier["user"] = session.user;
	if (rootTag != "")
		querier["rootTag"] = params["rootTag"];

	vector<ALARM_INFO*> vecAlarms = almSrv.tableCurrent.query(querier);
	int iAlarmCount = 0;
	int iWarnCount = 0;
	for (int i = 0; i < vecAlarms.size(); i++)
	{
		ALARM_INFO* pai = vecAlarms[i];
		if (pai->level == "alarm")
			iAlarmCount++;
		if (pai->level == "warn")
			iWarnCount++;
	}
	json jAlmStatis;

	//全局报警禁用功能
	if (!tds->conf->enableGlobalAlarm)
	{
		iAlarmCount = 0;
		iWarnCount = 0;
	}

	jAlmStatis["alarmCount"] = iAlarmCount;
	jAlmStatis["warnCount"] = iWarnCount;
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
		de["val"] = iAlarmCount;
		jRlt.push_back(de);

		de["tag"] = "statis.alarms.warnCount";
		de["val"] = iWarnCount;
		jRlt.push_back(de);

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

string rpcHandler::rpc_getMoStatus(json params, string& error,RPC_SESSION session)
{
	//检查mo类型参数是否填写
	if (params["type"] == nullptr)
	{
		error = RPCError(RPC_ERROR::TEC_FAIL, "type is not specified");
		return "";
	}
	//支持中文直接输入moType
	string moType = params["type"].get<string>();
	str::hanZi2Pinyin(moType, moType);

	string queryRootTag = session.org;
	if (params["rootTag"] != nullptr) 
	{
		string userQueryRootTag = params["rootTag"].get<string>();
		queryRootTag = TAG::addRoot(userQueryRootTag, queryRootTag);
	}
	string strList = "[";

	if (prj.m_mapCustomMOType.find(moType) != prj.m_mapCustomMOType.end())
	{
		vector<MO*> moList = prj.m_mapCustomMOType[moType];
		for (int i = 0; i < moList.size(); i++)
		{
			MO* pMo = moList[i];
			string sysTag = pMo->getTag();
			string queryTag = sysTag;

			//过滤用户权限
			if(session.user != "")
			{
				if (!userMng.checkTagPermission(session.user, sysTag))
					continue;
			}

			//过滤根mo
			if (queryRootTag != "")
			{
				if (sysTag.find(queryRootTag) == string::npos)
				{
					continue;
				}
				queryTag = str::trim(sysTag, queryRootTag + ".");
			}
			
			nlohmann::ordered_json oneData;
			oneData["监控对象"] = queryTag;

			//自定义监测对象类型，都判断一下是否是智能设备，也就是和ioDev绑定
			ioDev* piod = ioSrv.getIODevByTag(sysTag);
			if (piod)
			{
				oneData["在线"] = piod->m_bOnline;
			}
			else
			{
				oneData["在线"] = pMo->m_bOnline;
			}
			
			for (int j = 0; j < pMo->m_childMO.size(); j++)
			{
				MO* pChild = pMo->m_childMO[j];
				if (pChild->m_moType == MO_TYPE::mp)
				{
					MP* pmp = (MP*)pChild;
					oneData[pmp->m_strName] = pmp->m_curVal;
				}
			}
			if(strList != "[")
				strList += ",";

			strList += oneData.dump(); //此处json对象内的字段顺序按照监测点配置的顺序来排列，因此先序列化再拼接字符串
		}
		strList += "]";
		return strList;
	}
	else
	{
		json j = json::array();
		return j.dump();
	}
}



void rpcHandler::rpc_getMoStatusTable(json params, RPC_RESP& resp, RPC_SESSION session)
{
	//检查mo类型参数是否填写
	if (params["type"] == nullptr)
	{
		resp.error = RPCError(RPC_ERROR::TEC_FAIL, "type is not specified");
	}
	//支持中文直接输入moType
	string moType = params["type"].get<string>();
	str::hanZi2Pinyin(moType, moType);
	//查询根
	string rootTag;
	if (params["rootTag"] != nullptr) {
		rootTag = params["rootTag"];
	}
	rootTag = TAG::addRoot(rootTag, session.org);



	json jTable = json::array();
	json jTableHead = json::array();

	if (prj.m_mapCustomMOType.find(moType) != prj.m_mapCustomMOType.end())
	{
		vector<MO*> moList = prj.m_mapCustomMOType[moType];
		for (int i = 0; i < moList.size(); i++)
		{
			MO* pMo = moList[i];
			string tag = pMo->getTag();

			//过滤用户权限
			if (session.user != "")
			{
				if (!userMng.checkTagPermission(session.user, tag))
					continue;
			}

			//过滤根mo
			if (rootTag != "")
			{
				if (tag.find(rootTag) == string::npos)
				{
					continue;
				}
				tag = str::trim(tag, rootTag + ".");
			}
	
			//表头
			if (jTableHead.size() == 0)
			{
				jTableHead.push_back("位号");
				for (int j = 0; j < pMo->m_childMO.size(); j++)
				{
					MO* pChild = pMo->m_childMO[j];
					if (pChild->m_moType == MO_TYPE::mp)
					{
						MP* pmp = (MP*)pChild;
						jTableHead.push_back(pmp->m_strName);
					}
				}
				jTableHead.push_back("在线");
				jTableHead.push_back("更新时间");
				jTable.push_back(jTableHead);
			}

			//数据行
			json jTableRow;
			jTableRow.push_back(tag);
			for (int j = 0; j < pMo->m_childMO.size(); j++)
			{
				MO* pChild = pMo->m_childMO[j];
				if (pChild->m_moType == MO_TYPE::mp)
				{
					MP* pmp = (MP*)pChild;
					jTableRow.push_back(pmp->m_curVal);
				}
			}
			jTableRow.push_back(pMo->m_bOnline);
			jTableRow.push_back(timeopt::st2str(pMo->m_stDataLastUpdate));
			if(jTableRow!=nullptr && jTableRow.size() == jTableHead.size())
				jTable.push_back(jTableRow);
		}
		resp.result = jTable.dump();
	}
	else
	{
		json j = json::array();
		resp.result = j.dump();
	}
}

string rpcHandler::rpc_getMpStatus(json params, string& error, RPC_SESSION session, bool bValOnly)
{
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


	json rtList = json::array();
	json rtMap = json::object();
	//获取所有点
	if ( jTagQuerier == nullptr || (jTagQuerier.is_string() && jTagQuerier.get<string>() == "*"))
	{
		if(mode=="tree")
		{
			json j = prj.getRT();
			string result = j.dump(4);
			return result;
		}
		else
		{
			for (map<string, MP*>::iterator it = prj.m_mapAllMP.begin(); it != prj.m_mapAllMP.end(); it++)
			{
				string tag = it->second->getTag();

				if (session.user != "")
				{
					if (!userMng.checkTagPermission(session.user, tag))
						continue;
				}

				if (rootTag != "")
				{
					if (tag.find(rootTag) == string::npos)
						continue;
				}

				rtList.push_back(it->second->getRTData(rootTag,bValOnly));
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
	//模糊查询模式
	else if (jTagQuerier.is_string()) {
		string szTag = jTagQuerier.get<string>();
		std::vector<MP*> tagVec;
		prj.GetMPByTag(&tagVec, szTag);
		for (int i = 0; i < tagVec.size(); i++)
		{
			MP* pmp = tagVec.at(i);
			rtList.push_back(pmp->getRTData("", bValOnly));
		}
		string result = rtList.dump(4);
		return result;
	}
	else if(jTagQuerier.is_array())
	{
		for (auto& tag : jTagQuerier) {
			string sysTag = TAG::addRoot(tag, rootTag);
			MP* pmp = prj.getMp(sysTag);
			if(pmp)
				rtList.push_back(pmp->getRTData("", bValOnly));
			else
			{
				json j;
				j["tag"] = tag;
				j["time"] = nullptr;
				j["val"] = nullptr;
				rtList.push_back(j);
			}
		}
		string result = rtList.dump(4);
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

string rpcHandler::rpc_heartbeat(json params, string& error , RPC_SESSION session)
{
	if (params.is_object())
	{
		if(params["clientName"] != nullptr)
			session.name = params["clientName"];
	}
	return "\"pong\"";
}

string rpcHandler::rpc_xiaot(json params, string& error)
{
	string reply = tds->xiaoT->getReply(params);
	return reply;
}

string rpcHandler::rpc_logout(json params, string& error)
{
	try {
		string user = params["user"].get<string>();
		string pwd = params["pwd"].get<string>();
		json jInfo;
		if (userMng.checkLogin(user, pwd, jInfo))
		{
			return jInfo.dump(4);
		}
		else
		{
			error = RPCError(RPC_ERROR::TEC_FAIL, "fail");
		}
	}
	catch (std::exception& e)
	{
		error = RPCError(RPC_ERROR::TEC_FAIL, "request data error");
	}
	return "";
}

string rpcHandler::rpc_login(json params, string& error)
{
	try {
		string user = params["user"].get<string>();
		string pwd = params["pwd"].get<string>();
		json jInfo;
		if (userMng.checkLogin(user, pwd,jInfo))
		{
			return jInfo.dump(4);
		}
		else
		{
			error =  RPCError(RPC_ERROR::TEC_FAIL, "fail");
		}
	}
	catch (std::exception& e)
	{
		error =  RPCError(RPC_ERROR::TEC_FAIL, "request data error");
	}
	return "";
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

void rpcHandler::rpc_getIOTree(json params, RPC_RESP& resp, RPC_SESSION session)
{
	json j;

	//用户rootTag转系统rootTag
	if (params != nullptr && params.contains("rootTag"))
	{
		string rootTag = params["rootTag"].get<string>();
		rootTag = TAG::addRoot(rootTag, session.org);
		params["rootTag"] = rootTag;
	}

	ioSrv.toJson(j,params);
	resp.result = j.dump();
}

void rpcHandler::rpc_getChanStatus(json params, RPC_RESP& resp)
{
	json list;
	ioSrv.getChanStatus(list);
	resp.result = list.dump(4);
}


void rpcHandler::rpc_getDevStatus(json params, RPC_RESP& resp)
{
	string ioAddr = params["ioAddr"];
	ioDev* p = ioSrv.getIODev(ioAddr);
	if (p)
	{
		json status;
		json channels = json::array();

		p->m_csThis.lock_shared();
		for (int i = 0; i < p->m_channels.size(); i++)
		{
			ioChannel* pC = p->m_channels[i];
			json jDe;
			jDe["ioAddr"] = pC->getDevAddrStr();
			jDe["val"] = pC->m_curVal;
			channels.push_back(jDe);
		}
		//status["channels"] = p->m_jAcq;
		status["channels"] = channels;
		status["chanUpdateTime"] = timeopt::st2str(p->m_stLastChanDataTime);
		status["alarmStatus"] = p->m_jAlarmStatus;
		status["alarmUpdateTime"] = timeopt::st2str(p->m_stLastAlarmStatusTime);
		status["enableAlarm"] = tds->conf->enableGlobalAlarm;
		p->m_csThis.unlock_shared();
		resp.result = status.dump();
	}
}

void rpcHandler::rpc_getDevList(json params, RPC_RESP& resp)
{
	json j;  
	params["recursive"] = false;
	if (ioSrv.toJson(j, params))
	{
		resp.result = j.dump(2);
	}
}

void rpcHandler::rpc_getIoDevStatis(json params, RPC_RESP& resp)
{
	
}


void rpcHandler::rpc_getChanVal(json params, RPC_RESP& resp)
{
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
		error = RPCError(RPC_ERROR::TEC_FAIL, "必须指定ioAddr字段");
		return "";
	}

	string ioAddr = params["ioAddr"];
	ioDev* pDev = ioSrv.getIODev(ioAddr);
	if (!pDev)
	{
		error = RPCError(RPC_ERROR::IO_devNotFound, "io device not found");
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

	error = RPCError(TEC_STREAM_ID_NOT_FOUND, "stream id not found");
	return "";
}


string rpcHandler::rpc_getStreamInfo(json params,string& error)
{
	string streamId;
	if (params.find("streamId") != params.end())
		streamId = params["streamId"].get<string>();
	if (streamId == "")
	{
		error = RPCError(RPC_ERROR::TEC_PARAM_MISSING,"param missing,tag is not specified");
		return "";
	}

	streamSrvNode* pssn = streamSrv.getSrvNode(streamId);
	if(pssn->m_streamPusher == NULL)
	{
		error = RPCError(RPC_ERROR::TEC_NO_STREAM_SRC, "no stream src of this tag");
		return "";
	}

	if (pssn->m_streamPusher->m_streamInfo.w == 0 || pssn->m_streamPusher->m_streamInfo.h == 0)
	{
		error = RPCError(RPC_ERROR::TEC_VIDEO_PARAM_NOT_VALID, "video param is not valid");
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
	string notify = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params.dump() + "}";

	ds.m_mutexTdsSessionList.lock();
	vector<shared_ptr<TDS_SESSION>> tdsSessions;
	tdsSessions = ds.m_vecTdsSession;
	ds.m_mutexTdsSessionList.unlock();
	
	webSrvS->sendToWs(notify);
	webSrv->sendToWs(notify);

	for (int i=0;i<tdsSessions.size();i++)
	{
		shared_ptr<TDS_SESSION> p = tdsSessions[i];

		if (p == orgSession) //不发给来源
			continue;

		if (p->type == TDS_SESSION_TYPE::tdsClient)
		{
			p->sendContent = "text";
			p->send((char*)notify.c_str(), notify.length());
		}
	}
}

void rpcHandler::Notify(string strTag, string& szNotify)
{
	string str;

	vector<void*> clientList = ds.GetSessionList();
	for (int j = 0; j < clientList.size(); j++)
	{
		shared_ptr<TDS_SESSION> p = shared_ptr<TDS_SESSION>((TDS_SESSION*)clientList.at(j));
		if (p->iALProto == APP_LAYER_PROTO::HTTP || p->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP)
			continue;
		if (p->role != "m_ioSrv")
		{
			//if (m_pMonitorView)
			//{
			//	m_pMonitorView->StatisOnSend((char*)szNotify.c_str(), szNotify.size(), p->ip.c_str());
			//}
			if (p->encode == "utf8")
			{
				str = charCodec::ansi2Utf8(szNotify);
			}
			else
			{
				str = szNotify;
			}
			p->send((char*)str.data(), str.length());
		}
		else
		{
			map<string, string>::iterator i = p->mapTagDataSubscribe.begin();
			for (; i != p->mapTagDataSubscribe.end(); i++)
			{
				string tagSub = i->second;
				if (tagSub == strTag)
				{
					//if (m_pMonitorView)
					//{
					//	m_pMonitorView->StatisOnSend((char*)szNotify.c_str(), szNotify.size(), p->ip.c_str());
					//}
					if (p->encode == "utf8")
					{
						str = charCodec::ansi2Utf8(szNotify);
					}
					else
					{
						str = szNotify;
					}
					p->send((char*)str.data(), str.length());
				}
			}
		}
	}


	if (m_DataCenterClt.IsConnect())
	{
		/*if (m_pMonitorView)
		{
			string strTemp;
			strTemp=str::format("%s:%d", m_DataCenterClt.m_strServerIP, m_DataCenterClt.m_iServerPort);
			m_pMonitorView->StatisOnSend((char*)szNotify.c_str(), szNotify.size(), strTemp,"数据中心");
		}*/
		str = charCodec::ansi2Utf8(szNotify);
		m_DataCenterClt.SendData((char*)str.data(), str.length());
	}
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
