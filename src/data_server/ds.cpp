#include "pch.h"
#include "ds.h"
#include "logger.h"
#include "prj.h"
#include "httplib.h"
#include "data_server/dsClientStream.h"
#include "mp.h"
#include "rpcHandler.h"
#include "video/remoteDesktopServer.h"
#include <memory>
#include "ioSrv.h"
#include "tcpClt.h"
#include "commSrv.h"
#include "ioDev_genicam.h"
#include "streamServer.h"
#include "conf.h"
#include "tds.h"
#include "users/userMng.h"
#include "ioDev_tdsp.h"
#include "sha1.hpp"
#include "ioGW_LocalSerial.h"


dataServer ds;
using namespace httplib;

httplib::Server::HandlerResponse handleFilePermission(const httplib::Request& req, httplib::Response& res)
{
	if ((req.path.find("files") != string::npos))
	{
		string s = req.get_header_value("Cookie");
		vector<string> params;
		str::split(params, s, ";");
		map<string, string> mapKV;
		for (int i = 0; i < params.size(); i++)
		{	
			string p = params[i];
			vector<string> ary;
			str::split(ary, p, "=");
			if (ary.size() == 2)
			{
				mapKV[str::trim(ary[0])] = str::trim(ary[1]);
			}
		}

		if (!tds->conf->authDownload)
		{
			return httplib::Server::HandlerResponse::Unhandled;
		}

		
		if (mapKV.find("user") == mapKV.end() || mapKV.find("token") == mapKV.end())
		{
			res.status = 401;
			return httplib::Server::HandlerResponse::Handled;
		}
		else
		{
			string user = mapKV["user"];
			string token = mapKV["token"];

			if (userMng.checkToken(user, token))
			{
				return httplib::Server::HandlerResponse::Unhandled;
			}
			else
			{
				res.status = 401;
				return httplib::Server::HandlerResponse::Handled;
			}
		}
	}
	return httplib::Server::HandlerResponse::Unhandled;
}

//自动找到 files/apk 文件夹下面最新的apk文件并下载
void handleGet_apk(const httplib::Request& req, httplib::Response& res)
{
	res.status = 301;
	string redirectPath = "/files/apk/";
	string localPath = fs::appPath() + "/files/apk";
	vector<string> fl;
	fs::getFileList(fl, localPath);

	map<string, string> fil;

	for (auto& i : fl)
	{
		fs::FILE_INFO fi;
		string p = localPath + "/" + i;
		fs::getFileInfo(p, fi);
		fil[fi.modifyTime] = i;
	}


	if (fil.size() > 0) //默认按照时间的升序排列
	{
		redirectPath += fil.rbegin()->second;
		res.set_header("location", redirectPath);
		res.set_header("Cache-Control", "max-age=1");
	}
	else
	{
		redirectPath += "tds.apk";
		res.set_header("location", redirectPath);
		res.set_header("Cache-Control", "max-age=1");
	}
}

void handleGet_gzh(const httplib::Request& req, httplib::Response& res)
{
	string timestamp= req.get_param_value("timestamp");
	string	nonce = req.get_param_value("nonce");
	string	echostr = req.get_param_value("echostr");
	string signature = req.get_param_value("signature");

	LOG("[微信公众号] Get请求\n");
	LOG("timestamp " + timestamp + "\n");
	LOG("nonce " + nonce + "\n");
	LOG("echostr " + echostr + "\n");
	LOG("signature " + signature + "\n");

	vector<string> vec;
	vec.push_back(timestamp);
	vec.push_back(nonce);
	vec.push_back(echostr);

	sort(vec.begin(), vec.end());

	string s = vec[0] + vec[1] + vec[2];

	SHA1 checksum;
	checksum.update(s);
	string hash = checksum.final();
	LOG("signature calc  " + hash + "\n");

	res.set_content(echostr, "text/plain;charset=UTF-8");
}

void handlePost_gzh(const httplib::Request& req, httplib::Response& res)
{
	LOG("[微信公众号] Post请求\n" + req.body);
	
	json jResp = "ok";
	string resp = jResp.dump();

	if (resp != "")
	{
		//下面两句都是必须的，不然跨域请求的前端收不到
		res.set_content(resp, "application/json;charset=utf-8");
		res.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin"));
	}
}

void handleRpcOverHttp(const httplib::Request& req, httplib::Response& res)
{
	//解析url参数模式的rpc调用
	string path = req.path;
	string strRpc;
	httplib::Params params = req.params;
	if (params.size() > 0)
	{
		string method;
		auto iter = params.find("m");
		if (iter != params.end())
			method = iter->second;
		iter = params.find("method");
		if (iter != params.end())
			method = iter->second;
		params.erase("m");
		params.erase("method");
		json j;
		j["method"] = method;
		json jP;
		for (auto& [k, v] : params)
		{
			if (k == "tag")
				v = httplib::detail::decode_url(v, true);
			jP[k] = v;
		}

		j["params"] = jP;
		strRpc = j.dump();
	}
	else
		strRpc = req.body;
	if (strRpc == "")
		return;

	string resp;
	char* binResp = NULL;
	int iBinRespLen = 0;
	bool bNeedLog = true;

	std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	rpcSrv.handleRpcCall(strRpc, resp, binResp, iBinRespLen,bNeedLog, pSession);

	if (resp != "")
	{
		//下面两句都是必须的，不然跨域请求的前端收不到
		res.set_content(resp, "application/json;charset=utf-8");
		res.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin"));
	}
	else if (binResp)
	{
		res.set_content(resp, "application/octet-stream");
		delete binResp;
	}
}


void initHttpSrv(httplib::Server& svr)
{
// 跨域请求，使用VSCode调试时，网页从VSCode的http服务器走。该功能主要方便调试
// 网页上使用的fetch进行rpc调用时，从tds的http服务走，因此浏览器会先发送OPTION请求跨域
//响应跨域预检请求
//https://developer.mozilla.org/zh-CN/docs/Web/HTTP/CORS
	svr.Options("\\/.*",
		[&](const httplib::Request& req, httplib::Response& res) {
			res.status = 200;
			res.set_header("Server", "tds");
			res.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin"));
			res.set_header("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
			res.set_header("Access-Control-Allow-Headers", req.get_header_value("Access-Control-Request-Headers"));
			res.set_header("Access-Control-Max-Age", "86400");
		});

//rpc Post命令处理
	svr.Post("\\/rpc.*",handleRpcOverHttp);
	svr.Get("\\/rpc.*", handleRpcOverHttp);

//微信公众号消息处理
	svr.Get("\\/gzh.*", handleGet_gzh);
	svr.Post("\\/gzh.*", handlePost_gzh);

//最新版本的apk下载
	svr.Get("\\/apk", handleGet_apk);

//有权限控制的文件下载服务
	svr.set_pre_routing_handler(handleFilePermission);

//数据库文件上传Post命令处理
	svr.Post("\\/db.*",
  [&](const Request &req, Response &res, const ContentReader &content_reader) {
	string pathReq = charCodec::ansi2Utf8(req.path);
	pathReq = pathReq.substr(3,pathReq.length()-3);
	string dbPath = db.m_path  +  pathReq;
	dbPath = str::replace(dbPath,"\\","/");
	if(fs::fileExist(dbPath))
	{
		if(!fs::deleteFile(dbPath))return;
	}
	fs::createFolderOfPath(dbPath);

	wstring wpath = charCodec::utf8toUtf16(dbPath);
	FILE* fp = _wfopen(wpath.c_str(), L"ab");
	if (!fp)
	{
		return;
	}
	
    if (req.is_multipart_form_data()) {
      MultipartFormDataItems files;
      content_reader(
        [&](const MultipartFormData &file) {
          files.push_back(file);
          return true;
        },
        [&](const char *data, size_t data_length) {
          files.back().content.append(data, data_length);
          return true;
        });
    } else {
      std::string body;
      content_reader([&](const char *data, size_t data_length) 
	  {
			fwrite(data, 1, data_length, fp);
			return true;
      });
      res.set_content(body, "text/plain");
    }

	if(fp)
		fclose(fp);
  });
}


dataServer::dataServer()
{
	
}

dataServer::~dataServer()
{
}

void dataServer::statusChange_tcpSrv(tcpSession* pTcpSession, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		GetLocalTime(&p->stCreateTime);
		p->bConnected = true;
		p->pTcpSession = pTcpSession;
		p->sock = pTcpSession->sock;
		p->port = pTcpSession->remotePort;
		p->ip = pTcpSession->remoteIP;

		if (pTcpSession->pTcpServer)
		{
			tcpSrv* pts = (tcpSrv*)pTcpSession->pTcpServer;
			//tdsp协议端口发送请求设备信息命令
			if (pts->m_iServerPort == tds->conf->ioServerPort)
			{
				p->type = TDS_SESSION_TYPE::iodev;
				p->iALProto = APP_LAYER_PROTO::TDSRPC;
				string req = R"s({
						"jsonrpc": "2.0",
						"method": "getDevInfo",
						"params": {},
						"clientId": "tds",
						"ioAddr": "any",
						"id": 1
					}

)s";

				p->send((char*)req.c_str(), req.length());
			}
			else if (pts->m_iServerPort == 664)
			{
				p->type = TDS_SESSION_TYPE::iodev;
				p->iALProto = APP_LAYER_PROTO::MODBUS_RTU;
			}
			else if (pts->m_iServerPort == 663)
			{
				p->type = TDS_SESSION_TYPE::iodev;
				p->iALProto = APP_LAYER_PROTO::IQ60;
			}
		}

		ioDev* pIoDev = ioSrv.getIODev(p->ip);
		if (pIoDev)
		{
			p->m_IoDev = pIoDev;
			pIoDev->setOnline();
			GetLocalTime(&pIoDev->m_stLastActiveTime);
			logger.logInternal("[ioDev]设备上线,ioAddr=" + pIoDev->getIOAddrStr());
			pIoDev->bindIOSession(p);
		}
			
		pTcpSession->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();
	}
	else
	{
		if (pTcpSession->pALSession)
		{
			std::shared_ptr<TDS_SESSION> p = NULL;
			//从列表中删除
			m_mutexTdsSessionList.lock();
			for (int i = 0; i < m_vecTdsSession.size(); i++)
			{
				if (m_vecTdsSession.at(i)->pTcpSession == pTcpSession)
				{
					p = m_vecTdsSession[i];
					m_vecTdsSession.erase(m_vecTdsSession.begin() + i);
					break;
				}
			}
			m_mutexTdsSessionList.unlock();

			//更新该session状态。等待其他零散指针引用销毁后自动删除
			p->onTcpDisconnect();
		}
	}
}

void tdsEdgeRegisterThread(std::shared_ptr<TDS_SESSION> p)
{
	Sleep(1000);
	//向服务器发送注册包
	json j;
	j["method"] = "devRegister";
	j["ioAddr"] = tds->conf->deviceID;

	string s = j.dump() + "\n\n";
	p->send((char*)s.c_str(), s.length());
}

void dataServer::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		p->m_bActiveSession = true;
		p->bConnected = true;
		p->pTcpSessionClt = connInfo->tcpClt;
		p->sock = connInfo->sock;
		p->port = connInfo->srvPort;
		p->ip = connInfo->srvIP;
		connInfo->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();

		//作为tdsEdge连接上了服务器
		if (connInfo->tcpClt == m_tcpCltEdge)
		{
			LOG("[边缘网关]连接tds服务器成功");
			thread t(tdsEdgeRegisterThread,p);
			t.detach();
		}

		//tds服务连接上了TcpServer模式的设备
		string ioAddr = str::format("%s:%d", connInfo->srvIP.c_str(), connInfo->srvPort);
		ioDev* pIoDev = ioSrv.getIODev(ioAddr);
		if (pIoDev)
		{
			p->m_IoDev = pIoDev;
			p->type = TDS_SESSION_TYPE::iodev;
			if (pIoDev->m_devType == IO_DEV_TYPE::DEV::iq60_gateway)
			{
				p->iALProto = APP_LAYER_PROTO::IQ60;
			}
			else if (pIoDev->m_devType == IO_DEV_TYPE::DEV::tdsp_device)
			{
				p->iALProto = APP_LAYER_PROTO::TDSRPC;
			}
			else if (pIoDev->m_devType == IO_DEV_TYPE::GW::rs485_gateway)
			{
				p->iALProto = APP_LAYER_PROTO::MODBUS_RTU;
			}
			pIoDev->setOnline();
			GetLocalTime(&pIoDev->m_stLastActiveTime);
			logger.logInternal("[ioDev]设备上线,ioAddr=" + pIoDev->getIOAddrStr());
			pIoDev->bindIOSession(p);
		}
	}
	else
	{
		if (connInfo->pALSession)
		{
			m_mutexTdsSessionList.lock();
			for (int i = 0; i < m_vecTdsSession.size(); i++)
			{
				if (m_vecTdsSession.at(i)->pTcpSessionClt == connInfo->tcpClt)
				{
					std::shared_ptr<TDS_SESSION> p = m_vecTdsSession[i]; \
						p->onTcpDisconnect();
					m_vecTdsSession.erase(m_vecTdsSession.begin() + i);
				}
			}
			m_mutexTdsSessionList.unlock();
		}
	}
}

int dataServer::SendAppLayerData(char* pData, int iLen, void* pAppLayerCltInfo)
{
	bool bRet = false;
	TDS_SESSION* pALC = (TDS_SESSION*)pAppLayerCltInfo;
	tcpSession* pCommLayerCltInfo = (pALC)->pTcpSession;

	if (pALC->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
	{
		WS_FrameType ft = WS_TEXT_FRAME;
		if (pALC->type == TDS_SESSION_TYPE::bridgeToTcpClient 
			|| pALC->type == TDS_SESSION_TYPE::bridgeToLocalCom)
		{
			ft = WS_BINARY_FRAME;
		}
		else if (pALC->type == TDS_SESSION_TYPE::video)
		{
			if (pALC->pTcpSession->iSendSucCount == 0)//视频首帧发文本
			{
				ft = WS_TEXT_FRAME;
			}
			else
				ft = WS_BINARY_FRAME;
		}
		else if (pALC->type == TDS_SESSION_TYPE::tdsClient)
		{
			if (pALC->sendContent == "text")
			{
				ft = WS_TEXT_FRAME;
			}
			else
				ft = WS_BINARY_FRAME;
		}

		return m_wspSrv.sendData((char*)pData, iLen, pCommLayerCltInfo, ft);
	}
	else
	{
		return m_tcpSrv->SendData((char*)pData, iLen, pCommLayerCltInfo);
	}
	
	return 0;
}

int dataServer::Send(SOCKET sock, char* pBuffer, int iLength)
{
	return send(sock, pBuffer, iLength, 0);
}

void activeSessionThread()
{
	string asConf;
	fs::readFile("conf/activeSession.json",asConf);
	if (asConf == "")
		return;
	try {
		json jAs = json::parse(asConf);
		if (jAs.size() == 0)
			return;
		for (int i = 0; i < jAs.size(); i++)
		{
			json oneSession = jAs[i];
			ACTIVE_TDS_SESSION ats;
			ats.ip = oneSession["ip"].get<string>();
			ats.port = oneSession["port"].get<int>();
			tds->conf->vecActiveSession.push_back(ats);
		}
	}
	catch (std::exception& e)
	{
		string log = e.what();
		log = "[error]conf/activeSession.json解析失败," + log;
		LOG(log);
		return;
	}


	for (int i = 0; i < tds->conf->vecActiveSession.size(); i++)
	{
		ACTIVE_TDS_SESSION ats = tds->conf->vecActiveSession.at(i);
		tcpClt* p = new tcpClt();

		string log = str::format("启动主动式tdsSession,tcpServer地址,%s:%d", ats.ip.c_str(), ats.port);
		LOG(log);
		p->AsynConnect(&ds, ats.ip, ats.port);
		ds.m_tcpCltList.push_back(p);
	}

	while (1)
	{
		Sleep(5000);
		for (auto& i : ds.m_tcpCltList)
		{
			if (!i->m_bConn)
			{
				i->AsynConnect(i->m_pCallBackUser, i->m_remoteIP, i->m_remotePort);
			}
		}
	}
}


void httpSrvThread(int port)
{
	//http相关接口需要使用gb2312.因为里面调用了多字节windows api，为支持中文，此处将utf8转为gb2312
	httplib::Server httpSrv;
	initHttpSrv(httpSrv);

	//web网页端口
	string webPath = fs::appPath() + "/web";
	if (fs::fileExist(webPath))
	{
		string asc_customUI = charCodec::utf8toAnsi(webPath);
		httpSrv.set_mount_point("/", +asc_customUI.c_str());
		LOG("[keyinfo][HTTP服务器] 根目录: " + webPath);
	}

	//基于tds的二次开发，ui根目录位于此
	//并且将tds的app目录放置在该目录下
	string customUI = fs::appPath() + "/ui";
	if (fs::fileExist(customUI))
	{
		string asc_customUI = charCodec::utf8toAnsi(customUI);
		httpSrv.set_mount_point("/", +asc_customUI.c_str());
		LOG("[keyinfo][HTTP服务器] 根目录: " + customUI);
	}

	//ui/app
	string customUIApp = fs::appPath() + "/ui/app";
	if (fs::fileExist(customUIApp))
	{
		string s = charCodec::utf8toAnsi(customUIApp);
		httpSrv.set_mount_point("/", + s.c_str());
		LOG("[keyinfo][HTTP服务器] 根目录: " + customUIApp);
	}

	//tds自己使用时，直接将app作为根目录
	string uiApps = fs::appPath() + "/app";
	if (fs::fileExist(uiApps))
	{
		string asc_prjUI = charCodec::utf8toAnsi(uiApps);
		httpSrv.set_mount_point("/", asc_prjUI.c_str());
		LOG("[keyinfo][HTTP服务器] 根目录: " + uiApps);
	}

	//配置路径作为根目录
	string asc_confPath = charCodec::utf8toAnsi(tds->conf->projectConfPath);
	auto ret = httpSrv.set_mount_point("/config/", asc_confPath.c_str());
	if (!ret) {
		LOG("[error][HTTP服务器] " + tds->conf->projectConfPath + " 不存在,请检查配置");
	}
	else
	{
		LOG("[keyinfo][HTTP服务器] /config/" + tds->conf->projectConfPath);
	}


	//serve db files through http
	string asc_dbPath = charCodec::utf8toAnsi(db.m_path);
	 ret = httpSrv.set_mount_point("/db/", asc_dbPath.c_str());
	if (!ret) {
		LOG("[error][数据库]路径 " + db.m_path + " 不存在,请检查配置");
	}
	else
	{
		LOG("[keyinfo][HTTP服务器] /db/ " + db.m_path);
	}

	
	//文件下载目录
	string asc_filePath = charCodec::utf8toAnsi(fs::appPath() + "/files");
	ret = httpSrv.set_mount_point("/files/", asc_filePath.c_str());
	if (!ret) {
	}
	else
	{
		LOG("[keyinfo][HTTP服务器] /files/ " + fs::appPath() + "/files");
	}


	httpSrv.set_file_extension_and_mimetype_mapping("json", "text/json");
	httpSrv.set_file_extension_and_mimetype_mapping("html", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("htm", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("htm", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("apk", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("rar", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("doc", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("docx", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("md", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("zip", "application/x-zip-compressed");
	httpSrv.set_file_extension_and_mimetype_mapping("txt", "text/plain");

	LOG("[keyinfo][HTTP服务器] 端口: " + str::fromInt(port) + " 本机浏览器 http://localhost:" + str::fromInt(port) + " 访问软件用户界面");
	httpSrv.listen("0.0.0.0", port);
}


bool dataServer::runAsCloud()
{
	m_tcpSrv = new tcpSrv();
	m_wspSrv.m_pTcpServer = m_tcpSrv;
	
	//tds websocket服务 666
	string strName;
	if(!tds->conf->debugMode)
		m_tcpSrv->keepAliveTimeout = tds->conf->tcpKeepAliveDS;
	int tryPort = tds->conf->port;
	while (!m_tcpSrv->run(this, tryPort))
	{
		if (m_tcpSrv->m_lastError == WSAEADDRINUSE)//10048)
		{
			LOG("ERROR:10048,Only one usage of each socket address (protocol/network address/port) is normally permitted.");
		}
		else if(m_tcpSrv->m_lastError == WSAEACCES)//10013)
		{
			LOG("ERROR:10013,An attempt was made to access a socket in a way forbidden by its access permissions.");
		}
		string strData;
		int triedPort = tryPort;
		if (tryPort == 80)tryPort = 666;
		else tryPort++;
		strData = str::format("bind to port:%d fail,try %d", triedPort, tryPort);
		LOG(strData);

		if(tryPort > 666)
		{
			LOG("[error]no valid port can be used!!");
			exit(0);
		}
	}
	LOG("[keyinfo][TDS服务   ] 端口:" + str::fromInt(tryPort) + " 使用tdsRPC over websocket协议访问");
	strName=str::format("tds(%d)", tryPort);
	m_tcpSrv->SettIOCPName(strName);

	//http服务 667
	thread t2(httpSrvThread,tds->conf->httpPort);
	t2.detach();

	//http服务 80端口
	string webPath = fs::appPath() + "/web";
	if (fs::fileExist(webPath))
	{
		thread t(httpSrvThread,80);
		t.detach();
	}

	userMng.loadConf();

	thread t(activeSessionThread);//主要用于io设备通过tcp中转而非直接连接的情况
	t.detach();

	return  1;
}

bool dataServer::runAsEdge()
{
	//tdsEdge连接
	if (tds->conf->edge)
	{
		m_tcpCltEdge = new tcpClt();
		m_tcpCltEdge->run(this, tds->conf->cloudIP, tds->conf->cloudPort);
		LOG("[keyinfo][边缘网关模式] 云服务器地址:%s:%d", tds->conf->cloudIP.c_str(), tds->conf->cloudPort);
	}
	return false;
}

void dataServer::stop()
{
	LOG("[keyinfo]正在停止数据服务DataServer...");
	if(m_tcpSrv)
		m_tcpSrv->stop();
	LOG("[keyinfo]数据服务已停止");
}

bool dataServer::OnRecvRawTdsRpc(char* pData, int iLen, std::shared_ptr<TDS_SESSION> pALC)
{

	return true;
}

bool dataServer::isHttpPkt(string str)
{
	if (str.find("HTTP") != string::npos)
	{
		return true;
	}
	return false;
}


class httpReqHandleThread_threadPool;
void httpReqHandleThread(std::shared_ptr<TDS_SESSION> tdsSession);
void httpReqHandleThread_poolThread(httpReqHandleThread_threadPool* p);
class httpReqHandleThread_threadPool {
public:
	httpReqHandleThread_threadPool()
	{
		for (int i = 0; i < 15; i++)
		{
			thread t(httpReqHandleThread_poolThread, this);
			t.detach();
		}
	}
	mutex m_cs;
	queue<std::shared_ptr<TDS_SESSION>> toProcess;
	semaphore m_newTask;
	void addTask(std::shared_ptr<TDS_SESSION> tdsSession) {
		m_cs.lock();
		toProcess.push(tdsSession);
		m_cs.unlock();
		m_newTask.notify();
	}
};

//httpReqHandleThread_threadPool  threadPool1;

void httpReqHandleThread_poolThread(httpReqHandleThread_threadPool* p)
{
	while (1)
	{
		p->m_newTask.wait();
		p->m_cs.lock();
		std::shared_ptr<TDS_SESSION> tdsSession = p->toProcess.front();
		p->toProcess.pop();
		p->m_cs.unlock();

		httpReqHandleThread(tdsSession);
	}
}

void httpReqHandleThread(std::shared_ptr<TDS_SESSION> tdsSession)
{
	//tdsSession->httpReqHandleThreadID = GetCurrentThreadId();
	//httplib::detail::dsClientStream* bs = (httplib::detail::dsClientStream*)tdsSession->dsCltStream;
	//SOCKET sock = bs->sock_;
	//bool close = false;
	//while (1)
	//{
	//	// rpc over http 不用通过GET发送，httplib处理GET命令不会读取BODY中的数据，会导致流的处理错误.
	//	httpSrv.process_request(*bs, false, close, nullptr);   
	//	if (!close) //HTTP keep-alive 模式，该链接可能连续发送多个http请求
	//	{
	//		if (bs->haveData())//粘连包的情况
	//		{
	//			continue;
	//		}
	//		else if(bs->m_sem.wait_for(5000)) //收到了后续请求
	//		{
	//			continue;
	//		}
	//		else
	//			break;
	//	}
	//}
	//shutdown(sock, SD_BOTH);
	//closesocket(sock); //对于大文件下载，此处等待发送完成再close，查看bool tcpSrv::DoAccept(SOCKET sockAccept, SOCKADDR_IN* ClientAddr)
	//
	////大量http请求时，会出现此处删除后，tcpRecvCallback又收到数据的情况。
	//tdsSession->dsCltStream = nullptr;
	//delete bs;
}

bool bTestStream = false;
int ThreadfMp4OverWS(std::shared_ptr<TDS_SESSION> pTestSess) {
	bTestStream = true;
	string str = fs::appPath() + "\\test.mp4";
	//string str = "E:\\VideoTest\\1.avi";
	char* pData = NULL;
	int iDataLen = 0;
	FILE* f = fopen(str.c_str(), "rb");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		iDataLen = ftell(f);
		pData = new char[iDataLen];
		fseek(f, 0, SEEK_SET);
		fread(pData,1,iDataLen,f);
		fclose(f);
	}
	if (iDataLen == 0)
	{
		bTestStream = false;
		return 0;
	}

	Sleep(500);
	while (pTestSess->type == TDS_SESSION_TYPE::video)
	{
		pTestSess->type = TDS_SESSION_TYPE::video;
		for (int i = 0; i < iDataLen;)
		{
			int iSend = 20000;
			if (i + iSend > iDataLen)
				iSend = iDataLen - i;
			if (!pTestSess->send(pData + i, iSend))
			{
				pTestSess->type = TDS_SESSION_TYPE::none;
				break;
			}
			i += iSend;
			Sleep(40);
		}
	}
	pTestSess->type = TDS_SESSION_TYPE::none;
	delete pData;
	bTestStream = false;
	return 0;
}


//此处加锁，连接断开现成可能会并发操作此列表
shared_ptr<TDS_SESSION> dataServer::getTDSSession(tcpSession* pTcpSess)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for(int i=0;i<m_vecTdsSession.size();i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		if(p->pTcpSession == pTcpSess)
		{
			return p;
		}
	}
	return nullptr;
}


shared_ptr<TDS_SESSION> dataServer::getTDSSession(string remoteIP,int remotePort)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for (int i = 0; i < m_vecTdsSession.size(); i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		std::unique_lock<recursive_mutex> lock(p->m_mutexTcpLink);
		if (p->isConnected())
		{
			if (p->m_bActiveSession)
			{
				//客户端模式remoteAddr 只有1个，但本地有可以有多个连接，因此使用本地端口+ip作为id
				if (p->pTcpSessionClt->m_strLocalIP == remoteIP && p->pTcpSessionClt->m_iLocalPort == remotePort)
				{
					return p;
				}
			}
			else
			{
				if (p->pTcpSession->remoteIP == remoteIP && p->pTcpSession->remotePort == remotePort)
				{
					return p;
				}
			}
		}
	}
	return nullptr;
}

shared_ptr<TDS_SESSION> dataServer::getTDSSession(string remoteAddr)
{
	int pos = remoteAddr.find(":");
	if (pos < 0)
		return nullptr;
	string ip = remoteAddr.substr(0, pos);
	string sPort = remoteAddr.substr(pos + 1, remoteAddr.length() - pos - 1);
	int iPort = atoi(sPort.c_str());
	return getTDSSession(ip, iPort);
}



shared_ptr<TDS_SESSION> dataServer::getTDSSession(tcpSessionClt* pTcpSess)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for (int i = 0; i < m_vecTdsSession.size(); i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		if (p->pTcpSessionClt == pTcpSess->tcpClt)
		{
			return p;
		}
	}
	return nullptr;
}

string dataServer::checkTransportLayerProto(string& strData, tcpSession* pTcpSess)
{
	return "";
}

vector<std::shared_ptr<TDS_SESSION>> logTdsSessions;
void logToWebsock(string text)
{

	for (int i = 0; i < logTdsSessions.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> ps = logTdsSessions[i];
		if (!ps->isConnected())
		{
			logTdsSessions.erase(logTdsSessions.begin() + i);
			i--;
		}
		else
			ps->send((char*)text.c_str(), text.length());
	}
}

/*
生产者-临时消费者模式  
tdsSessionProcessThread  为消费者，临时线程
OnRecvData_TCPServer 为生产者，常驻线程
tdsSession->dataBuff 为任务队列
当任务队列中有数据时，该模式控制 必有1个消费者 且 只有1个消费者

此处使用队列的原因。
不能直接将OnRecvData_TCPServer收到的数据多线程调用tdsSessionProcessThread去处理
因为可能网络中一个大数据包可能会被分包为多次回调，触发多个tdsSessionProcessThread之后，
多线程可能不按照数据流本身的先后顺序执行处理，导致数据包分片数据错误从而导致处理出错
*/
class tdsSessionProcessThread_threadPool;
int tdsSessionProcessThread_count = 0;
int tdsSessionProcessThread_poolSize = 5;
void tdsSessionProcessThread(std::shared_ptr<TDS_SESSION> tdsSession);
void tdsSessionProcessThread_poolThread(tdsSessionProcessThread_threadPool* p);
class tdsSessionProcessThread_threadPool {
public:
	tdsSessionProcessThread_threadPool()
	{
		for (int i = 0; i < 5; i++)
		{
			thread t(tdsSessionProcessThread_poolThread,this);
			t.detach();
		}
	}
	mutex m_cs;
	queue<std::shared_ptr<TDS_SESSION>> toProcess;
	semaphore m_newTask;
	void addTask(std::shared_ptr<TDS_SESSION> tdsSession) {
		m_cs.lock();
		toProcess.push(tdsSession);
		m_cs.unlock();
		m_newTask.notify();
	}
};

//tdsSessionProcessThread_threadPool  threadPool;



void tdsSessionProcessThread(std::shared_ptr<TDS_SESSION> tdsSession)
{
	//控制只有一个 - m_bSessionProcessing为false才能进入，因此不会出现两个工作者，
	//控制必有一个 - 此处如果return后，创建消费者线程的代码前面的代码已经插入了新任务，并且解锁后已存在工作者一定会进行一次待办任务确认，不会有不被执行的任务
	tdsSession->m_mutexTcpBuff.lock();
	if (tdsSession->m_bSessionProcessing)
	{
		tdsSession->m_mutexTcpBuff.unlock();//必须保证此处unlock后，已有的消费者一定会去检查任务队列
		return;
	}
	tdsSession->m_bSessionProcessing = true;
	tdsSession->m_mutexTcpBuff.unlock();

	while(1)
	{
		//取出任务
		//是否继续工作判断。 当其他线程获得锁，并且m_bSessionProcessing==true时，当前消费者线程一定还在while循环当中
		tdsSession->m_mutexTcpBuff.lock();
		if (tdsSession->dataBuff.size() == 0)
		{
			tdsSession->m_bSessionProcessing = false;
			tdsSession->m_mutexTcpBuff.unlock();
			break;
		}
		TCP_DATA_BUFF tdb = tdsSession->dataBuff.front();
		tdsSession->dataBuff.pop();
		tdsSession->m_mutexTcpBuff.unlock();

		//执行任务
		ds.OnRecvData_TCP(tdb.pData, tdb.iLen, tdsSession);
		delete tdb.pData;
	}
}

void tdsSessionProcessThread_poolThread(tdsSessionProcessThread_threadPool* p)
{
	while (1)
	{
		p->m_newTask.wait();
		p->m_cs.lock();
		std::shared_ptr<TDS_SESSION> tdsSession = p->toProcess.front();
		p->toProcess.pop();
		p->m_cs.unlock();

		tdsSessionProcessThread(tdsSession);
	}
}

void dataServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	std::shared_ptr<TDS_SESSION> tdsSession = getTDSSession(pTcpSess);

	std::unique_lock<mutex> g(tdsSession->m_mutexTcpBuff);
	TCP_DATA_BUFF tdb;
	tdb.pData = new char[iLen];
	tdb.iLen = iLen;
	memcpy(tdb.pData, pData, iLen);
	tdsSession->dataBuff.push(tdb);
	//调用临时消费者
	thread t(tdsSessionProcessThread, tdsSession);
	t.detach();
	//threadPool.addTask(tdsSession);
}

void dataServer::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
{
	std::shared_ptr<TDS_SESSION> tdsSession = getTDSSession(connInfo);
	OnRecvData_TCP(pData, iLen, tdsSession);
}

void dataServer::getUrlParams(string& url,map<string, string>& mapParams)
{
	int paramStart = url.find('?', 0);
	if (paramStart != string::npos)//解析携带参数
	{
		int paramEnd = url.find(' ', paramStart);
		string paramStr = url.substr(paramStart + 1, paramEnd - paramStart - 1);
		vector<string> params;
		str::split(params, paramStr, "&");
		
		for (int i = 0; i < params.size(); i++)
		{
			string oneP = params[i];
			vector<string> pkv;
			str::split(pkv, oneP, "=");
			if (pkv.size() == 2)
			{
				mapParams[pkv[0]] = pkv[1];
			}
		}
	}
}

void dataServer::onWebsocketSessionOpen(string& strData, std::shared_ptr<TDS_SESSION> tdsSession)
{
	string szLog = str::format("[trace][ds]websocket session opened,client addr is %s:%d", tdsSession->ip.c_str(), tdsSession->port);
	LOG(szLog);

	//回复websocket握手
	CWSPPkt req;
	std::string handshakeString = req.GetHandshakeString(strData);
	send(tdsSession->sock, handshakeString.c_str(), handshakeString.size(), 0);

	//terminal可以用来打开与某一接口的透传桥接，并发送指令
	if (strData.find("/terminal") != string::npos)
	{
		int pos = strData.find("terminal");
		int pos1 = strData.find(" ", pos);
		string ioAddr = strData.substr(pos + 9, pos1 - (pos + 9));
		ioDev* p = ioSrv.getIODev(ioAddr);
		if (p && p->pIOSession != NULL)
		{
			tdsSession->type = TDS_SESSION_TYPE::bridgeToiodev;
			tdsSession->bridgedIoSession = p->pIOSession;
			p->pIOSession->bridgedIoSessionClient = tdsSession;
			LOG("open websocket terminal at ioAddr %s success", ioAddr.c_str());
			tdsSession->setActivityCheck(false);
			tdsSession->bridgedIoSession->setActivityCheck(false);
		}
		else if (p && p->m_devType == IO_DEV_TYPE::GW::local_serial)
		{
			tdsSession->setActivityCheck(false);
			tdsSession->type = TDS_SESSION_TYPE::bridgeToLocalCom;
			tdsSession->bridgedLocalCom = ioAddr;
			p->pTdsSession = tdsSession;
		}
		else
		{
			closesocket(tdsSession->sock);
			return;
		}
	}
	else if (strData.find("/COM") != string::npos)
	{
		int pos = strData.find("COM");
		int pos1 = strData.find(" ", pos);
		string portNum = strData.substr(pos, pos1 - pos);
		ioDev* p = ioSrv.getIODev(portNum);
		tdsSession->type = TDS_SESSION_TYPE::bridgeToLocalCom;
		tdsSession->setActivityCheck(false);
		if (p)
		{
			tdsSession->bridgedLocalCom = portNum;
			p->pTdsSession = tdsSession;
		}
		else
		{
			//string html = portNum + " is not in the opened port list,please open it first";
			//std::string header = "HTTP/1.1 200 OK\r\n";
			//header += "Content-Type: text/html; charset=utf-8\r\n";
			//header += "Accept-Ranges: none\r\n"; // no support for partial requests
			//header += "Cache-Control: no-store, must-revalidate\r\n";
			//header += "Content-Length: " + std::to_string(html.length()) + "\r\n";
			//header += "\r\n";

			//string resp = header + html;
			//send(tdsSession->sock, (char*)resp.data(), resp.length(), 0);
			closesocket(tdsSession->sock);
			return;
		}
	}
	else if (strData.find("tcp") != string::npos)
	{
		int pos = strData.find("tcp");
		int pos1 = strData.find(" ", pos);
		string host = strData.substr(pos + 4, pos1 - (pos + 4));
		tdsSession->pBridgedTcpClient = new tcpClt();
		tdsSession->type = TDS_SESSION_TYPE::bridgeToTcpClient;
		tdsSession->setActivityCheck(false);
		if (tdsSession->pBridgedTcpClient->connect(&tdsSession->bridgedTcpCltHandler, host))
		{
			LOG("bridge websocket to tcp %s success", host.c_str());
		}
		else
		{
			LOG("bridge websocket to tcp %s fail", host.c_str());
			delete tdsSession->pBridgedTcpClient;
			tdsSession->pBridgedTcpClient = NULL;
			closesocket(tdsSession->sock);
			return;
		}
	}
	else if (strData.find("/log") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::log;
		logTdsSessions.push_back(tdsSession);
		logger.logOutput = logToWebsock;
		tdsSession->setActivityCheck(false);
	}
	else if (strData.find("/sessionpkt") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::sessionPkt;
		sessionPktSessions.push_back(tdsSession);
		tdsSession->setActivityCheck(false);
	}
	else if (strData.find("/commpkt") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::commpkt;
		commpktSessions.push_back(tdsSession); 
		tdsSession->setActivityCheck(false);
	}
	else if (strData.find("teststream") != string::npos && !bTestStream)
	{
		tdsSession->type = TDS_SESSION_TYPE::video;
		std::thread t(ThreadfMp4OverWS, tdsSession);
		t.detach();
	}
	else if (strData.find("desktop") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::video;
#ifdef ENABLE_FFMPEG
		rds.startStream(tdsSession);
#endif
	}
	else if (strData.find("stream") != string::npos)
	{
		int pos = strData.find("stream");
		map<string, string> mapParams;
		getUrlParams(strData, mapParams);
		string streamId; //支持码流的tag
		string fmt = ""; //为空，则图像不进行任何转换直接发送
		int frameRate = 0;
		if (mapParams.size() > 0)
		{
			if (mapParams.find("streamId") != mapParams.end())
			{
				streamId = mapParams["streamId"];
			}
			if (mapParams.find("fmt") != mapParams.end())//fmt is not specified
			{
				fmt = mapParams["fmt"];
			}
			if (mapParams.find("frameRate") != mapParams.end())//fmt is not specified
			{
				frameRate = str::toInt(mapParams["frameRate"]);
			}
			streamId = httplib::detail::decode_url(streamId, false);


			if (streamId != "")
			{
				streamSrvNode* pVsn = streamSrv.getSrvNode(streamId);
				if (pVsn)
				{
					tdsSession->videoServiceNode = pVsn;
					tdsSession->type = TDS_SESSION_TYPE::video;
					STREAM_INFO si;
					si.pixelFmt = fmt;
					si.frameRate = frameRate;
					pVsn->addPuller(tdsSession, &si);
					string szLog = "[Session会话][开始] 类型:" + tdsSession->type + " 码流ID:" + streamId + " 格式:" + fmt + ",客户端地址:" + tdsSession->ip + ":" + str::fromInt(tdsSession->port);
					LOG(szLog);
				}
			}
		}
	}
	else //连接根地址 默认为rpc连接
	{
		if (strData.find("tdsClient") != string::npos)
		{

		}

		map<string, string> mapParams;
		getUrlParams(strData, mapParams);
		if (mapParams.find("needLog") != mapParams.end())
		{
			string needLog = mapParams["needLog"];
			if (needLog == "0")
			{
				tdsSession->m_bNeedLog = false;
			}
		}
		
		if (tds->conf->debugMode)
		{
			string s = R"(
						{
							"jsonrpc": "2.0", 
							"method": "notify.close_heartbeat", 
							"params": {
							}, 
							"id": null
						}
					)";
			tdsSession->send((char*)s.data(), s.length());
		}
		tdsSession->type = TDS_SESSION_TYPE::tdsClient;
		tdsSession->iALProto = APP_LAYER_PROTO::TDSRPC;

		string szLog = "[Session会话][开始] 类型:" + tdsSession->type + ",客户端地址:" + tdsSession->ip + ":" + str::fromInt(tdsSession->port);
		LOG(szLog);
	}
}


void dataServer::OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{

	GetLocalTime(&tdsSession->lastRecvTime);

	{
		//后续此处加入互斥量保护
		//if (tdsSession->m_IoDev)
		//{
		//	tdsSession->m_IoDev->OnRecvData(pData, iLen);
		//	return;
		//}	 
	}


	//if it's the first time recv data from a connection. check transport layer protocol first
	//if applayer protocol is TDS RPC,transport layer protocol can be HTTP or WebSocket or RawTcp(no transport layer)
	//if applayer protocol is HTTP,transport layer is specified as none
	//首次从该链接收到数据时的处理。
	if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN)
	{	
		string strData = str::fromBuff(pData,iLen);

		//parse transfer layer protocol
		if(isHttpPkt(strData))
		{
			tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP;
			if (CWSPPkt::isHandShake(strData))
			{
				tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET;
			}
			else if(strData.find("/tdsClient") != string::npos)
			{
				tdsSession->iALProto = APP_LAYER_PROTO::TDSRPC;
			}
		}
		else
		{
			tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_NONE;
		}

		//if websocket. deal the first handshake pkt 
		if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
		{
			onWebsocketSessionOpen(strData, tdsSession);
			return;
		}
	}


	//  rpc 或者 桥接数据
	if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
	{
		if (!tdsSession) {
			string str = "dataServer::OnRecvData_TCPServer: DSP_CLIENT_SESSION is null";
			string strText = str.c_str();
			LOG(strText);
			return;
		}
		m_wspSrv.OnRecvWSData(pData, iLen, &tdsSession->m_tlBuf, tdsSession);
		return;
	}
	//http处理   1.网页请求  2.tdsRpc over http   
	else if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP)
	{
		return;
		char* ptmp = new char[iLen + 1];
		memset(ptmp, 0, iLen + 1);
		memcpy(ptmp, pData, iLen);
		string strData = ptmp;
		delete ptmp;

		if (strData.find("/rpc") == string::npos) //仅处理rpc请求。 http网页请求走667端口
			return;
		//rpc 先组包后处理
		//if (tdsSession->iALProto == APP_LAYER_PROTO::TDSRPC)
		//{
		//	stream2pkt* pab = &tdsSession->m_alBuf;
		//	pab->PushStream(pData, iLen);
		//	while (pab->PopPkt(APP_LAYER_PROTO::HTTP))
		//	{
		//		tdsSession->iALProto = pab->m_protocolType;
		//		onRecvHttpPkt(pab->pkt, pab->iPktLen, tdsSession);
		//	}
		//}
		//其他url流式处理，避免长度很长的请求包造成不必要的组包消耗.httplib内部是先接收http header。再处理content的
		//因此无需先获得整个的http包。特别针对大文件上传时，必须采用流式处理。否则每次分片尝试识别是否完整包造成不必要的计算消耗
		//else
		//{
			httplib::detail::dsClientStream* bs = NULL;
			if(tdsSession->dsCltStream == NULL)
			{
				bs = new httplib::detail::dsClientStream;
				bs->sock_ = tdsSession->pTcpSession->sock;
				tdsSession->dsCltStream = bs;
				std::thread t(httpReqHandleThread,tdsSession);
				t.detach();
				//threadPool1.addTask(tdsSession);
			}
			else
			{
				bs = (httplib::detail::dsClientStream*)tdsSession->dsCltStream;
			}

			//httpReqHandleThread内部可能删除bs导致野指针，需要优化
			bs->appendBuffer(pData, iLen);
		//}
	}
	//tcp直连,没有传输层，表示全部都是应用层数据
	else if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_NONE)
	{	
		OnRecvAppLayerData(pData, iLen, tdsSession);
		return;
	}
}





//handle http not using files in disk
bool dataServer::httpHandleInternal(string strData,std::shared_ptr<TDS_SESSION> pAppLayerClt)
{
	if (strData.find("GET /desktop") != string::npos)
	{
		string html = getRDSPage();
		//fs::readFile("./ui/app/remotedesktop/index.html",html);
		//str::replace(html,"src=\"h5player.js\"","src=\"app/remotedesktop/h5player.js\"");
		std::string header = "HTTP/1.1 200 OK\r\n";
		header += "Content-Type: text/html; charset=utf-8\r\n";
		header += "Accept-Ranges: none\r\n"; // no support for partial requests
		header += "Cache-Control: no-store, must-revalidate\r\n";
		header += "Content-Length: "+std::to_string(html.length())+"\r\n";
		header += "\r\n";
		
		string resp = header + html;
		pAppLayerClt->send((char*)resp.data(),resp.length());
		return true;
	}

	return false;
}


void handleRPCOverHttp(const Request&, Response&)
{

}


bool dataServer::onRecvHttpPkt(char* pDataBuf, int iLen, std::shared_ptr<TDS_SESSION> pALC)
{
	char* ptmp = new char[iLen + 1];
	memset(ptmp, 0, iLen + 1);
	memcpy(ptmp, pDataBuf, iLen);
	string strData = ptmp;
	delete ptmp;

	if (strData.find("/rpc") != string::npos)
	{
		//响应跨域预检请求
		//https://developer.mozilla.org/zh-CN/docs/Web/HTTP/CORS
		if (strData.find("OPTIONS") == 0)
		{
			httplib::detail::dsClientStream dscs;
			dscs.appendBuffer((char*)strData.c_str(), strData.length());
			httplib::Request req;
			detail::read_headers(dscs, req.headers);

			string httpHead = "HTTP/1.1 200 OK\r\n";
			httpHead += "Server: tds\r\n";
			httpHead += "Access-Control-Allow-Origin: " + req.get_header_value("Origin") + "\r\n";
			httpHead += "Access-Control-Allow-Methods: POST,GET,OPTIONS\r\n";
			httpHead += "Access-Control-Allow-Headers:" + req.get_header_value("Access-Control-Request-Headers")+"\r\n";
			httpHead += "Access-Control-Max-Age: 86400\r\n";
			httpHead += "Keep-Alive : timeout=2,max=100\r\n";
			httpHead += "Connection: Keep-Alive\r\n";
			
			string resp = httpHead + "\r\n";
			pALC->send((char*)resp.data(), resp.length());
			return true;		
		}

		//解析url参数模式的rpc调用
		map<string, string> mapParams;
		getUrlParams(strData, mapParams);


		string szLog = str::format("[trace][ds]tdsrpc over http session opened,client addr is %s:%d",pALC->pTcpSession->remoteIP.c_str(),pALC->pTcpSession->remotePort);
		LOG(szLog);
		pALC->iALProto = APP_LAYER_PROTO::TDSRPC;
		pALC->type = TDS_SESSION_TYPE::tdsClient;

		httplib::detail::dsClientStream dscs;
		dscs.appendBuffer((char*)strData.c_str(), strData.length());
		httplib::Request req;
		detail::read_headers(dscs, req.headers);

		int ipos = strData.find("\r\n\r\n");
		if (ipos == string::npos)
		{
			ipos = strData.find("\n\n");
		}
		string strRpc = "";
		if (ipos != string::npos)
		{
			strRpc = strData.substr(ipos, strData.length() - ipos);
		}
		else
		{
			strRpc = "";
		}
		
		if (mapParams.size() > 0)
		{
			string method;
			if (mapParams.find("m") != mapParams.end())
				method = mapParams["m"];
			if (mapParams.find("method") != mapParams.end())
				method = mapParams["method"];
			mapParams.erase("m");
			mapParams.erase("method");
			json j;
			j["method"] = method;
			json jP;
			for (auto &[k,v] : mapParams)
			{
				if (k == "tag")
					v = httplib::detail::decode_url(v,true);
				jP[k] = v;
			}

			j["params"] = jP;
			strRpc = j.dump();
		}
		
		string resp;
		char* binResp = NULL;
		int iBinRespLen = 0;
		bool bNeedLog = true;
		rpcSrv.handleRpcCall(strRpc, resp, binResp, iBinRespLen, bNeedLog,pALC);

		if (resp != "")
		{
			string httpHead = "HTTP/1.1 200 OK\r\n";
			httpHead += "Connection: close\r\n";
			httpHead += "Content-Length: " + str::fromInt(resp.length()) + "\r\n";
			httpHead += "Content-Type: application/json;charset=utf-8\r\n";
			string origin = req.get_header_value("origin");
			if (origin != "")
			{
				httpHead += "Access-Control-Allow-Origin: " + req.get_header_value("Origin") + "\r\n";
			}
			string httpResp = httpHead + "\r\n" + resp;
			pALC->send((char*)httpResp.data(), httpResp.length(),bNeedLog);
		}
		if (binResp != NULL)
		{
			string httpHead = "HTTP/1.1 200 OK\r\n";
			httpHead += "Connection: close\r\n";
			httpHead += "Content-Length: " + str::fromInt(iBinRespLen) + "\r\n";
			httpHead += "Content-Type: application/octet-stream\r\n";
			string origin = req.get_header_value("origin");
			if (origin != "")
			{
				httpHead += "Access-Control-Allow-Origin: " + req.get_header_value("Origin") + "\r\n";
			}
			pALC->send((char*)httpHead.data(), httpHead.length());
			pALC->send((char*)"\r\n", 2);
			pALC->send((char*)binResp, iBinRespLen, bNeedLog);
			delete binResp;
		}
	}
	else
	{
		//internal handle
		if (httpHandleInternal(strData, pALC))
			return true;
	}
	

	return true;
}

void onRecvIQ60Pkt(char* pData, int iLen, std::shared_ptr<TDS_SESSION> pALC)
{
	string pkt = str::fromBuff(pData,iLen);

	try {
		json jpkt = json::parse(pkt);

		if (jpkt.is_array() && jpkt.size() >= 1)
		{
			//转发给对应设备
			string id = jpkt[0];
			ioDev* pIoDev = ioSrv.getIODev(id);
			if (pIoDev)
			{
				if (pIoDev->m_devType == IO_DEV_TYPE::DEV::iq60_gateway)
				{
					ioDev* p = pIoDev;
					if (p->m_bEnableIoLog)
						p->statisOnRecv((char*)pkt.c_str(), pkt.length(), p->getIOAddrStr());

					p->bindIOSession(pALC);
					p->setOnline();
					json j;
					p->toJson(j);
					if (!pALC->getIODev(p->getIOAddrStr()))
					{
						pALC->m_vecIoDev.push_back(p->getIOAddrStr());
					}
					p->onRecvPkt(jpkt);
				}
				else
				{
					LOG("[error]%s iq60 online,but this addr is configured as not an iq60 dev", id);
				}
			}
			//设备发现功能
			else
			{
				json jAddr;
				jAddr["id"] = id;
				ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::DEV::iq60_gateway);
			}
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		string log = "pkt from iq60,json parse error. " + errorType;
		LOG(log);
	}
}

bool dataServer::handleAppLayerData_IODev(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	bool bHandled = true;
	if (tdsSession->type == TDS_SESSION_TYPE::iodev)
	{
		//协议检测
		if (tdsSession->iALProto == APP_LAYER_PROTO::UNKNOWN)//应用层协议类型检测
		{
			//应用层协议智能检测。根据收到的首包数据进行检测
			//傲华尔远程控制协议
			if ((pData[0] == '[' && pData[iLen - 1] == ']') ||
				(pData[0] == '[' && pData[iLen - 1] == '\n' && pData[iLen - 2] == ']')
				)
			{
				tdsSession->iALProto = APP_LAYER_PROTO::IQ60;
				tdsSession->type = TDS_SESSION_TYPE::iodev + ".IQ60";
			}
		}

		//应用层协议处理
		if (tdsSession->bridgedIoSessionClient != NULL)
		{
			if (tdsSession->iALProto == APP_LAYER_PROTO::TDSRPC)
			{
				stream2pkt* pab = &tdsSession->m_alBuf;
				pab->PushStream(pData, iLen);
				while (pab->PopPkt(APP_LAYER_PROTO::textEnd2LF))
				{
					tdsSession->bridgedIoSessionClient->send(pab->pkt, pab->iPktLen);
					string s = str::fromBuff(pab->pkt, pab->iPktLen);
					LOG("[IO设备透传]dev->client " + s);
				}
			}
			//iq60的命令行数据包需要组包后再转发，否则可能导致中文utf8字符被分割后无法解析
			else if (tdsSession->iALProto == APP_LAYER_PROTO::IQ60)
			{
				stream2pkt* pab = &tdsSession->m_alBuf;
				pab->PushStream(pData, iLen);
				while (pab->PopPkt(APP_LAYER_PROTO::IQ60) || pab->PopPkt(APP_LAYER_PROTO::terminalPrompt))
				{
					tdsSession->bridgedIoSessionClient->send(pab->pkt, pab->iPktLen);
					string s = str::fromBuff(pab->pkt, pab->iPktLen);
					LOG("[IO设备透传]dev->client " + s);
				}
			}
		}
		else if (tdsSession->iALProto == APP_LAYER_PROTO::IQ60)
		{
			bool regPkt = false;
			if (!tdsSession->m_bAppDataRecved)//首包数据,按照tdsp注册包处理
			{
				string s = str::fromBuff(pData, iLen);
				if (s.find("IQ60_") == 0)
				{
					s = s.substr(0, 16);
					LOG("IQ60首发数据," + s);
					regPkt = true;
					string strIoAddr = s.substr(5,s.length()-5);
					ioSrv.handleDevOnline(strIoAddr, tdsSession);

					if (iLen > 16)
					{
						stream2pkt* pab = &tdsSession->m_alBuf;
						pab->PushStream(pData + 16, iLen-16);
						while (pab->PopPkt(APP_LAYER_PROTO::IQ60))
						{
							onRecvIQ60Pkt(pab->pkt, pab->iPktLen, tdsSession);
						}
					}
				}
			}
			
			if (!regPkt)
			{
				stream2pkt* pab = &tdsSession->m_alBuf;
				pab->PushStream(pData, iLen);
				while (pab->PopPkt(APP_LAYER_PROTO::IQ60))
				{
					onRecvIQ60Pkt(pab->pkt, pab->iPktLen, tdsSession);
				}
			}
		}
		else if(tdsSession->iALProto == APP_LAYER_PROTO::TDSRPC)
		{
			//检查是否是imei直接注册包,15位且都是数字，认为是imei
			if (!tdsSession->m_bAppDataRecved && iLen == 15 && str::isDigits(pData,iLen))//首包数据,按照tdsp注册包处理
			{
				string imei = str::fromBuff(pData, iLen);
				LOG("TDSP端口665收到首发IMEI注册包,IMEI=" + imei);
				ioDev* pIoDev = ioSrv.handleDevOnline(imei, tdsSession);
			}
			else
			{
				stream2pkt* pab = &tdsSession->m_alBuf;
				pab->PushStream(pData, iLen);
				while (pab->PopPkt(APP_LAYER_PROTO::TDSRPC))
				{
					if (pab->abandonData != "")
					{
						string remoteAddr = tdsSession->getRemoteAddr();
						LOG("[error]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
						tdsSession->abandonLen += pab->iAbandonBytes;
					}
					tdsSession->iALProto = pab->m_protocolType;
					onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession);
				}
			}
		}
		else if (tdsSession->iALProto == APP_LAYER_PROTO::MODBUS_RTU)
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			bool bRegPkt = false;
			if (!tdsSession->m_bAppDataRecved)//首包数据,按照tdsp注册包处理
			{
				if (pab->PopPkt(APP_LAYER_PROTO::TDSRPC))
				{
					bRegPkt = true;
					onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession,true);
					LOG("Modbus网关注册数据包:" + str::bytesToHexStr(pData, iLen));
				}
			}
			

			if (!bRegPkt)
			{
				while (pab->PopPkt(APP_LAYER_PROTO::MODBUS_RTU))
				{
					if (pab->abandonData != "")
					{
						string remoteAddr = tdsSession->getRemoteAddr();
						LOG("[error]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
						tdsSession->abandonLen += pab->iAbandonBytes;
					}
					tdsSession->iALProto = pab->m_protocolType;
					onRecvPkt_ioDev(pab->pkt, pab->iPktLen, tdsSession);
				}
			}
		}
	}
	else
	{
		bHandled = false;
	}
	return true;
}

//应用层数据桥接
bool dataServer::handleAppLayerData_Bridge(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	bool bHandled = true;
	if (tdsSession->type == TDS_SESSION_TYPE::bridgeToLocalCom)
	{
		ioDev* p = ioSrv.getIODev(tdsSession->bridgedLocalCom);
		if (p && p->m_devType == IO_DEV_TYPE::GW::local_serial)
		{
			ioGW_LocalSerial* pLs = (ioGW_LocalSerial*)p;
			if (!pLs->sendData(pData, iLen))
			{
				LOG("[warn][数据桥接]发送数据到串口失败," + tdsSession->bridgedLocalCom + "," + pLs->m_strErrorInfo);
			}
		}
		else
		{
			LOG("[warn][数据桥接]未找到串口设备" + tdsSession->bridgedLocalCom);
		}
	}
	else if (tdsSession->type == TDS_SESSION_TYPE::bridgeToTcpClient)
	{
		if (tdsSession->pBridgedTcpClient)
			tdsSession->pBridgedTcpClient->SendData(pData, iLen);
	}
	else if (tdsSession->type == TDS_SESSION_TYPE::bridgeToiodev)
	{
		if (tdsSession->bridgedIoSession)
			tdsSession->bridgedIoSession->send(pData, iLen);
		string s = str::fromBuff(pData,iLen);
		LOG("[IO设备透传]client->dev " + s);
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}


//onRecvData需要组包
bool dataServer::OnRecvAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession,bool isPkt)
{
	tdsSession->statisOnRecv(pData, iLen);

	DWORD dwDataLen = iLen;

	//根据 tdsSessionType 对应用层数据做不同的处理
	//有桥接先判断桥接
	handleAppLayerData_Bridge(pData, iLen, tdsSession);

	//处理来自于io设备的数据
	handleAppLayerData_IODev(pData, iLen, tdsSession);
	
	//tds rpc over tcp
	if (tdsSession->type == TDS_SESSION_TYPE::tdsClient && isPkt)
	{
		onRecvPkt_tdsClient(pData, iLen, tdsSession);
	}
	
	tdsSession->m_bAppDataRecved = true;
	return true;
}

void dataServer::onRecvPkt_ioDev(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool registerPkt)
{
	try 
	{
		if (tdsSession->iALProto == APP_LAYER_PROTO::TDSRPC)
		{
			string sResp = str::fromBuff(pData, iLen);

			//编解码转换
			if(rpcSrv.isGB2312Pkt(sResp))
			{
				sResp = charCodec::ansi2Utf8(sResp);
				tdsSession->m_charset = "gb2312";
			}

			//解析请求基本信息
			json jResp = json::parse(sResp);
			if (!jResp.contains("method"))
			{
				LOG("[error][TDSP]tdsp设备的协议数据包必须包含method字段\n" + sResp);
				return;
			}
			string method = jResp["method"].get<string>();
			json params;
			if (jResp.contains("params"))
				params = jResp["params"];
			json id = jResp["id"];
			json clientId = jResp["clientId"]; //tds edge模式使用
			tdsSession->lastMethodCalled = method;
			string charset = "utf8";
			if (jResp.contains("charset"))
			{
				charset = jResp["charset"].get<string>();
			}

			//注册包			
			if (method == "devRegister")
			{
				string strIoAddr = jResp["ioAddr"].get<string>();
				ioDev* pIoDev = ioSrv.handleDevOnline(strIoAddr, tdsSession);
				pIoDev->onRecvPkt(jResp);
				pIoDev->m_charset = charset;
			}
			//透传到tds客户端的指令。使用clientId进行透传机制，暂时取消
			//else if (clientId != nullptr && clientId.get<string>() != "tds")
			//{
			//	string addr = jResp["clientId"].get<string>();
			//	shared_ptr<TDS_SESSION> p = ds.getTDSSession(addr);
			//	if (p != nullptr)
			//	{
			//		string s = jResp.dump() + "\n\n";
			//		p->send((char*)s.c_str(), s.length());
			//		LOG("[设备透传]设备->客户端:\r\n" + s + "\r\n");
			//	}
			//	else
			//	{
			//		string s = jResp.dump(2) + "\n\n";
			//		LOG("[error][设备透传]设备->客户端 未找到会话:\r\n" + s + "\r\n");
			//	}

			//	//部分命令拦截并记录.用户点击查询数据时，也记录数据库。
			//	string strIoAddr = jResp["ioAddr"].get<string>();
			//	ioDev* pIoDev = ioSrv.getIODev(strIoAddr);
			//	GetLocalTime(&pIoDev->m_stLastActiveTime);
			//	if (pIoDev && pIoDev->m_devType == IO_DEV_TYPE::DEV::tdsp_device)
			//	{
			//		ioDev_tdsp* pDevTdsp = (ioDev_tdsp*)pIoDev;
			//		pDevTdsp->handleAsynResp(jResp);
			//	}
			//}
			//ioDev -> tds
			else
			{
				string strIoAddr = jResp["ioAddr"].get<string>();
				ioDev* pIoDev = ioSrv.getIODev(strIoAddr);
				if (!pIoDev)
				{
					json jAddr;
					jAddr["id"] = strIoAddr;
					pIoDev = ioSrv.onChildDevDiscovered(jAddr, IO_DEV_TYPE::DEV::tdsp_device);
				}
				pIoDev->m_charset = charset;
				pIoDev->bindIOSession(tdsSession);
				pIoDev->onRecvPkt(jResp);
				LOG("[trace]TDSP响应:\r\n" + sResp + "\r\n");
			}
		}
		else if (tdsSession->iALProto == APP_LAYER_PROTO::MODBUS_RTU)
		{
			//4g模式下的modbus RTU over tcp 第一包必须发送注册包
			if (registerPkt)
			{
				string sResp = str::fromBuff(pData, iLen);
				json jResp = json::parse(sResp);
				string method = jResp["method"].get<string>();
				string strIoAddr = jResp["ioAddr"].get<string>();
				if (method == "devRegister")
					ioSrv.handleDevOnline(strIoAddr, tdsSession);
			}
			else
			{
				if (tdsSession->m_IoDev)
				{
					tdsSession->m_IoDev->onRecvPkt(pData, iLen);
				}
			}
		}
	}
	catch (std::exception& e)
	{
		string errorType = e.what();
		//json库的 what 返回的字符串，本身可能是一个携带非utf8字符的字符串。这串错误描述可能包含了解析错误的那个字符,所以也非法。
		//全部转换为ascII，用转义字符表示。否则后面的jError.dump() 会奔溃
		errorType = str::encodeAscII(errorType);
		LOG("onRecvPkt_ioDev 处理异常" + errorType);
	}
}

void dataServer::onRecvPkt_tdsClient(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	string req = str::fromBuff(pData,iLen);
	string resp;
	char* binResp = NULL;
	int iBinRespLen = 0;
	bool bNeedLog = true;
	rpcSrv.handleRpcCall(req, resp, binResp, iBinRespLen,bNeedLog, tdsSession);

	if (resp != "")
	{
		tdsSession->sendContent = "text";
		tdsSession->send((char*)resp.data(), resp.length(),bNeedLog);
	}

	if (iBinRespLen > 0)
	{
		tdsSession->sendContent = "binary";
		tdsSession->send(binResp, iBinRespLen, bNeedLog);
	}

	//如果没有任何回复,可能是透传指令,不回复

	if (binResp)
		delete binResp;
}


vector<void*> dataServer::GetSessionList()
{
	vector<void*> lst;
	for (int i = 0; i < m_vecTdsSession.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		lst.push_back(p.get());
	}
	return lst;
}

string dataServer::getSessionStatus(json params)
{
	json typeFilter = nullptr;
	json nameFilter = nullptr;
	if(params!=nullptr) typeFilter = params["type"];
	if (params != nullptr) nameFilter = params["name"];
	lock_guard<mutex> g(m_mutexTdsSessionList);
	json jList = json::array();
	for (int i = 0; i < m_vecTdsSession.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);

		if (typeFilter != nullptr)
		{
			if (typeFilter.is_string())
			{
				string stf = typeFilter.get<string>();
				if (p->type.find(stf)==string::npos)
					continue;
			}
		}
		if (nameFilter != nullptr && nameFilter.is_string())
		{
			if (nameFilter.get<string>() != p->name)
				continue;
		}


		json jSession;
		jSession["type"] = p->type;
		jSession["ip"] = p->ip;
		jSession["port"] = p->port;
		jSession["name"] = p->name;
		jSession["transLayer"] = p->iTLProto;
		jSession["createTime"] = timeopt::st2str(p->stCreateTime);
		if (p->lastMethodCalled != "")
		{
			jSession["lastMethodCalled"] = p->lastMethodCalled;
		}
		jSession["lastRecvTime"] = timeopt::st2str(p->lastRecvTime);
		jSession["lastSendTime"] = timeopt::st2str(p->lastSendTime);
		jSession["sendBytes"] = p->getSendedBytes();
		jSession["recvBytes"] = p->getRecvedBytes();
		jSession["buffLen"] = p->m_alBuf.iStreamLen;
		jSession["abandonLen"] = p->abandonLen;
		jSession["lastMethod"] = p->lastMethodCalled;

		string ioAddrInSession = "";
		for (int i = 0; i < p->m_vecIoDev.size(); i++)
		{
			if (i > 0)
				ioAddrInSession += ";";
			ioAddrInSession += p->m_vecIoDev[i];
			ioAddrInSession += ",";
			ioAddrInSession += p->m_vecIoBindTag[i];
		}
		jSession["ioAddr"] = ioAddrInSession;

		string ioAddrInSessionHist = "";
		for (int i = 0; i < p->m_vecHistIoDev.size(); i++)
		{
			if (i > 0)
				ioAddrInSessionHist += ";";
			ioAddrInSessionHist += p->m_vecHistIoDev[i];
			ioAddrInSessionHist += ",";
			ioAddrInSessionHist += p->m_vecHistIoBindTag[i];
		}
		jSession["ioAddrHist"] = ioAddrInSessionHist;
		
		if (p->type == "video" && p->pTcpSession)
		{
			jSession["sendBytes"] = p->pTcpSession->iSendSucCount;
			jSession["sendFailBytes"] = p->pTcpSession->iSendFailCount;
		}
		jList.push_back(jSession);
	}

	string s = jList.dump(2);
	return s;
}


string dataServer::getRDSPage()
{
	string s = R"delimiter(
		<!DOCTYPE html>
		<html>
			<head>
			<style type="text/css">
					* {
						margin: 0;
						padding: 0;
						border: 0;
					}
					html {
						height: 100%;
						width:100%;
					}
					body {
						width: 100%;
						height: 100%;
						background-color: rgb(30, 30, 30);
					}
					#livestream {
						width: 100%;
						height: 100%
					}
					/*
					video::-webkit-media-controls-fullscreen-button {
						display: none;
					}*/
					/*播放按钮*/
					video::-webkit-media-controls-play-button {
						display: none;
					}
					/*进度条*/
					video::-webkit-media-controls-timeline {
						display: none;
					}
					/*观看的当前时间*/
					video::-webkit-media-controls-current-time-display{
						display: none;           
					}
					/*剩余时间*/
					video::-webkit-media-controls-time-remaining-display {
						display: none;           
					}
					/*音量按钮*/
					video::-webkit-media-controls-mute-button {
						display: none;           
					}
					video::-webkit-media-controls-toggle-closed-captions-button {
						display: none;           
					}
					/*音量的控制条*/
					video::-webkit-media-controls-volume-slider {
						display: none;           
					}
			</style>
			</head>
			<body>
				<video id="livestream" controls="false" autoplay="autoplay" muted="muted">
					您的浏览器不支持 video 标签。
				</video>
			</body>
			<script>
			// see https://w3c.github.io/media-source/#dom-evt-sourceopen for mse specification
			// use F12->more options->more tools->media to debug
			// check https://www.w3.org/TR/mse-byte-stream-format-isobmff to debug the video stream format
			// see https://html.spec.whatwg.org/multipage/media.html#the-video-element about the video element
				var h5player=(function(){
					var h5playerFun = function(config){};
					
					//初始化函数
					h5playerFun.prototype.init = function(config) {
						this.videoelement = document.getElementById(config.elementid);
						this.videoelement.addEventListener('error',function(e){
							console.log(e);
						});

						this.videoelement.addEventListener('play',()=>{
							console.log("Resuming video playback");
							// jump to last frame to catch up with stream
							if (this.videoelement.buffered.length > 0)
							this.videoelement.currentTime = this.videoelement.buffered.end(0);
						});

						this.wsURL=config.wsurl;
						this.ackID=config.ackid;
						return this;
					};	   
					
					//开始函数
					h5playerFun.prototype.start = function() {
						this.sourceBuffer;
						//https://developer.mozilla.org/en-US/docs/Web/Media/Formats/codecs_parameter
						//quick test for supportment https://gist.github.com/granoeste/8727308
						//this.mimeCodec = 'video/mp4; codecs="avc1.42E01E"';  //h264  has latency when use h264_mf encoder
						this.mimeCodec = 'video/webm; codecs="vp9"';
						//this.mimeCodec = 'video/mp4; codecs="mp4v.20.8"';  //mp4v.20.8  stands for mpeg4 part 2/mpeg4 visual but unsupported by chrome
						//this.mimeCodec = 'video/mp4; codecs="mp4a.40.2"'; //chrome supported   ffmpeg encode = ?
						this.ws;
						this.dataArray=[];		  
						this.mediaSource=new MediaSource();
						this.videoelement.src=URL.createObjectURL(this.mediaSource); //this step will fire sourceopen event
						this.mediaSource.addEventListener('sourceopen',()=>{_sourceOpen(this);});
					};		  
					
					//资源打开监听 
					var _sourceOpen = function(that){
						that.sourceBuffer=that.mediaSource.addSourceBuffer(that.mimeCodec);
						that.sourceBuffer.mode = "sequence";	  
						//that.sourceBuffer.addEventListener('updatestart', function(e) { console.log('sourceBuffer updatestart; readyState=' + that.mediaSource.readyState); });
						//that.sourceBuffer.addEventListener('update', function(e) { console.log('sourceBuffer update; readyState=' + that.mediaSource.readyState); });
						//that.sourceBuffer.addEventListener('updateend', function(e) { console.log('sourceBuffer updateend; readyState=' + that.mediaSource.readyState); });
						that.sourceBuffer.addEventListener('error', 
						function(e) { 
							console.log('sourceBuffer error;  readyState=' + that.mediaSource.readyState); 
							console.log(e);}
							);
						that.sourceBuffer.addEventListener('abort', function(e) { console.log('sourceBuffer abort; readyState=' + that.mediaSource.readyState); });			  
						_fetchMedia(that);
						};

					var _fetchMedia = function(that){
							//创建websocket连接
							that.ws=new WebSocket(that.wsURL);
							//设置接收数据位二进制
							that.ws.binaryType="arraybuffer";
							
							//创建成功回调函数发送ackid给服务
							that.ws.onopen=function(){
								console.log("websocket connected");
							};
							
							//接收消息回调函数
							that.ws.onmessage=function(e){
								//把接收的数据存入缓存区存满50000个字节再塞到节点播放
								this.buffer = [];
								this.buffer.push(e.data);
								var data = this.buffer.shift();
								var array = Array.prototype.slice.call(new Uint8Array(data));
								that.dataArray=that.dataArray.concat(array);
							
								if(!that.sourceBuffer.updating){
									var arrayBuffer = new Uint8Array(that.dataArray).buffer;
									that.dataArray=[];
									_addsourec(arrayBuffer,that);
									}
							};
							
							//ws关闭回调，关闭之后重新连接
							that.ws.onclose=function(){ 
							console.error("websocket closed");
							that.start();				  
							};
							
							//ws错误回调
							that.ws.onerror=function(e){
							console.log("websocket error" + e.toString());
							};            
					}; 	

					//把接收的数据存入塞到节点播放
					var _addsourec = function(buf,that){
							try{       
							that.sourceBuffer.appendBuffer(buf);
							}catch(err){
								console.error("append buf to sourceBuffer error;" + err.toString());
							}             
					};
					
					//返回构造函数
					return h5playerFun;		  
				})();
				var url = "ws://" + window.location.host + "/desktop"; 
				window.onload = function() {
					var player1=new h5player();
					var  option1={elementid:'livestream',
									wsurl:url,
									ackid:'play1',
									encodedType:'mp4'
								};
					player1.init(option1);
					player1.start(); 
				}
			</script>
		</html>



	)delimiter";
	return s;
}
