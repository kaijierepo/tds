#include "pch.h"
#include "tdspSrv.h"
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

tdsServer tdsSrv;

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

tdsServer::tdsServer()
{
	m_pluginHandler = NULL;
}

tdsServer::~tdsServer()
{

}


bool tdsServer::Init()
{
	m_DB = prj.DB;

	string strDSIP = tds->conf->dataCenterIp;
	str::removeChar(strDSIP, ' ');
	if (strDSIP.length() > 0)
	{
		string str = "";
		//..todo
		m_DataCenterClt.heartbeat.resize(str.length());
		memcpy(m_DataCenterClt.heartbeat.data(), str.data(), str.length());
		m_DataCenterClt.Run(this, strDSIP.c_str(), 2107);
	}
	return true;
}

string tdsServer::RPCError(int code,string msg)
{
	json jError = {
			{"code", code},
			{"message" , msg}
	};
	string error = jError.dump();
	return "!" + error;
}

string tdsServer::parseDataSelector(json params,TIME_SELECTOR& timeSelector, TAG_SELECTOR& tagSelector)
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


string tdsServer::rpc_query(json params)
{
	TIME_SELECTOR timeSelector;
	TAG_SELECTOR tagSelector;
	string error = parseDataSelector(params,timeSelector,tagSelector);
	if(error != "") return error;

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


string tdsServer::ResolveTdsRpcEvnVar(string strIn, std::shared_ptr<TDS_SESSION> pSession)
{
	//使用正则搜寻 ${XXX}
	//"${src_ip}" 替换成 pSession->ip
	string str = strIn;
	if (str.find("${src_ip}") != str.npos) {
		string ip = pSession->ip;
		string::size_type pos = pSession->ip.find(":");
		if (std::string::npos != pos) {
			ip = pSession->ip.substr(0, pos);
		}
		str::replace(str, "${src_ip}", ip);
		}
	return str;
}

string tdsServer::handleMethodCall(string method, json params)
{
	string result = "";
	if (method == "heartbeat")
	{
		result = rpc_heartbeat(params);
	}
	else if (method == "xiaot")
	{
		result = rpc_xiaot(params);
	}
	else if (method == "input")
	{
		result = rpc_input(params);
	}
	else if (method == "output")
	{
		result = rpc_output(params);
	}
	else if (method == "rt")
	{
		result = rpc_rt(params);
	}
	else if (method == "query")
	{
		result = rpc_query(params);
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
	else if (method == "getconf")
	{
		result = rpc_getconf(params);
	}
	else if (method == "setconf")
	{
		result = rpc_setconf(params);
	}
	else if (method == "com.open")
	{
		result = rpc_openCom(params);
	}
	else if (method == "com.close")
	{
		result = rpc_closeCom(params);
	}
	else if (method == "com.list")
	{
		result = rpc_com_list(params);
	}
	return result;
}


void tdsServer::handleRpcCall(string strReq, string& strResp, std::shared_ptr<TDS_SESSION> pSession)
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


	if (m_pluginHandler)
	{
		if (m_pluginHandler(strReq, strResp))//handled
			return;
	}

	try
	{
		json jReq = json::parse(strReq);

		LOG("[trace]tdsRPC call <--:\r\n" + strReq + "\r\n");

		method = jReq["method"].get<string>();
		json params = jReq["params"];
		json jId = jReq["id"];
		if(jId != nullptr)
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
		//result is a json string
		result = handleMethodCall(method, params);
		if(result == "")
		{
			json jError = {
				{"code", -32601},
				{"message" , "Method not found"}
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
		else if(result[0] == '!')
		{
			error = result.substr(1,result.length() - 1);
		}
	}

	if (error != "")
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"error\":" + error + ",\"id\":" + id +  "}";
	}
	else
	{
		strResp = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"id\":" + id + ",\"result\":" + result + "}";
	}
	

	LOG("[trace]tdsRPC return --> :\r\n" + strResp + "\r\n");

}

void tdsServer::ConnStatusChange(ConnInfo* connInfo, bool bIsConn)
{
}

void tdsServer::OnRecvData_TCPClient(char* pData, int iLen, ConnInfo* connInfo)
{
}

void tdsServer::saveDataFromUrl(string& strUrl, SYSTEMTIME& stTime, string& strTag, string suffix)
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

string tdsServer::rpc_output(json params)
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

	if (val.is_boolean() && pmp->m_valType != DATA_TYPE::switching)
	{
		json jError = {
				{"code", TEC_VAL_TYPE_ERROR},
				{"message" , "error: wrong val type , should be switching type "}
		};
		string error = jError.dump();
		return "!" + error;
	}
	else if (val.is_number() && pmp->m_valType != DATA_TYPE::real)
	{
		json jError = {
				{"code", TEC_VAL_TYPE_ERROR},
				{"message" , "error: wrong val type , should be real type "}
		};
		string error = jError.dump();
		return "!" + error;
	}
	
	if(pmp->outputVal(val))
		return "\"ok\"";
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


string tdsServer::rpc_input(json params) 
{
	//parse param
	SYSTEMTIME stTimeStamp;
	string tag = "", cid = "",time="";
	bool bHavePic = false;
	json val = "";
	if (params.find("val") != params.end())
		val = params["val"];
	else
		return "";
	if (params.find("pic") != params.end())
		bHavePic = params["pic"].get<bool>();
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

	pmp->inputVal(val, &stTimeStamp, bHavePic);
	return "ok";
}

string tdsServer::rpc_rt(json params)
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

string tdsServer::rpc_getconf(json params)
{
	string type = "";
	if(params.find("type") != params.end())
		 type = params["type"].get<string>();
	if (type == "mo-tree")
	{
		json j;
		prj.saveConf(j, "exclude-common-mp"); //不包含通用mp的树，例如开关量，模拟量；但包含自定义值类型mp，例如 车闸，人闸，测试结果
		string conf = j.dump(4);
		return conf;
	}
	else if (type == "mo-mp-tree")
	{
		string conf;
		fs::readFile(tds->conf->projectConfPath + "\\mo.json", conf);
		if (conf == "")
		{
			json j;
			j["name"] = "空项目";
			j["type"] = "project";
			j["children"] = json::array();
			conf = j.dump(4);
		}
		return conf;
	}
	else if(type == "io-tree")
	{
		string conf;
		fs::readFile(tds->conf->projectConfPath + "\\io.json", conf);
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
		return rpc_getconffile(params);
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

string tdsServer::rpc_setconf(json params)
{
	//按照类型配置
	string type = "";
	if (params.find("type") != params.end())
		type = params["type"].get<string>();
	if (type == "mo-mp-tree")
	{
		string strData = params["conf"].dump(4);
		fs::writeFile(tds->conf->projectConfPath + "\\mo.json", strData);
		prj.m_childMO.clear();
		prj.loadConf();
		return "ok";
	}
	else if (type == "io-tree")
	{
		string strData = params["conf"].dump(4);
		fs::writeFile(tds->conf->projectConfPath + "\\io.json", strData);
		return "ok";
	}
	else if (type == "file")
	{
		return rpc_setconffile(params);
	}

	return "";
}

string tdsServer::rpc_getconffile(json params)
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

string tdsServer::rpc_setconffile(json params)
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

string tdsServer::rpc_heartbeat(json params)
{
	return "\"pong\"";
}

string tdsServer::rpc_xiaot(json params)
{
	string reply = xiaot.getReply(params);
	return reply;
}



string tdsServer::rpc_openCom(json params)
{
	ioDev* pDev = NULL;
	string portNum = params["portNum"].get<string>();
	pDev = ioSrv.getIODev(portNum);
	if (pDev)
	{
		json jError = "ok";
		return jError.dump();
	}

	ioGW_LocalSerial* pCom = new ioGW_LocalSerial();
	if (pCom->OpenCom(params.dump()))
	{
		pCom->run();
		ioSrv.m_vecChild.push_back(pCom);
		return "\"ok\"";
	}
	else
	{
		json jError = "fail," + pCom->m_strErrorInfo;
		delete pCom;
		return jError.dump();
	}
}

string tdsServer::rpc_com_list(json params)
{
	/*
	vector<string> aryName = sys::getCOMList();
	json result;
	for (auto& i : aryName)
	{
		json jComInfo;
		jComInfo["portNum"] = i;
		jComInfo["desc"] = "";
		result.push_back(jComInfo);
	}*/

	vector<sys::COM_INFO> ary;
	ary = sys::getCOMInfoList();
	json result;
	for (auto& i : ary)
	{
		sys::COM_INFO ci = i;
		json jComInfo;
		jComInfo["portNum"] = ci.portNum;
		jComInfo["desc"] = ci.desc;
		result.push_back(jComInfo);
	}
	return result.dump();
}


string tdsServer::rpc_closeCom(json params)
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

vector<shared_ptr<TDS_SESSION>> tdsServer::GetAllSession()
{
	vector<shared_ptr<TDS_SESSION>> vecSess;
	for (int i = 0; i < m_vecTLServer.size(); i++)
	{
		CTLServer* pTLSrv = m_vecTLServer.at(i);
		vector<void*> clientList = pTLSrv->GetSessionList();
		for (int j = 0; j < clientList.size(); j++)
		{
			shared_ptr<TDS_SESSION> p = std::shared_ptr<TDS_SESSION>((TDS_SESSION*)clientList.at(j));
			vecSess.push_back(p);
		}
	}
	return vecSess;
}

void tdsServer::Notify(string strTag, string& szNotify)
{
	string str;

	for (int i = 0; i < m_vecTLServer.size(); i++)
	{
		CTLServer* pTLSrv = m_vecTLServer.at(i);
		vector<void*> clientList = pTLSrv->GetSessionList();
		for (int j = 0; j < clientList.size(); j++)
		{
			shared_ptr<TDS_SESSION> p = shared_ptr<TDS_SESSION>((TDS_SESSION*)clientList.at(j));
			if (p->iALProto == APP_LAYER_PROTO_TYPE::PROTOCOL_HTTP || p->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP)
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
