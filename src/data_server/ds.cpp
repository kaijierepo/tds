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
#include "ioDev_iq60.h"
#include "tcpClt.h"
#include "commSrv.h"
#include "ioDev_genicam.h"
#include "streamServer.h"


dataServer ds;
httplib::Server httpSrv;
using namespace httplib;
void initHttpSrv(httplib::Server& svr)
{
	svr.Post("\\/db.*",
  [&](const Request &req, Response &res, const ContentReader &content_reader) {
	string pathReq = charCodec::ansi2Utf8(req.path);
	pathReq = pathReq.substr(3,pathReq.length()-3);
	string dbPath = db.m_path  +  pathReq;
	str::replace(dbPath,"\\","/");
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

void dataServer::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		GetLocalTime(&p->stCreateTime);
		p->bConnected = true;
		p->pTLServer = this;
		p->pTcpSession = pCltInfo;
		p->sock = pCltInfo->sock;
		p->port = pCltInfo->iPort;
		p->ip = pCltInfo->strIP;
		pCltInfo->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();
	}
	else
	{
		if (pCltInfo->pALSession)
		{
			std::shared_ptr<TDS_SESSION> p = NULL;
			//从列表中删除
			m_mutexTdsSessionList.lock();
			for (int i = 0; i < m_vecTdsSession.size(); i++)
			{
				if (m_vecTdsSession.at(i)->pTcpSession == pCltInfo)
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

void dataServer::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		p->bConnected = true;
		p->pTcpSessionClt = connInfo->tcpClt;
		p->sock = connInfo->sock;
		p->port = connInfo->srvPort;
		p->ip = connInfo->srvIP;
		connInfo->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();
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
		if (pALC->type == TDS_SESSION_TYPE::tunnel )
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
		else if (pALC->type == TDS_SESSION_TYPE::rpc)
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
	for (int i = 0; i < tds->conf->vecActiveSession.size(); i++)
	{
		ACTIVE_TDS_SESSION ats = tds->conf->vecActiveSession.at(i);
		tcpClt* p = new tcpClt();
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
				i->AsynConnect(i->m_pCallBackUser, i->m_strServerIP, i->m_iServerPort);
			}
		}
	}
}

bool dataServer::run()
{
	//if db folder is not exist. open will create an empty folder
	db.Open(tds->conf->dbPath,prj.m_strName);

	m_tcpSrv = new tcpSrv();
	m_wspSrv.m_pTcpServer = m_tcpSrv;
	m_wspSrv.m_pALServer = this;

	//http相关接口需要使用gb2312.因为里面调用了多字节windows api，为支持中文，此处将utf8转为gb2312
	initHttpSrv(httpSrv);
	//serve project specified ui through http.both are root path. specified ui path has higher priority
	string prjUI = tds->conf->projectConfPath + "\\ui";
	if(fs::fileExist(prjUI))
	{
		string asc_prjUI = charCodec::utf8toAnsi(prjUI);
		httpSrv.set_mount_point("/",asc_prjUI.c_str());
		LOG("[HTTP服务器] 根目录: " + prjUI + "[project specified ui]");
	}
	//custom tds ver specified ui through http
	string customUI = fs::appPath() + "\\ui";
	if (fs::fileExist(customUI))
	{
		string asc_customUI = charCodec::utf8toAnsi(customUI);
		httpSrv.set_mount_point("/", + asc_customUI.c_str());
		LOG("[HTTP服务器] 根目录: " + customUI);
	}
	//serve common ui through http
	//string path = fs::appPath() + "\\tdskit\\ui";
	//auto ret = httpSrv.set_mount_point("/", path.c_str());
	//if (!ret) {
	//	LOG("[warn]" + path + " 路径不存在,get a complete software package");
	//}
	//else
	//{
	//	LOG("[HTTP Server] root at " + path + "[tdskit common ui]");
	//}
	//serve db files through http
	string asc_dbPath = charCodec::utf8toAnsi(db.m_path);
	auto ret = httpSrv.set_mount_point("/db/", asc_dbPath.c_str());
	if (!ret) {
		LOG("[error][数据库]路径 " + db.m_path + " 不存在,请检查配置");
	}
	else
	{
		LOG("[数据库    ] 路径 " + db.m_path);
	}


	httpSrv.set_file_extension_and_mimetype_mapping("jdb", "text/x-jdb");
	httpSrv.set_file_extension_and_mimetype_mapping("html", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("htm", "text/html");

	string strName;
	if(!tds->conf->debugMode)
		m_tcpSrv->keepAliveTimeout = 30;
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

		if(tryPort > 669)
		{
			LOG("[error]no valid port can be used!!");
			exit(0);
		}
	}
	LOG("[TDS服务   ] 端口:" + str::fromInt(tryPort) + " 本机浏览器 http://localhost:" + str::fromInt(tryPort) + "访问软件用户界面");
	strName=str::format("tds(%d)", tryPort);
	m_tcpSrv->SettIOCPName(strName);

	tdsSrv.m_vecTLServer.push_back(this);


	thread t(activeSessionThread);
	t.detach();

	return  1;
}

bool dataServer::OnRecvRawTdsRpc(char* pData, int iLen, std::shared_ptr<TDS_SESSION> pALC)
{
	stream2pkt* pab = &pALC->m_alBuf;
	pab->PushStream(pData, iLen);
	while (pab->PopPkt(APP_LAYER_PROTO::TDSRPC))
	{
		pALC->iALProto = pab->m_protocolType;
		OnRecvAppLayerPkt(pab->pkt, pab->iPktLen, pALC);
	}
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

void httpReqHandleThread(httplib::detail::dsClientStream* bs, tcpSession* pCltInfo)
{
	SOCKET sock = pCltInfo->sock;
	bool close = false;
	httpSrv.process_request(*bs, true, close,nullptr);
	Sleep(10000);//此处不能过早关闭，httpSrv内部启动了线程发送文件，必须等文件发完。
	closesocket(sock);
	delete bs;
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
		if (!ps->bConnected)
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
tdsSession->m_mutex 为任务队列

此处使用队列的原因。
不能直接将OnRecvData_TCPServer收到的数据多线程调用tdsSessionProcessThread去处理
因为可能网络中一个大数据包可能会被分包为多次回调，触发多个tdsSessionProcessThread之后，
多线程可能不按照数据流本身的先后顺序执行处理，导致数据包分片数据错误从而导致处理出错
*/

void tdsSessionProcessThread(std::shared_ptr<TDS_SESSION> tdsSession)
{
	std::unique_lock<recursive_mutex> g(tdsSession->m_mutex);
	while (tdsSession->dataBuff.size() > 0)
	{
		TCP_DATA_BUFF tdb = tdsSession->dataBuff.front();
		tdsSession->dataBuff.pop();
		ds.OnRecvData_TCP(tdb.pData, tdb.iLen, tdsSession);
		delete tdb.pData;
	}
}

void dataServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	std::shared_ptr<TDS_SESSION> tdsSession = getTDSSession(pTcpSess);
	std::unique_lock<recursive_mutex> g(tdsSession->m_mutex);
	TCP_DATA_BUFF tdb;
	tdb.pData = new char[iLen];
	tdb.iLen = iLen;
	memcpy(tdb.pData, pData, iLen);
	tdsSession->dataBuff.push(tdb);
	//调用临时消费者
	thread t(tdsSessionProcessThread, tdsSession);
	t.detach();
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
	string szLog = str::format("[trace][ds]websocket session opened,client addr is %s:%d", tdsSession->ip, tdsSession->port);
	LOG(szLog);

	if (strData.find("COM") != string::npos)
	{
		int pos = strData.find("COM");
		int pos1 = strData.find(" ", pos);
		string portNum = strData.substr(pos, pos1 - pos);
		ioDev* p = ioSrv.getIODev(portNum);
		tdsSession->type = TDS_SESSION_TYPE::tunnel;
		tdsSession->setActivityCheck(false);
		if (p)
		{
			tdsSession->bridgedLocalCom = portNum;
			p->pTdsSession = tdsSession;
		}
		else
		{
			string html = portNum + " is not in the opened port list,please open it first";
			std::string header = "HTTP/1.1 200 OK\r\n";
			header += "Content-Type: text/html; charset=utf-8\r\n";
			header += "Accept-Ranges: none\r\n"; // no support for partial requests
			header += "Cache-Control: no-store, must-revalidate\r\n";
			header += "Content-Length: " + std::to_string(html.length()) + "\r\n";
			header += "\r\n";

			string resp = header + html;
			send(tdsSession->sock, (char*)resp.data(), resp.length(), 0);
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
		tdsSession->type = TDS_SESSION_TYPE::tunnel;
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

	CWSPPkt req;
	std::string handshakeString = req.GetHandshakeString(strData);
	send(tdsSession->sock, handshakeString.c_str(), handshakeString.size(), 0);

	if (strData.find("/log") != string::npos)
	{
		logTdsSessions.push_back(tdsSession);
		logger.logOutput = logToWebsock;
		tdsSession->type = TDS_SESSION_TYPE::log;
	}
	else if (strData.find("/commpkt") != string::npos)
	{
		commpktSessions.push_back(tdsSession);
		tdsSession->type = TDS_SESSION_TYPE::commpkt;
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
	else if (strData.find("video") != string::npos)
	{
		int pos = strData.find("video");
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
		if (strData.find("rpc") != string::npos)
		{

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
		tdsSession->type = TDS_SESSION_TYPE::rpc;
		tdsSession->iALProto = APP_LAYER_PROTO::TDSRPC;

		string szLog = "[Session会话][开始] 类型:" + tdsSession->type + ",客户端地址:" + tdsSession->ip + ":" + str::fromInt(tdsSession->port);
		LOG(szLog);
	}
}


void dataServer::OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	GetLocalTime(&tdsSession->lastRecvTime);
	//if it's the first time recv data from a connection. check transport layer protocol first
	//if applayer protocol is TDS RPC,transport layer protocol can be HTTP or WebSocket or RawTcp(no transport layer)
	//if applayer protocol is HTTP,transport layer is specified as none
	//首次从该链接收到数据时的处理。
	if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN)
	{
		char* ptmp = new char[iLen + 1];
		memset(ptmp, 0, iLen + 1);
		memcpy(ptmp, pData, iLen);
		string strData = ptmp;
		delete ptmp;

		//parse transfer layer protocol
		if(isHttpPkt(strData))
		{
			tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP;
			if (CWSPPkt::isHandShake(strData))
			{
				tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET;
			}
			else if(strData.find("/rpc") != string::npos)
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


	// extract app layer data and handle it
	// tds rpc over websocket
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
	//tds rpc over http
	else if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP)
	{
		//rpc 先组包后处理
		if (tdsSession->iALProto == APP_LAYER_PROTO::TDSRPC)
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			while (pab->PopPkt(APP_LAYER_PROTO::HTTP))
			{
				tdsSession->iALProto = pab->m_protocolType;
				onRecvHttpPkt(pab->pkt, pab->iPktLen, tdsSession);
			}
		}
		//其他url流式处理，避免长度很长的请求包造成不必要的组包消耗
		else
		{
			httplib::detail::dsClientStream* bs = NULL;
			if(tdsSession->dsCltStream == NULL)
			{
				bs = new httplib::detail::dsClientStream;
				bs->sock_ = tdsSession->pTcpSession->sock;
				tdsSession->dsCltStream = bs;
				std::thread t(httpReqHandleThread, bs, tdsSession->pTcpSession);
				t.detach();
			}
			else
			{
				bs = (httplib::detail::dsClientStream*)tdsSession->dsCltStream;
			}

			bs->appendBuffer(pData, iLen);
		}
	}
	else if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_NONE)
	{	
		//应用层协议智能检测。根据收到的首包数据进行检测
		//傲华尔远程控制协议
		if ((pData[0] == '[' && pData[iLen - 1] == ']') ||
			(pData[0] == '[' && pData[iLen - 1] == '\n' && pData[iLen - 2] == ']')
			)
		{
			tdsSession->iALProto = APP_LAYER_PROTO::IQ60;
		}
		//tdsRPC协议
		else
		{
			tdsSession->iALProto = APP_LAYER_PROTO::TDSRPC;
		}


		if (tdsSession->iALProto == APP_LAYER_PROTO::IQ60)
		{
			stream2pkt* pab = &tdsSession->m_alBuf;
			pab->PushStream(pData, iLen);
			while (pab->PopPkt(APP_LAYER_PROTO::IQ60))
			{
				onRecvIQ60Pkt(pab->pkt, pab->iPktLen, tdsSession);
			}
		}
		else
		{
			//tds rpc over tcp
			OnRecvRawTdsRpc(pData, iLen, tdsSession);
		}
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



void dataServer::SendData(char* pData, int iLen)
{
	for (int i = 0; i < m_tcpSrv->m_vecContInfo.size(); i++)
	{
		tcpSession* pCltInfo = &m_tcpSrv->m_vecContInfo.at(i)->m_cltInfo;
		std::shared_ptr<TDS_SESSION> pAppLayerClt = getTDSSession(pCltInfo);
		if (pAppLayerClt->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
		{
			m_wspSrv.sendData((char*)pData, iLen, pCltInfo);
		}
		else
		{
			m_tcpSrv->SendData((char*)pData, iLen, pCltInfo);
		}
	}
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


		string szLog = str::format("[trace][ds]tdsrpc over http session opened,client addr is %s:%d",pALC->pTcpSession->strIP,pALC->pTcpSession->iPort);
		LOG(szLog);
		pALC->iALProto = APP_LAYER_PROTO::TDSRPC;
		pALC->type = TDS_SESSION_TYPE::rpc;

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
		
		RPC_RESP resp;
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
		
		tdsSrv.handleRpcCall(strRpc, resp, pALC);

		if (resp.textResp != "")
		{
			string httpHead = "HTTP/1.1 200 OK\r\n";
			httpHead += "Connection: close\r\n";
			httpHead += "Content-Length: " + str::fromInt(resp.textResp.length()) + "\r\n";
			httpHead += "Content-Type: application/json;charset=utf-8\r\n";
			string origin = req.get_header_value("origin");
			if (origin != "")
			{
				httpHead += "Access-Control-Allow-Origin: " + req.get_header_value("Origin") + "\r\n";
			}
			string httpResp = httpHead + "\r\n" + resp.textResp;
			pALC->send((char*)httpResp.data(), httpResp.length());
		}
		if (resp.binLen > 0)
		{
			string httpHead = "HTTP/1.1 200 OK\r\n";
			httpHead += "Connection: close\r\n";
			httpHead += "Content-Length: " + str::fromInt(resp.binLen) + "\r\n";
			httpHead += "Content-Type: application/octet-stream\r\n";
			string origin = req.get_header_value("origin");
			if (origin != "")
			{
				httpHead += "Access-Control-Allow-Origin: " + req.get_header_value("Origin") + "\r\n";
			}
			pALC->send((char*)httpHead.data(), httpHead.length());
			pALC->send((char*)"\r\n", 2);
			pALC->send((char*)resp.binResp, resp.binLen);
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


bool dataServer::OnRecvAppLayerPkt(char* pDataBuf, int iLen, std::shared_ptr<TDS_SESSION> pALC)
{
	DWORD dwDataLen = iLen;

	if (pALC->bridgedLocalCom != "")//tds link is bridged to a local com
	{
		ioDev* p = ioSrv.getIODev(pALC->bridgedLocalCom);
		if (p)
		{
			p->sendData(pDataBuf, iLen);
		}
	}
	else if (pALC->pBridgedTcpClient != NULL)
	{
		pALC->pBridgedTcpClient->SendData(pDataBuf, iLen);
	}
	else if (pALC->iALProto == APP_LAYER_PROTO::TDSRPC)
	{
		char* szJson = new char[iLen + 1];
		memset(szJson, 0, iLen + 1);
		memcpy(szJson, pDataBuf, iLen);

		string req = szJson;
		if (szJson)
		{
			delete szJson;
			szJson = NULL;
		}

		RPC_RESP resp;
		tdsSrv.handleRpcCall(req, resp, pALC);

		if(resp.textResp!="")
		{
			pALC->sendContent = "text";
			pALC->send((char*)resp.textResp.data(), resp.textResp.length());
		}
		
		if(resp.binLen > 0)
		{
			pALC->sendContent = "binary";
			pALC->send((char*)resp.binResp, resp.binLen);
		}
		
		return 1;
	}

	return(true);
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
				if (typeFilter.get<string>() != p->type)
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
