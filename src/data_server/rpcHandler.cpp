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
#include "xiaot/xiaot.h"
#include "ioGW_localSerial.h"
#include "ioDev_iq60.h"
#include "ioChan.h"
#include "ioDev_genicam.h"
#include "streamServer.h"
#include "users/userMng.h"

rpcHandler rpcSrv;

void msgSinker_rpcHandler(MODULE_BUS_MSG& msg)
{
	if (msg.eventName == "ioDev.offline")
	{
		json jMsg = json::parse(msg.content);
		json j;
		j["addr"] = jMsg["ioAddr"];
		j["type"] = msg.eventName;
		rpcSrv.notify("ioEvent", j);
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

string rpcHandler::RPCError(int code,string msg)
{
	json jError = {
			{"code", code},
			{"message" , msg}
	};
	string error = jError.dump();
	return error;
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


string rpcHandler::rpc_db_select(json params,string& error, std::shared_ptr<TDS_SESSION> pSession)
{
	TIME_SELECTOR timeSelector;
	TAG_SELECTOR tagSelector;

	if (pSession->user != "")
	{
		params["root"] = pSession->org;
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
	
	//load from database 监测点类型过滤
	vector<MP*> tagSetTmp;
	vector<MP*> tagSet;
	prj.GetMPByTag(&tagSetTmp,tagSelector.tagExp);
	if (typeFilter != "")//has type filter
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

	string result = "";
	try
	{
		//DB_DATA_SET set;
		vector<string> tags;
		for (auto& i : tagSet)
		{
			tags.push_back(i->getTag());
		}
		//result = db.dataSet2String(set);
		db.Select_yyjson(tags, timeSelector, filter, result);
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
		str::replace(str, "$src_ip$", ip);
		}

	str = str::replace(str, "$dbPath$", db.m_path);
	str = str::replace(str, "$confPath$", tds->conf->projectConfPath);

	return str;
}


void selectFolderDlgThread(std::shared_ptr<TDS_SESSION> pSession, json params)
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


void openFileDlgThread(std::shared_ptr<TDS_SESSION> pSession,json params)
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

void saveFileDlgThread(std::shared_ptr<TDS_SESSION> pSession, json params)
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

bool rpcHandler::handleMethodCall(string method, json params, RPC_RESP& rpcResp, std::shared_ptr<TDS_SESSION> pSession)
{
	//method = str::removeChar(method,'_');
	//transform(method.begin(), method.end(), method.begin(), ::tolower);
	string& result = rpcResp.result;
	string& error = rpcResp.error;
	//可完全并发的命令
	//#region concurrent cmd
	if (method == "xiaot")
	{
		result = rpc_xiaot(params, error);
	}
	else if (method == "getAlarmCurrent")
	{
		json jFilter;
		jFilter["user"] = pSession->user;
		result = almSrv.getCurrent(jFilter);
	}
	else if (method == "getAlarmStatus")
	{
		json jFilter;
		jFilter["user"] = pSession->user;
		result = almSrv.getStatus(jFilter);
	}
	else if (method == "getAlarmUnack")
	{
		json jFilter;
		jFilter["user"] = pSession->user;
		result = almSrv.getUnack(jFilter);
	}
	else if (method == "getAlarmHistory")
	{
		result = almSrv.getHistory(params, pSession->user);
	}
	else if (method == "addAlarmEvent")
	{
		result = almSrv.rpc_addEvent(params);
	}
	else if (method == "updateAlarmStatus")
	{
		almSrv.rpc_updateStatus(params,rpcResp);
	}
	else if (method == "ackAlarmEvent")
	{
		ALARM_KEY ai;
		ai.tag = params["tag"].get<string>();
		ai.time = params["time"].get<string>();
		ai.type = params["type"].get<string>();
		string user = pSession->user;
		string info = params["ack_info"];
		almSrv.acknowledge(ai, info, user);
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
		std::thread t(openFileDlgThread, pSession, params);
		t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.saveFileDlg")
	{
		std::thread t(saveFileDlgThread, pSession, params);
		t.detach();
		result = "\"ok\"";
	}
	else if (method == "fs.selectFolderDlg")
	{
		std::thread t(selectFolderDlgThread, pSession, params);
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
	//#endregion

	//配置的使用与配置的修改之间不允许并发。使用读写锁保护
	if (method == "setconf")
	{
		unique_lock<shared_mutex> lock(prj.m_csPrj);
		result = rpc_setconf(params, error);
	}
	else
	{
		shared_lock<shared_mutex> lock(prj.m_csPrj);
		//以下配置使用 mo conf 和 io conf
		if (method == "input")
		{
			result = rpc_input(params, error);
		}
		else if (method == "output")
		{
			result = rpc_output(params, error);
		}
		else if (method == "getMpStatus")
		{
			result = rpc_getMpStatus(params, error, pSession);
		}
		else if (method == "getMoStatus")
		{
			result = rpc_getMoStatus(params, error,pSession);
		}
		else if (method == "getMoStatis")
		{
			rpc_getMoStatis(params, rpcResp, pSession);
		}
		else if (method == "getMoStatusTable")
		{
			result = rpc_getMoStatusTable(params, error,pSession);
		}
		else if (method == "getTopoList")
		{
			result = rpc_getTopoList(params, error,pSession);
		}
		else if (method == "getconf")
		{
			result = rpc_getconf(params, error);
		}
		else if (method == "getUsers")
		{
			json j = userMng.getUsers(pSession->user);
			result = j.dump(4);
		}
		else if (method == "getRoles")
		{
			json j = userMng.getRoles(pSession->user);
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
		else if (method == "getMpTypeList")
		{
			json list;
			prj.getMpTypeList(list);
			result = list.dump();
		}
		else if (method == "getMoTree")
		{
			string subTreeRoot = "";
			//如果指定了root，按照root取子树
			if (params != nullptr && params["root"] != nullptr && params["root"].get<string>() != "") //获取子树
			{
				subTreeRoot = params["root"].get<string>();
			}
			//没有root按照用户权限取子树
			else if (pSession->user != "")
			{
				json jUser = userMng.getUser(pSession->user);
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
		else if (method == "getMoCustomType")
		{
			json jList = json::array();
			for (auto& i : prj.m_mapCustomMOType)
			{
				jList.push_back(i.first);
			}
			result = jList.dump();
		}
		else if (method == "getmplist")//or getMpList or get_mp_list
		{
			json list;
			prj.getMpList(list);
			result = list.dump(2);
		}
		else if (method == "ioTree" || method == "iotree")
		{
			result = rpc_io_tree(params, error);
		}
		else if (method == "getChanStatus")
		{
			rpc_getChanStatus(params,rpcResp, pSession);
		}
		else if (method == "getDevStatus")
		{
			rpc_getDevStatus(params, rpcResp, pSession);
		}
		else if (method == "getChanVal")
		{
			rpc_getChanVal(params, rpcResp, pSession);
		}
		else if (method == "scanChannel" || method == "scanchannel")
		{
			result = rpc_io_scanChannel(params, error, pSession);
		}
		else if (method == "getIoDevStatis")
		{
			rpc_getIoDevStatis(params, rpcResp, pSession);
		}
		else if (method == "getDevList")
		{
			rpc_getDevList(params, rpcResp, pSession);
		}
		else if (method == "getStreamInfo")
		{
			result = rpc_getStreamInfo(params, error);
		}
		else if (method == "setStream")
		{
			result = rpc_setStream(params, error);
		}
	}


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
		else if (params["bin"] != nullptr)
		{
			long len = params["len"].get<long>();
			pSession->m_fileUploader.startWrite(p, len);
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
		fs::getFileList(fl,path, includeFolder, recursive);
		json j = fl;
		result = j.dump();
	}


	if (method == "db.select")
	{
		result = rpc_db_select(params, error,pSession);
	}
	if (method == "db.update")
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

	if (method == "getSessions")
	{
		result = ds.getSessionStatus(params);
	}

	if (method == "discoverDev")
	{
#ifdef ENABLE_GENICAM
		if (params["type"] == IO_DEV_TYPE::DEV::genicam)
		{
			json j = ioDev_genicam::listDevices();
			rpcResp.result = j.dump(2);
		}
#endif
	}
	if (method == "captureFrame")
	{
		
	}

#ifdef ENABLE_GENICAM
	if (method == "genicam.doCmd")
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

	if (method == "ui.maximize")
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

	if (method == "testCrash")
	{
		rpcResp.result = "\"ok\"";
		

		//程序崩溃
		int i = 13; int j = 0; int m = i / j;
		LOG("[debug]tds.Crash" + str::fromInt(m));
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


bool rpcHandler::handleDevRpcDispatch(string& strReq,json& jReq, std::shared_ptr<TDS_SESSION> pSession)
{
	string method = jReq["method"].get<string>();
	if (method == "devRegister")
	{
		string strIoAddr = jReq["ioAddr"].get<string>();
		ioDev* pIoDev = ioSrv.getIODev(strIoAddr);
		if (!pIoDev)
		{
			json jAddr;
			jAddr["id"] = strIoAddr;
			pIoDev = ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::DEV::tdsp_device);
		}
		pSession->type = "ioDev.tdsp";
		pIoDev->setIOSession(pSession);
		pIoDev->onRecvPkt(jReq);
		return true;
	}
	if (jReq.contains("ioAddr"))
	{
		//包有效性检测
		if (!jReq.contains("result") && !jReq.contains("error") && !jReq.contains("params"))
		{
			LOG("无效的设备通信rpc数据包,result,error,params中必须指定1个字段");
			return true;
		}

		// tds客户端 -> ioDev   
		if (!jReq.contains("result") && !jReq.contains("error"))
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
			LOG("RPC转发 客户端->设备:\r\n" + s + "\r\n");
			return true;
		}
		//ioDev -> tdsClient
		else if (jReq.contains("clientId") && jReq["clientId"].get<string>()!="tds")
		{
			string addr = jReq["clientId"].get<string>();
			shared_ptr<TDS_SESSION> p = ds.getTDSSession(addr);
			if (p != nullptr)
			{
				string s = jReq.dump() + "\n\n";
				p->send((char*)s.c_str(), s.length());
				LOG("RPC转发 设备->客户端:\r\n" + s + "\r\n");
			}
			else
			{
				string s = jReq.dump(2) + "\n\n";
				LOG("RPC转发 设备->客户端 未找到会话:\r\n" + s + "\r\n");
			}

			return true;
		}
		//ioDev -> tds
		else
		{
			string strIoAddr = jReq["ioAddr"].get<string>();
			ioDev* pIoDev = ioSrv.getIODev(strIoAddr);
			if (!pIoDev)
			{
				json jAddr;
				jAddr["id"] = strIoAddr;
				pIoDev = ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::DEV::tdsp_device);
			}
			pIoDev->setIOSession(pSession);
			pIoDev->onRecvPkt(jReq);
			LOG("TDSP响应:\r\n" + strReq + "\r\n");
			return true;
		}
	}

	return false;
}



void rpcHandler::handleRpcCall(string strReq, string& strResp,char*& binResp,int& iBinLen,bool bNeedLog, std::shared_ptr<TDS_SESSION> pSession)
{
	string error = "";
	RPC_RESP rpcResp;
	string method = "";
	json id = nullptr;
	bool bGB2312 = false;

	str::trim(strReq);
	if (strReq.length() == 0) 
	{
		json jError = {
		{"code", -32700},
		{"message" , "Parse error"}
		};
		error = jError.dump();
		goto HANDLE_END;
	}

	
	if (strReq.find("GB2312") != strReq.npos)
	{
		bGB2312 = true;
	}
	else if (strReq.find("gb2312") != strReq.npos) {
		bGB2312 = true;
	}
	
	if(bGB2312)
		strReq = charCodec::ansi2Utf8(strReq);

	strReq = ResolveTdsRpcEvnVar(strReq, pSession);


	try
	{
		//解析请求基本信息
		json jReq = json::parse(strReq);
		method = jReq["method"].get<string>();
		json params;
		if (jReq.contains("params"))
			params = jReq["params"];
		id = jReq["id"];
		pSession->lastMethodCalled = method;
			
		//对部分命令日志记录
		bNeedLog = needLog(method);
		if (bNeedLog)
			LOG("RPC请求:\r\n" + strReq + "\r\n");

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
		if (handleDevRpcDispatch(strReq,jReq, pSession))
			return;


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
				rpcResp.error = RPCError(RPC_ERROR::TEC_FAIL, "access denied, set user and token.");
				goto HANDLE_END;
			}
			string user = jReq["user"].get<string>();
			string token = jReq["token"].get<string>();

			if (!userMng.checkToken(user, token))
			{
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
		bool bHandled = handleMethodCall(method, params, rpcResp,pSession);
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
		LOG("handleRpcCall异常" + errorType);
		json jError = {
				{"code", -32700},
				{"message" , "Parse error," + errorType}
		};
		rpcResp.error = jError.dump();
		goto HANDLE_END;
	}

HANDLE_END:
	string strRespForLog = "";//对于某些内容特别长的数据包，省略一些内容进行日志记录
	if (rpcResp.error != "")
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"error\":" + rpcResp.error + ",\"id\":" + id.dump() + "}";
	}
	else if (rpcResp.result != "")
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id.dump() + ",\"result\":" + rpcResp.result + "}";

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
			LOG("RPC响应:\r\n" + strRespForLog + "\r\n");
		else if (bNeedLog)
			LOG("RPC响应:\r\n" + rpcResp.result + "\r\n");
	}


	//处理二进制响应
	if (rpcResp.iBinLen > 0)
	{
		binResp = rpcResp.binResult;
		iBinLen = rpcResp.iBinLen;
		rpcResp.binResult = NULL;
		rpcResp.iBinLen = 0;
		LOG("RPC响应: 二进制数据 len = " + str::fromInt(iBinLen));
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

string rpcHandler::rpc_output(json params, string& error)
{
	json val = "";
	if (params["val"] != nullptr)
		val = params["val"];
	else
		return "";

	string tag;
	if (params["tag"] != nullptr)
		tag = params["tag"].get<string>();

	MP* pmp = prj.GetMPByTag(tag);
	if (!pmp)
	{
		json jError = {
				{"code", TEC_TAG_NOT_EXIST},
				{"message" , "error: tag not exist"}
		};
		string error = jError.dump();
		return "!" + error;
	}

	if (val.is_boolean() && pmp->m_valType != VAL_TYPE::boolean)
	{
		json jError = {
				{"code", TEC_VAL_TYPE_ERROR},
				{"message" , "error: wrong val type , should be bool type "}
		};
		string error = jError.dump();
		return "!" + error;
	}
	else if (val.is_number() && pmp->m_valType != VAL_TYPE::Float)
	{
		json jError = {
				{"code", TEC_VAL_TYPE_ERROR},
				{"message" , "error: wrong val type , should be real type "}
		};
		string error = jError.dump();
		return "!" + error;
	}
	
	json jResp;
	if(pmp->output(val, jResp))
		return jResp.dump();
	else
	{
		json jError = {
		{"code", TEC_OUTPUT_EXECUTION_FAIL},
		{"message" , "error: output execution fail"}
		};
		string error = jError.dump();
		return "!" + error;
	}
}


string rpcHandler::rpc_input(json params, string& error)
{
	//parse param
	SYSTEMTIME stTimeStamp;
	string tag = "", cid = "",time="";
	json dataFile;
	json val = "";
	if (params.find("val") != params.end())
		val = params["val"];
	else
		return "";
	if (params.find("dataFile") != params.end())
		dataFile = params["dataFile"];
	if (params.find("tag") != params.end())
		tag = params["tag"].get<string>();
	if (params.find("cid") != params.end())
		cid = params["cid"].get<string>();
	if(tag==""&& cid!="")
		tag = ioSrv.getTag(cid);
	if (tag == "")
		return "";
	MP* pmp = prj.GetMPByTag(tag);
	if (!pmp)
	{
		json jError = {
				{"code", TEC_TAG_NOT_EXIST},
				{"message" , "error: tag not exist"}
		};
		string error = jError.dump();
		return "!" + error;
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
	return "ok";
}

string rpcHandler::rpc_getTopoList(json params, string& error,std::shared_ptr<TDS_SESSION> pSession)
{
	string path = tds->conf->projectConfPath + "/topo";
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

	if (pSession->user != "")
	{
		//删除没有权限的拓扑图
		for (int i = 0; i < topoList.size(); i++)
		{
			string& s = topoList[i];
			if (!userMng.checkTagPermission(pSession->user, s))
			{
				topoList.erase(topoList.begin() + i);
				i--;
			}
		}
	}

	json j = topoList;
	return j.dump();
}

void rpcHandler::rpc_getMoStatis(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession)
{
	string rootTag = "";
	if (params.contains("rootTag"))
	{
		rootTag = params["rootTag"].get<string>();
	}
	string devType = "*";
	if (params.contains("devType"))
	{
		devType = params["devType"].get<string>();
	}

	//仅统计1级设备
	vector<ioDev*> m_devList;
	for (auto& i : ioSrv.m_vecChild)
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


	json j;
	j["total"] = m_devList.size();
	j["online"] = onlineCount;
	j["offline"] = m_devList.size() - onlineCount;
	json jRlt;
	jRlt["smartDev"] = j;

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
		resp.result = jRlt.dump(4);
	}
}


string rpcHandler::rpc_getMoStatus(json params, string& error, std::shared_ptr<TDS_SESSION> pSession)
{
	if (params["type"] == nullptr)
	{
		error = RPCError(RPC_ERROR::TEC_FAIL, "type is not specified");
		return "";
	}
	string moType = params["type"].get<string>();
	string strList = "[";

	if (prj.m_mapCustomMOType.find(moType) != prj.m_mapCustomMOType.end())
	{
		vector<MO*> moList = prj.m_mapCustomMOType[moType];
		for (int i = 0; i < moList.size(); i++)
		{
			MO* pMo = moList[i];

			if(pSession->user != "")
			{
				if (!userMng.checkTagPermission(pSession->user, pMo->getTag()))
					continue;
			}
			
			nlohmann::ordered_json oneData;
			oneData["监控对象"] = pMo->getTag();
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



string rpcHandler::rpc_getMoStatusTable(json params, string& error, std::shared_ptr<TDS_SESSION> pSession)
{
	if (params["type"] == nullptr)
	{
		error = RPCError(RPC_ERROR::TEC_FAIL, "type is not specified");
		return "";
	}
	string moType = params["type"].get<string>();
	json jTable = json::array();
	json jTableHead = json::array();

	if (prj.m_mapCustomMOType.find(moType) != prj.m_mapCustomMOType.end())
	{
		vector<MO*> moList = prj.m_mapCustomMOType[moType];
		for (int i = 0; i < moList.size(); i++)
		{
			MO* pMo = moList[i];
			if (i == 0)
			{
				for (int j = 0; j < pMo->m_childMO.size(); j++)
				{
					MO* pChild = pMo->m_childMO[j];
					if (pChild->m_moType == MO_TYPE::mp)
					{
						MP* pmp = (MP*)pChild;
						jTableHead.push_back(pmp->m_strName);
					}
				}
				jTable.push_back(jTableHead);
			}

			json jTableRow;
			for (int j = 0; j < pMo->m_childMO.size(); j++)
			{
				MO* pChild = pMo->m_childMO[j];
				if (pChild->m_moType == MO_TYPE::mp)
				{
					MP* pmp = (MP*)pChild;
					jTableRow.push_back(pmp->m_curVal);
				}
			}
			jTable.push_back(jTableRow);
		}
		return jTable.dump();
	}
	else
	{
		json j = json::array();
		return j.dump();
	}
}

string rpcHandler::rpc_getMpStatus(json params, string& error, std::shared_ptr<TDS_SESSION> pSession)
{
	string szTag = "*"; //未指定位号默认查询所有位号
	if(params["tag"]!=nullptr)
		szTag = params["tag"].get<string>();
	string fmt = "table";
	if(params["fmt"]!=nullptr)
	 	fmt = params["fmt"].get<string>();
	json rtList;
	if (szTag == "*")
	{
		if(fmt=="tree")
		{
			json j = prj.getRT();
			string result = j.dump(4);
			return result;
		}
		else
		{
			if (pSession->user != "")
			{
				json jUser = userMng.getUser(pSession->user);
				for (map<string, MP*>::iterator it = prj.m_mapAllMP.begin(); it != prj.m_mapAllMP.end(); it++)
				{
					if(userMng.checkTagPermission(pSession->user,it->second->getTag()))
						rtList.push_back(it->second->getRTData(jUser["org"].get<string>()));
				}
				string result = rtList.dump(4);
				return result;
			}
			else
			{
				
				for (map<string, MP*>::iterator it = prj.m_mapAllMP.begin(); it != prj.m_mapAllMP.end(); it++)
				{
					rtList.push_back(it->second->getRTData());
				}
				string result = rtList.dump(4);
				return result;
			}
		}
	}
	else
	{
		std::vector<MP*> tagVec;
		prj.GetMPByTag(&tagVec,szTag);
		for(int i=0;i<tagVec.size();i++)
		{
			MP* pmp = tagVec.at(i);
			rtList.push_back(pmp->getRTData());
		}
		string result = rtList.dump(4);
		return result;
	}
}

string rpcHandler::rpc_getUsers(json params, string& error)
{

	return "";
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
			path = tds->conf->projectConfPath + "/" + path;
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
	if (type == "mo-tree")
	{
		string strData = params["conf"].dump(4);
		fs::writeFile(tds->conf->projectConfPath + "/mo.json", strData);
		//mo tree 热更新
		prj.clearChildren();
		prj.loadConf();
		return "\"ok\"";
	}
	else if (type == "io-tree")
	{
		string strData = params["conf"].dump(4);
		fs::writeFile(tds->conf->projectConfPath + "/io.json", strData);
		//io tree 热更新
		ioSrv.stop(); //退出所有工作线程
		ioSrv.clear();
		ioSrv.run();
		return "\"ok\"";
	}
	else if (type == "file")
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
		path = tds->conf->projectConfPath + "/" + path;
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
		path = tds->conf->projectConfPath + "/" + path;
		fs::createFolderOfPath(path);
		fs::writeFile(path, conf);
		return "ok";
	}
	return string();
}

string rpcHandler::rpc_heartbeat(json params, string& error , std::shared_ptr<TDS_SESSION> pSession)
{
	if (params.is_object())
	{
		if(params["clientName"] != nullptr)
			pSession->name = params["clientName"];
	}
	return "\"pong\"";
}

string rpcHandler::rpc_xiaot(json params, string& error)
{
	string reply = xiaot.getReply(params);
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
		ioGW_LocalSerial* pCom = (ioGW_LocalSerial*) pDev;
		if (pCom->OpenCom(params.dump()))
		{
			pCom->run();
			return "\"ok\"";
		}
		else
		{
			json jError = "fail," + pCom->m_strErrorInfo;
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

string rpcHandler::rpc_io_tree(json params, string& error)
{
	json j;
	ioSrv.toJson(j);
	return j.dump();
}

void rpcHandler::rpc_getChanStatus(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession)
{
	json list;
	ioSrv.getChanStatus(list);
	resp.result = list.dump(4);
}


void rpcHandler::rpc_getDevStatus(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession)
{
	string ioAddr = params["ioAddr"];
	ioDev* p = ioSrv.getIODev(ioAddr);
	if (p)
	{
		json status;
		//json channels = json::array();

		/*for (int i = 0; i < p->m_vecChild.size(); i++)
		{
			ioDev* pChild = p->m_vecChild[i];
			if (pChild->m_level == IO_DEV_LEVEL::channel)
			{
				ioChannel* pC = (ioChannel*)pChild;
				json jDe;
				jDe["ioAddr"] = pC->m_name;
				jDe["val"] = pC->m_curVal;
				channels.push_back(jDe);
			}
		}*/
		status["channels"] = p->m_jAcq;
		status["alarmStatus"] = p->m_jAlarmStatus;

		resp.result = status.dump();
	}
}

void rpcHandler::rpc_getDevList(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession)
{
	json j;  
	params["recursive"] = false;
	if (ioSrv.toJson(j, params))
	{
		resp.result = j.dump(2);
	}
}

void rpcHandler::rpc_getIoDevStatis(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession)
{
	
}


void rpcHandler::rpc_getChanVal(json params, RPC_RESP& resp, std::shared_ptr<TDS_SESSION> pSession)
{
	json list;
	ioSrv.getChanStatus(list);
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


string rpcHandler::rpc_io_scanChannel(json params, string& error, std::shared_ptr<TDS_SESSION> pSession)
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
		error = RPCError(RPC_ERROR::IO_DEV_NOT_FOUND, "io device not found");
		return "";
	}

	if (pDev->m_devType == "iq60-gateway")
	{
		ioDev_iq60* p = (ioDev_iq60*)pDev;
		json chanList;
		if (p->pIOSession == nullptr)
		{
			json jError = {
				{"code", -32603},
				{"message" , "设备不在线，请确认IQ60的连接配置，并在主动上送数据"}
			};
			error = jError.dump();
		}
		else if (p->scanChannel(chanList))
		{
			//比对p->m_vecChild是否已经存在,只发不存在的给前端
			for (int i = 0; i < chanList.size(); i++)
			{
				json jsubPkt = chanList.at(i);
				string straddr = jsubPkt["addr"];

				for (auto j : p->m_vecChild)
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
			result["ioAddr"] = p->getIOAddrStr();
			result["channels"] = chanList;


			//新建空闲设备通道
			for (int i = 0; i < chanList.size(); i++)
			{
				json jC = chanList[i];
				ioChannel* pC = new ioChannel;
				pC->m_mngStatus = IODEV_MNG_STATUS::spare;
				pC->loadConf(jC);
				p->addChild(pC);
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
	}
	else
	{
		json jError = {
			{"code", -32603},
			{"message" , "该ioAddr地址设备不是IQ60"}
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
		ioGW_LocalSerial* pls  =  (ioGW_LocalSerial*)i;
		json jComInfo;
		jComInfo["portNum"] = pls->m_devAddr;
		jComInfo["desc"] = pls->m_devTypeLabel;
		jComInfo["online"] = pls->m_bOnline;
		jComInfo["connected"] = pls->isOpen();
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
		ioGW_LocalSerial* p = (ioGW_LocalSerial*)pCom;
		p->closeCom();
		json j = "ok";
		return j.dump();
	}
	
	json j = "portNum " + portNum + " is not opened";
		
	return j.dump();
}


void rpcHandler::notify(string method, json params, std::shared_ptr<TDS_SESSION> orgSession)
{
	string notify = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params.dump() + "}";
	vector<shared_ptr<TDS_SESSION>> tdsSessions;
	tdsSessions = ds.m_vecTdsSession;
	
	
	for (int i=0;i<tdsSessions.size();i++)
	{
		shared_ptr<TDS_SESSION> p = tdsSessions[i];

		if (p == orgSession) //不发给来源
			continue;

		if(p->type == TDS_SESSION_TYPE::tdsClient)
			p->send((char*)notify.c_str(), notify.length());
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
