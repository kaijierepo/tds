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

rpcHandler tdsSrv;

void msgSinker_rpcHandler(MODULE_BUS_MSG& msg)
{
	if (msg.eventName == "ioDev.offline")
	{
		json jMsg = json::parse(msg.content);
		json j;
		j["addr"] = jMsg["ioAddr"];
		j["type"] = msg.eventName;
		tdsSrv.notify("ioEvent", j);
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


bool rpcHandler::Init()
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
	if(!timeSelector.Init(strTime))
		return RPCError(TEC_TIME_SELECTOR_FMT_ERROR,"time selector format error:" + timeSelector.error);

	//parse tag param
	std::string strTag, strTagTmp;
	if(params["tag"].is_null()){return RPCError(TEC_PARAM_MISSING,"param missing:\"tag\"");}
	try{strTag = params["tag"].get<string>();}
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


string rpcHandler::rpc_query(json params,string& error)
{
	TIME_SELECTOR timeSelector;
	TAG_SELECTOR tagSelector;
	error = parseDataSelector(params,timeSelector,tagSelector);
	if(error != "") return "";

	//parse type filter
	string typeFilter;
	if (params["type"] != nullptr)
		typeFilter = params["type"].get<string>();

	//parse attr filter
	string filter;
	if(params["attr"] != nullptr)
	 	filter = params["attr"].get<string>();
	
	//load from database
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
	DB_DATA_SET set;
	for(auto& i:tagSet)
	{
		db.SELECT(i->getTag(), timeSelector, filter, set);
	}
	string result = db.dataSet2String(set);
	
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

	str::replace(str, "$dbPath$", db.m_path);

	return str;
}

void openFileDlgThread(std::shared_ptr<TDS_SESSION> pSession,json params)
{
	string openFile = fs::GetOpenFile();
	if (openFile != "")
	{
		json p;
		p["path"] = openFile;
		p["caller"] = params["caller"];
		tdsSrv.notify("fs.openFileDlg", p);
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
	string file = fs::GetSaveFile((char*)filter.c_str(),(char*)title.c_str());
	if (file != "")
	{
		json p;
		p["path"] = file;
		p["caller"] = params["caller"];
		tdsSrv.notify("fs.saveFileDlg", p);
	}
}

string rpcHandler::handleMethodCall(string method, json params,string& error, std::shared_ptr<TDS_SESSION> pSession)
{
	string result = "";
	//可完全并发的命令
	//#region concurrent cmd
	if (method == "xiaot")
	{
		result = rpc_xiaot(params, error);
	}
	else if (method == "alarm.current")
	{
		result = almSrv.getCurrent();
	}
	else if (method == "alarm.status")
	{
		result = almSrv.getStatus();
	}
	else if (method == "alarm.unack")
	{
		result = almSrv.getUnack();
	}
	else if (method == "alarm.history")
	{
		result = almSrv.getHistory(params);
	}
	else if (method == "alarm.add_event")
	{
		result = almSrv.rpc_addEvent(params);
	}
	else if (method == "alarm.update_status")
	{
		result = almSrv.rpc_updateStatus(params);
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
	else if (method == "fs.openFolder")
	{
		string s = params["path"];
		s = str::replace(s,"/", "\\");
		wstring ws = charCodec::utf8toUtf16(s);
		ShellExecuteW(NULL, L"open", L"explorer.exe", ws.c_str(), NULL, SW_SHOWNORMAL);
		result = "\"ok\"";
	}
	else if (method == "fs.openDEFolder")
	{
		string tag = params["tag"];
		string time = params["time"];
		string path = db.m_path  + db.getPath_deFile(tag, timeopt::str2st(time));

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
		else if (method == "rt")
		{
			result = rpc_rt(params, error);
		}
		else if (method == "query")
		{
			result = rpc_query(params, error);
		}
		else if (method == "getconf")
		{
			result = rpc_getconf(params, error);
		}
		else if (method == "io.tree")
		{
			result = rpc_io_tree(params, error);
		}
		else if (method == "io.scanChannel")
		{
			result = rpc_io_scanChannel(params, error, pSession);
		}
		else if (method == "getStreamInfo")
		{
			result = rpc_getStreamInfo(params, error);
		}
	}


	//文件操作
	if (method == "fs.readFile")
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
  	else if (method == "fs.writeFile")
	{
		string p = params["path"].get<string>();
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
	else if (method == "getCurDir")
	{
		WCHAR buff[300] = { 0 };
		GetCurrentDirectoryW(300, buff);
		wstring s = buff;
		json j = charCodec::utf16toUtf8(s);
		result = j.dump();
	}

	if (method == "db.update")
	{
		string tag = params["tag"].get<string>();
		string time = params["time"].get<string>();
		json val = params["val"];
		db.UPDATE(tag, timeopt::str2st(time), val);
		result = "\"ok\"";
	}

	if (method == "sessionStatus")
	{
		result = ds.getSessionStatus(params);
	}

	return result;
}




bool rpcHandler::needLog(string method)
{
	if (method == "fs.writeFile" ||
		method == "heartbeat" ||
		method == "sessionStatus"||
		method == "rt")
		return false;
	return true;
}

void rpcHandler::handleRpcCall(string strReq, string& strResp, std::shared_ptr<TDS_SESSION> pSession)
{
	string error = "";
	string result = "";
	string method = "";
	string id = "null";
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
		pSession->lastMethodCalled = method;
		json params = jReq["params"];
		json jId = jReq["id"];
		if (jId != nullptr)
		{
			if (jId.is_number_integer())
			{
				id = str::fromInt(jId.get<int>());
			}
			else
			{
				id = jId.get<string>();
			}
		}

		//对部分命令日志记录
		if(needLog(method))
			LOG("RPC call <--:\r\n" + strReq + "\r\n");

		//心跳最先处理
		if (method == "heartbeat")
		{
			if (params.is_object())
			{
				if (params["clientName"] != nullptr)
					pSession->name = params["clientName"];
				if (params["echo"] != nullptr)
				{
					if (params["echo"].get<bool>() == false)
					{
						return;
					}
				}
			}
			result = "\"pong\"";
			goto HANDLE_END;
		}
		

		//先使用外部注册的handler受理请求
		if (m_pluginHandler)
		{
			result = m_pluginHandler(strReq, strResp, error);
		}
		if (result != "" || error != "")
		{
			goto HANDLE_END;
		}

		//tds自身受理
		//result is a json string
		result = handleMethodCall(method, params,error,pSession);
		if(result == "" && error == "")
		{
			json jError = {
				{"code", -32601},
				{"message" , "Method not found"},
				{"method", method}
			};
			error = jError.dump();
			goto HANDLE_END;
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		json jError = {
				{"code", -32700},
				{"message" , "Parse error," + errorType}
		};
		error = jError.dump();
		goto HANDLE_END;
	}

HANDLE_END:

	if (error.length() == 0)
	{
		if(result.length() == 0)
		{
			json jError = {
				{"code", -32603},
				{"message" , "Internal error"}
			};
			error = jError.dump();
		}
	}

	string strRespForLog = "";//对于某些内容特别长的数据包，省略一些内容进行日志记录
	if (error != "")
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"error\":" + error + ",\"id\":" + id +  "}";
	}
	else
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id + ",\"result\":" + result + "}";

		if (method == "fs.readFile")
		{
			strRespForLog = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id + ",\"result\":\"$fileLen = " + str::fromInt(result.length()) + "$\"}";
		}
	}
	

	if(strRespForLog!="")
		LOG("RPC return --> :\r\n" + strRespForLog + "\r\n");
	else if(needLog(method))
		LOG("RPC return --> :\r\n" + strResp + "\r\n");
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
	if (params.find("val") != params.end())
		val = params["val"];
	else
		return "";

	string tag;
	if (params.find("tag") != params.end())
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
	else if (val.is_number() && pmp->m_valType != VAL_TYPE::real)
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

string rpcHandler::rpc_rt(json params, string& error)
{
	string szTag = params["tag"].get<string>();
	string fmt = "table";
	if(params["fmt"]!=nullptr)
	 	fmt = params["fmt"].get<string>();
	json rtList;
	if (szTag == "*")
	{
		if(fmt=="tree")
		{
			json j = prj.getRT();
			string strResult = j.dump(4);
			return strResult;
		}
		else
		{
			for (map<string, MP*>::iterator it = prj.m_mapAllMP.begin(); it != prj.m_mapAllMP.end(); it++) 
			{
				rtList.push_back(it->second->getRTData());
			}
			string strResult = rtList.dump(4);
			return strResult;
		}
	}
	else
	{
		MP* pmp = prj.GetMPByTag(szTag);
		if (pmp)
		{
			rtList.push_back(pmp->getRTData());
		}
		string strResult = rtList.dump(4);
		return strResult;
	}
}

string rpcHandler::rpc_getconf(json params, string& error)
{
	string type = "";
	if(params.find("type") != params.end())
		 type = params["type"].get<string>();
	if (type == "mo-tree")
	{
		json j;
		prj.toJson(j, params); //不包含通用mp的树，例如开关量，模拟量；但包含自定义值类型mp，例如 车闸，人闸，测试结果
		string conf = j.dump(4);
		return conf;
	}
	else if(type == "io-tree")
	{
		string conf;
		fs::readFile(tds->conf->projectConfPath + "\\io.json", conf);
		if (conf == "")
		{
			return "[]";
		}
		return conf;
	}
	else if(type == "mp-list")
	{
		json mpList;
		prj.getMpList(mpList);
		return mpList.dump();
	}
	else if(type == "mp-type-list")
	{
		json list;
		prj.getMpTypeList(list);
		return list.dump();
	}
	else if (type == "file")
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
			path = tds->conf->projectConfPath + "\\" + path;
			path::normalization(path);
			vector<string> fl = fs::getFileList(path);
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
		fs::writeFile(tds->conf->projectConfPath + "\\mo.json", strData);
		//mo tree 热更新
		prj.clearChildren();
		prj.loadConf();
		return "\"ok\"";
	}
	else if (type == "io-tree")
	{
		string strData = params["conf"].dump(4);
		fs::writeFile(tds->conf->projectConfPath + "\\io.json", strData);
		//io tree 热更新
		ioSrv.m_vecChild.clear();
		ioSrv.loadConf();
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
		path = tds->conf->projectConfPath + "\\" + path;
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
		path = tds->conf->projectConfPath + "\\" + path;
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
			return jError.dump();
		}
	}
	else
	{
		json jError = "fail,portNum not found";
		return jError.dump();
	}
}

string rpcHandler::rpc_io_tree(json params, string& error)
{
	json j;
	ioSrv.toJson(j);
	return j.dump();
}


string rpcHandler::rpc_io_scanChannel(json params, string& error, std::shared_ptr<TDS_SESSION> pSession)
{
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
		if (p->scanChannel(chanList))
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
			result["ioAddr"] = p->m_devAddr;
			result["channels"] = chanList;

			return result.dump();
		}
	}


	json jError = {
				{"code", -32603},
				{"message" , "Internal error"}
	};
	error = jError.dump();

	return "";
}


string rpcHandler::rpc_getStreamInfo(json params,string& error)
{
	string tag;
	if (params.find("tag") != params.end())
		tag = params["tag"].get<string>();
	if (tag == "")
	{
		error = RPCError(RPC_ERROR::TEC_PARAM_MISSING,"param missing,tag is not specified");
		return "";
	}
	MP* pmp = prj.getMp(tag);
	if(pmp == NULL)
	{
		error = RPCError(RPC_ERROR::TEC_TAG_NOT_EXIST, "tag not exist");
		return "";
	}

	if (pmp->m_streamInfo.w == 0 || pmp->m_streamInfo.h == 0)
	{
		error = RPCError(RPC_ERROR::TEC_VIDEO_PARAM_NOT_VALID, "video param is not valid");
		return "";
	}

	json jSi;
	jSi["w"] = pmp->m_streamInfo.w;
	jSi["h"] = pmp->m_streamInfo.h;
	jSi["type"] = pmp->m_streamInfo.type;
	
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
		jComInfo["open"] = pls->isOpen();
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
		ioSrv.deleteChild(p);
		delete p;
		json j = "ok";
		return j.dump();
	}
	
	json j = "portNum " + portNum + " is not opened";
		
	return j.dump();
}


void rpcHandler::notify(string method, json params)
{
	string notify = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"param\":" + params.dump() + "}";
	vector<shared_ptr<TDS_SESSION>> tdsSessions = ds.m_vecTdsSession;
	for (int i=0;i<tdsSessions.size();i++)
	{
		shared_ptr<TDS_SESSION> p = tdsSessions[i];
		if(p->type == TDS_SESSION_TYPE::rpc)
			p->send((char*)notify.c_str(), notify.length());
	}
}

void rpcHandler::Notify(string strTag, string& szNotify)
{
	string str;

	for (int i = 0; i < m_vecTLServer.size(); i++)
	{
		CTLServer* pTLSrv = m_vecTLServer.at(i);
		vector<void*> clientList = pTLSrv->GetSessionList();
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
