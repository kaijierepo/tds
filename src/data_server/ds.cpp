#include "pch.h"
#include "ds.h"
#include "logger.h"
#include "prj.h"
#include "httplib.h"
#include "data_server/dsClientStream.h"
#include "mp.h"
#include "tdspSrv.h"
#include "video/remoteDesktopServer.h"
#include <memory>
#include "ioSrv.h"


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
      content_reader([&](const char *data, size_t data_length) {
		fs::appendFile(dbPath,(char*)data,data_length);
        return true;
      });
      res.set_content(body, "text/plain");
    }
  });
}


dataServer::dataServer()
{
	
}

dataServer::~dataServer()
{
}

void dataServer::ConnStatusChange(tcpSession* pCltInfo, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		p->sock = pCltInfo->sock;
		p->boolConnected = true;
		p->pTLServer = this;
		p->pTcpClt = pCltInfo;
		p->ip = str::format("%s:%d", pCltInfo->strIP, pCltInfo->iPort);
		pCltInfo->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();
	}
	else
	{
		if (pCltInfo->pALSession)
		{
			m_mutexTdsSessionList.lock();
			for (int i = 0; i < m_vecTdsSession.size(); i++)
			{
				if (m_vecTdsSession.at(i)->pTcpClt == pCltInfo)
				{
					std::shared_ptr<TDS_SESSION> p = m_vecTdsSession[i];
					//p->pTcpClt is a tcpSession will be deleted after ConnStatusChange callback
					//but TDS_SESSION is not deleted until all users release it
					//so here p->pTcpClt is set to none
					//this is not safe,a critical section should be used for p->pTcpClt
					//[unsafe]
					p->pTLServer = nullptr;
					p->pTcpClt = nullptr;
					if (p->pBridgedTcpClient)
					{
						delete p->pBridgedTcpClient;
					}
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
	tcpSession* pCommLayerCltInfo = (pALC)->pTcpClt;
	if (pCommLayerCltInfo)
	{
		if (pALC->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
		{
			WS_FrameType ft = WS_TEXT_FRAME;
			if (pALC->bVideoStream)
				ft = WS_BINARY_FRAME;
			if(pALC->bridgedLocalCom!="")
				ft = WS_BINARY_FRAME;
			if (pALC->pBridgedTcpClient != NULL)
				ft = WS_BINARY_FRAME;
			return m_wspSrv.sendData((char*)pData, iLen, pCommLayerCltInfo, ft);
		}
		else
		{
			return m_tcpSrv->SendData((char*)pData, iLen, pCommLayerCltInfo);
		}
	}
	else
	{
		for (int i = 0; i < m_tcpSrv->m_vecContInfo.size(); i++)
		{
			pCommLayerCltInfo = &m_tcpSrv->m_vecContInfo.at(i)->m_cltInfo;
			if (pALC->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
			{
				WS_FrameType ft = pALC->bVideoStream ? WS_BINARY_FRAME : WS_TEXT_FRAME;
				return m_wspSrv.sendData((char*)pData, iLen, pCommLayerCltInfo, ft);
			}
			else
			{
				return m_tcpSrv->SendData((char*)pData, iLen, pCommLayerCltInfo);
			}
		}
	}
	return 0;
}

int dataServer::Send(SOCKET sock, char* pBuffer, int iLength)
{
	return send(sock, pBuffer, iLength, 0);
}

bool dataServer::run()
{
	db.Open(tdsConf.dbPath,prj.m_strName);

	m_tcpSrv = new tcpSrv();
	m_wspSrv.m_pTcpServer = m_tcpSrv;
	m_wspSrv.m_pALServer = this;

	
	initHttpSrv(httpSrv);
	//serve project specified ui through http.both are root path. specified ui path has higher priority
	string prjUI = tdsConf.projectConfPath + "\\ui";
	if(fs::fileExist(prjUI))
	{
		httpSrv.set_mount_point("/", +prjUI.c_str());
		LOG("[HTTP Server] root at " + prjUI + "[project specified ui]");
	}
	//custom tds ver specified ui through http
	string customUI = fs::appPath() + "\\ui";
	if (fs::fileExist(customUI))
	{
		httpSrv.set_mount_point("/", + customUI.c_str());
		LOG("[HTTP Server] root at " + customUI + "[custom software ui]");
	}
	//serve common ui through http
	string path = fs::appPath() + "\\tdskit\\ui";
	auto ret = httpSrv.set_mount_point("/", path.c_str());
	if (!ret) {
		LOG("[warn]" + path + " is not exist,get a complete software package");
	}
	else
	{
		LOG("[HTTP Server] root at " + path + "[tdskit common ui]");
	}
	//serve db files through http
	ret = httpSrv.set_mount_point("/db/", db.m_path.c_str());
	if (!ret) {
		LOG("[error][Data Base] at " + db.m_path + " is not exist,check your configuration.");
	}
	else
	{
		LOG("[Data Base] at " + db.m_path);
	}


	httpSrv.set_file_extension_and_mimetype_mapping("jdb", "text/x-jdb");
	httpSrv.set_file_extension_and_mimetype_mapping("html", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("htm", "text/html");

	string strName;
	if(!tdsConf.debugMode)
		m_tcpSrv->keepAliveTimeout = 30;
	int tryPort = tdsConf.port;
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
		if (tryPort = 80)tryPort = 666;
		else tryPort++;
		strData = str::format("bind to port:%d fail,try %d", triedPort, tryPort);
		LOG(strData);

		if(tryPort > 669)
		{
			LOG("[error]no valid port can be used!!");
			exit(0);
		}
	}
	LOG("[TDS Server] at port " + str::fromInt(tryPort));

	strName=str::format("tds(%d)", tryPort);
	m_tcpSrv->SettIOCPName(strName);

	tdsSrv.m_vecTLServer.push_back(this);
	return  1;
}

bool dataServer::OnRecvRawTdsRpc(char* pData, int iLen, void* pCltInfo)
{
	tcpSession* pClt = (tcpSession*)pCltInfo;
	std::shared_ptr<TDS_SESSION> pALC = getTDSSession(pClt);
	if (!pALC) {
		string str = "dataServer::OnRecvAppLayerData: DSP_CLIENT_SESSION is null";
		string strText = str.c_str();
		LOG(strText);
		return false;
	}

	stream2pkt* pab = &pALC->m_alBuf;
	pab->PushStream(pData, iLen);
	while (pab->PopPkt(APP_LAYER_PROTO_TYPE::PROTOCOL_TDSRPC))
	{
		pALC->iALProto = pab->m_protocolType;
		OnRecvAppLayerPkt(pab->pkt, pab->iPktLen, pClt);
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
	Sleep(3000);
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
	while (pTestSess->bVideoStream)
	{
		pTestSess->bVideoStream = true;
		for (int i = 0; i < iDataLen;)
		{
			int iSend = 20000;
			if (i + iSend > iDataLen)
				iSend = iDataLen - i;
			if (!pTestSess->send(pData + i, iSend))
			{
				pTestSess->bVideoStream = false;
				break;
			}
			i += iSend;
			Sleep(40);
		}
	}
	pTestSess->bVideoStream = false;
	delete pData;
	bTestStream = false;
	return 0;
}

shared_ptr<TDS_SESSION> dataServer::getTDSSession(tcpSession* pTcpSess)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for(int i=0;i<m_vecTdsSession.size();i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		if(p->pTcpClt == pTcpSess)
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

std::shared_ptr<TDS_SESSION> logTdsSession = NULL;
void logToWebsock(string text)
{
	if (logTdsSession&&!logTdsSession->boolConnected)
		logTdsSession = NULL;

	if (logTdsSession)
	{
		logTdsSession->send((char*)text.c_str(), text.length());
	}
}


void dataServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	std::shared_ptr<TDS_SESSION> tdsSession = getTDSSession(pTcpSess);

	char* ptmp = new char[iLen + 1];
	memset(ptmp, 0, iLen + 1);
	memcpy(ptmp, pData, iLen);
	string strData = ptmp;
	delete ptmp;

	//check transport layer protocol first
	//if applayer protocol is TDS RPC,transport layer protocol can be HTTP or WebSocket or RawTcp(no transport layer)
	//if applayer protocol is HTTP,transport layer is specified as none
	if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN)
	{
		if(isHttpPkt(strData))
		{
			tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP;
			if (CWSPPkt::isHandShake(strData))
			{
				if (strData.find("COM") != string::npos)
				{
					int pos = strData.find("COM");
					int pos1 = strData.find(" ", pos);
					string portNum = strData.substr(pos,pos1-pos);
					ioDev* p = ioSrv.getIODev(portNum);
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
						send(pTcpSess->sock, (char*)resp.data(), resp.length(),0);
						closesocket(pTcpSess->sock);
						return;
					}
				}
				else if (strData.find("tcp") != string::npos)
				{
					int pos = strData.find("tcp");
					int pos1 = strData.find(" ", pos);
					string host = strData.substr(pos+4, pos1 - (pos+4));
					tdsSession->pBridgedTcpClient = new CTCPClient();
					
					if(tdsSession->pBridgedTcpClient->connect(&tdsSession->bridgedTcpCltHandler, host))
					{
						LOG("bridge websocket to tcp %s success", host.c_str());
					}
					else
					{
						LOG("bridge websocket to tcp %s fail", host.c_str());
						delete tdsSession->pBridgedTcpClient;
						tdsSession->pBridgedTcpClient = NULL;
						closesocket(pTcpSess->sock);
						return;
					}
				}


				CWSPPkt req;
				std::string handshakeString = req.GetHandshakeString(strData);
				send(pTcpSess->sock, handshakeString.c_str(), handshakeString.size(), 0);
				tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET;

				string szLog = str::format("[trace][ds]websocket session opened,client addr is %s:%d",pTcpSess->strIP,pTcpSess->iPort);
				LOG(szLog);

				if (strData.find("rpc") != string::npos)
				{
					if (tdsConf.debugMode)
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
				}
				else if (strData.find("log"))
				{
					logTdsSession = tdsSession;
					logger.logOutput = logToWebsock;
				}
				else if (strData.find("teststream") != string::npos && !bTestStream)
				{
					tdsSession->bVideoStream = true;
					std::thread t(ThreadfMp4OverWS,tdsSession);
					t.detach();
				}
				else if (strData.find("desktop") != string::npos)
				{
					tdsSession->bVideoStream = true;
#ifdef ENABLE_FFMPEG
					rds.startStream(tdsSession);
#endif
				}
				else if (strData.find("video") != string::npos)
				{
					int pos = strData.find("video");
					pos = strData.find('/', pos);
					if (pos != string::npos)
					{
						int pos1 = strData.find(' ', pos);
						string tag = strData.substr(pos + 1, pos1 - pos - 1);
						tag = httplib::detail::decode_url(tag,false);
						mp* p = prj.getMp(tag);
						if (p && p->m_valType == "video")
						{
							
						}
					}
				}
				
				return;
			}
		}
		else
		{
			tdsSession->iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_NONE;
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
		m_wspSrv.OnRecvWSData(pData, iLen, &tdsSession->m_tlBuf, pTcpSess);
		return;
	}
	//tds rpc over http
	else if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_HTTP)
	{
		stream2pkt* pab = &tdsSession->m_alBuf;
		pab->PushStream(pData, iLen);
		while (pab->PopPkt(APP_LAYER_PROTO_TYPE::PROTOCOL_HTTP))
		{
			tdsSession->iALProto = pab->m_protocolType;
			onRecvHttpPkt(pab->pkt, pab->iPktLen, tdsSession);
		}
	}
	else if (tdsSession->iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_NONE)
	{	
		//tds rpc over tcp
		OnRecvRawTdsRpc(pData, iLen, pTcpSess);
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
		string szLog = str::format("[trace][ds]tdsrpc over http session opened,client addr is %s:%d",pALC->pTcpClt->strIP,pALC->pTcpClt->iPort);
		LOG(szLog);
		pALC->iALProto = APP_LAYER_PROTO_TYPE::PROTOCOL_TDSRPC;

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
		string resp;
		tdsSrv.handleRpcCall(strRpc, resp, pALC);

		string httpHead = "HTTP/1.1 200 OK\r\n";
		httpHead += "Connection: close\r\n";
		httpHead += "Content-Length: " + str::fromInt(resp.length()) + "\r\n";
		httpHead += "Content-Type: application/json;charset=utf-8\r\n";

		resp = httpHead + "\r\n" + resp;

		pALC->send((char*)resp.data(), resp.length());
	}
	else
	{
		string szLog = str::format("[trace][ds]http session opened,client addr is %s:%d",pALC->pTcpClt->strIP,pALC->pTcpClt->iPort);
		LOG(szLog);
		pALC->iALProto = APP_LAYER_PROTO_TYPE::PROTOCOL_HTTP;

		//internal handle
		if(httpHandleInternal(strData,pALC))
			return true;

		//web server folder handle
		httplib::detail::dsClientStream* bs = new httplib::detail::dsClientStream;
		bs->sock_ = pALC->pTcpClt->sock;
		bs->setBuffer(pDataBuf,iLen);
		std::thread t(httpReqHandleThread, bs, pALC->pTcpClt);
		t.detach();
	}

	return true;
}


bool dataServer::OnRecvAppLayerPkt(char* pDataBuf, int iLen, void* pCltInfo)
{
	tcpSession* pClt = (tcpSession*)pCltInfo;
	SOCKET* pSocket = &pClt->sock;
	DWORD dwDataLen = iLen;

	std::shared_ptr<TDS_SESSION> pALC = getTDSSession(pClt);
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
	else if (pALC->iALProto == APP_LAYER_PROTO_TYPE::PROTOCOL_TDSRPC)
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


		string ipid;
		ipid=str::format("%s,%d", pClt->strIP, pClt->iPort);

		string resp;
		tdsSrv.handleRpcCall(req, resp, pALC);
		if (resp.length() > 0)
			pALC->send((char*)resp.data(), resp.length());

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