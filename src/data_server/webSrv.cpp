#include "pch.h"
#include "webSrv.h"
#include "tdsSession.h"
#include "rpcHandler.h"
#include "common/common.hpp"
#include "logger.h"
#include "proto/wsProto.h"
#include "httplib.h"
#include "sha1.hpp"
#include "common/mongoose.h"
#include "tools/hmrSrv.h"
string rootDir;
string confDir;
string filesDir;


int WS_PKT_HEADER_LEN = sizeof(int);


WebServer* webSrv = new WebServer();
WebServer* webSrvS = new WebServer();
WebServer* webSrv2 = new WebServer();
WebServer* webSrvS2 = new WebServer();

//日志监视会话
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


//io通信日志包监视会话
vector<std::shared_ptr<TDS_SESSION>> commpktSessions;
void sendToCommLog(string s)
{
	for (int i = 0; i < commpktSessions.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = commpktSessions[i];
		if (!session->isConnected())
		{
			commpktSessions.erase(commpktSessions.begin() + i);
			i--;
			continue;
		}


		session->send((char*)s.c_str(), s.length());
	}
}

//session通信监视会话
//会话数据包 监视会话。不监视自己的数据包发送。
//rpc的实时数据轮询时。 响应线程多线程处理。 会并发调用此发送接口。
vector<std::shared_ptr<TDS_SESSION>> sessionPktSessions;
shared_mutex csSessionPktSessions;
void sendToSessionPktSessions(char* p, int len)
{
	csSessionPktSessions.lock();
	for (int i = 0; i < sessionPktSessions.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = sessionPktSessions[i];
		if (!session->isConnected())
		{
			sessionPktSessions.erase(sessionPktSessions.begin() + i);
			i--;
			continue;
		}
	}
	csSessionPktSessions.unlock();

	csSessionPktSessions.lock_shared();
	for (int i = 0; i < sessionPktSessions.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = sessionPktSessions[i];
		session->send(p, len, false);
	}
	csSessionPktSessions.unlock_shared();
}





static void link_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2;
	c2->fn_data = c1;
}

static void unlink_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2->fn_data = NULL;
}

bool extractWsPkt(mg_iobuf& iobuff,int& pktLen) {
	if (iobuff.len < sizeof(int))
		return false;

	int* pLen = (int*)iobuff.buf;
	pktLen = *pLen + sizeof(int);
	if (iobuff.len >= pktLen) {
		return true;
	}
	return false;
}

//websocket主动通知数据和所线程的响应都通过触发pairdsock的 pcb 实现
static void pipeCallback(struct mg_connection* c, int ev, void* ev_data, void* fn_data) {
	struct mg_connection* parent = (struct mg_connection*)fn_data;
	//MG_INFO(("%lu %p %d %p", c->id, c->fd, ev, parent));
	if (parent == NULL) {  // If parent connection closed, close too
		c->is_closing = 1;
	}
	else if (ev == MG_EV_READ) {  // websocket的 pairsocket发完不断开
		if (parent->is_websocket) //websocket通知数据包大小不能大于 c->recv 的ioBuff的大小。大于会导致应用层分包。目前前端不进行应用层组包
		{
			//此处可能收到粘连包，使用\n\n分包
			int pktLen = 0;
			while (extractWsPkt(c->recv, pktLen)) {
				//发送1包
				mg_ws_send(parent, (const char*)c->recv.buf + WS_PKT_HEADER_LEN, pktLen - WS_PKT_HEADER_LEN, WEBSOCKET_OP_TEXT);
				//删除已发送数据
				long leftLen = c->recv.len - pktLen;
				memcpy(c->recv.buf, c->recv.buf + pktLen, leftLen);
				c->recv.len = leftLen;
			}
		}
	}
	else if (ev == MG_EV_OPEN) {
		link_conns(c, parent);
#ifdef DEBUG
		//LOG("websocket pipe建立： src conn = %p ,src sock=%d,pipe conn=%p,pipe sock=%d", parent, parent->fd, c, c->fd);
#endif
	}
	else if (ev == MG_EV_CLOSE) { //http的 pair sock发完就断开
		if (c->is_websocket)
		{

		}
		else
		{
			string resHeader = "Content-Type:application/json;charset=utf-8\r\n";
			resHeader += "Access-Control-Allow-Origin:*\r\n";  //允许所有源，也可以指定请求中的源
			mg_http_reply(parent, 200, resHeader.c_str(), (const char*)c->recv.buf);  // Respond!
		}
		unlink_conns(c, parent);
	}
}

void handleGet_gzh(mg_http_message* hm, string& resHeader,string& respBody)
{
	map<string, string> mapParams;
	string query = str::fromBuff(hm->query.ptr, hm->query.len);
	vector<string> vecParams;
	str::split(vecParams, query, "&");
	for (auto& i : vecParams)
	{
		int pos = i.find("=");
		string key = i.substr(0, pos);
		string val = i.substr(pos + 1, i.length() - pos - 1);
		mapParams[key] = val;
	}

	string  timestamp = mapParams["timestamp"];
	string	nonce = mapParams["nonce"];
	string	echostr = mapParams["echostr"];
	string  signature = mapParams["signature"];

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

	nsSHA1::SHA1 checksum;
	checksum.update(s);
	string hash = checksum.final();
	LOG("signature calc  " + hash + "\n");

	resHeader = "Content-Type:text/plain;charset=UTF-8\r\n";
	respBody = echostr;
}

void handlePost_gzh(string reqBody, string& resHeader,string& resBody)
{
	LOG("[微信公众号] Post请求\n" + reqBody);

	resBody = tds->gzhServer->getReply(reqBody);

	LOG("[微信公众号] Post回复\n" + resBody);

	string resp = resBody;
	resHeader = "Content-Type:application/json;charset=utf-8\r\n";
}


void thread_handleRpcOverHttp(string rpcReqStr,int sock)
{
	string rpcRespStr;
	string resp;
	char* binResp = NULL;
	int iBinRespLen = 0;
	bool bNeedLog = true;

	std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	rpcSrv.handleRpcCall(rpcReqStr, rpcRespStr, binResp, iBinRespLen, bNeedLog, pSession);

	string resBody = rpcRespStr;
	string ctLen = to_string(resBody.length());

	int isend = send(sock,resBody.c_str(), resBody.length(),MSG_DONTROUTE);         
	closesocket(sock);                      // Close the connection
}

void thread_handleRpcOverWebsocket(string rpcReqStr, int pipeSock)
{
	string rpcRespStr;
	string resp;
	char* binResp = NULL;
	int iBinRespLen = 0;
	bool bNeedLog = true;

	std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	rpcSrv.handleRpcCall(rpcReqStr, rpcRespStr, binResp, iBinRespLen, bNeedLog, pSession);

	string resBody = rpcRespStr;
	string ctLen = to_string(resBody.length());

	WebServer::sendToWs((char*)resBody.c_str(),resBody.length(), pipeSock);
}


static void fn(struct mg_connection* c, int ev, void* ev_data, void* fn_data) {
	WebServer* pWs = (WebServer*)c->mgr->userdata;
	if (ev == MG_EV_ACCEPT) {
		if (pWs->enableHttps)
		{
			struct mg_tls_opts opts;
			memset(&opts, 0, sizeof(opts));

			//string certPath = fs::appPath() + "/cert.pem";
			//certPath = _GB(certPath);
			//opts.cert = certPath.c_str();

			//string keyPath = fs::appPath() + "/key.pem";
			//keyPath = _GB(keyPath);
			//opts.certkey = keyPath.c_str();

			opts.cert = "cert.pem";
			opts.certkey = "key.pem";
			mg_tls_init(c, &opts);
		}
	}
	else if (ev == MG_EV_HTTP_MSG)
	{
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;
		struct mg_str* s = mg_http_get_header(hm, "Connection");
		if (s!= NULL && memcmp(s->ptr,"Upgrade",7) == 0) {
			mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to WS
			std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
			p->bConnected = true;
			string uri = str::fromBuff(hm->uri.ptr, hm->uri.len);
			ds.initWsSessionInfo(uri, p);
			//建立一个发往实际sock的管道
			int sPipe = mg_mkpipe(c->mgr, pipeCallback, c);
			c->pipeSock = sPipe;
			//记录管道发送sock口
			p->sockPipe = (SOCKET)sPipe;
			//加入websocket连接列表
			pWs->m_csWsSessions.lock();
			pWs->m_wsSessions[c] = p;
			pWs->m_csWsSessions.unlock();
		}
		else if (mg_http_match_uri(hm, "/gzh/*") || mg_http_match_uri(hm, "/gzh*")) {
			string httpReqStr = str::fromBuff(hm->message.ptr, hm->message.len);
			string resHeader, resBody;
			httplib::Server srv;
			if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0)
			{
				handlePost_gzh(hm->body.ptr, resHeader, resBody);
				mg_http_reply(c, 200, resHeader.c_str(), resBody.c_str());  
			}
			else
			{
				handleGet_gzh(hm, resHeader, resBody);
				mg_http_reply(c, 200, resHeader.c_str(), resBody.c_str());
			}
		}
		else if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0 || mg_http_match_uri(hm, "/rpc"))
		{
			int sock = mg_mkpipe(c->mgr, pipeCallback, c);                   // Create pipe
			string rpcReqStr = str::fromBuff(hm->body.ptr, hm->body.len);
			thread t(thread_handleRpcOverHttp, rpcReqStr, sock);
			t.detach();
		}
		else if (mg_http_match_uri(hm, "/release"))
		{
			string localPath = fs::appPath() + "/files/release";
			vector<string> fl;
			fs::getFileList(fl, localPath);
			string redirectPath = "/files/release/";

			map<string, string> fil;

			for (auto& i : fl)
			{
				fs::FILE_INFO fi;
				string p = localPath + "/" + i;
				fs::getFileInfo(p, fi);
				fil[fi.modifyTime] = i;
			}

			string sHeader;
			if (fil.size() > 0) //默认按照时间的升序排列
			{
				redirectPath += fil.rbegin()->second;
				sHeader = "location:" + redirectPath + "\r\n";
				sHeader += "Cache-Control:max-age=1\r\n";
			}
			else
			{
				sHeader = "location:tds.zip\r\n";
				sHeader += "Cache-Control:max-age=1\r\n";
			}
			mg_http_reply(c, 301, sHeader.c_str(),NULL);
		}
		else if (mg_http_match_uri(hm, "/apk"))
		{
			string localPath = fs::appPath() + "/files/apk";
			vector<string> fl;
			fs::getFileList(fl, localPath);
			string redirectPath = "/files/apk/";

			map<string, string> fil;

			for (auto& i : fl)
			{
				fs::FILE_INFO fi;
				string p = localPath + "/" + i;
				fs::getFileInfo(p, fi);
				fil[fi.modifyTime] = i;
			}

			string sHeader;
			if (fil.size() > 0) //默认按照时间的升序排列
			{
				redirectPath += fil.rbegin()->second;
				sHeader = "location:" + redirectPath + "\r\n";
				sHeader += "Cache-Control:max-age=1\r\n";
			}
			else
			{
				sHeader = "location:tds.zip\r\n";
				sHeader += "Cache-Control:max-age=1\r\n";
			}
			mg_http_reply(c, 301, sHeader.c_str(), "");
		}
		else if (memcmp(hm->method.ptr, "OPTIONS", hm->method.len) == 0)
		{
			// 跨域请求，使用VSCode调试时，网页从VSCode的http服务器走。该功能主要方便调试
			// 网页上使用的fetch进行rpc调用时，从tds的http服务走，因此浏览器会先发送OPTION请求跨域
			//响应跨域预检请求
			//https://developer.mozilla.org/zh-CN/docs/Web/HTTP/CORS

			mg_str* mgsOrg = mg_http_get_header(hm, "Origin");
			if (mgsOrg != nullptr)
			{
				string sOrg = str::fromBuff(mgsOrg->ptr, mgsOrg->len);
				mg_str* mgsHeaders = mg_http_get_header(hm, "Access-Control-Request-Headers");
				string sHeaders = str::fromBuff(mgsHeaders->ptr, mgsHeaders->len);

				string resHeader = "Server:tds\r\n";
				resHeader += "Access-Control-Allow-Origin:" + sOrg + "\r\n";
				resHeader += "Access-Control-Allow-Methods:POST,GET,OPTIONS\r\n";
				resHeader += "Access-Control-Allow-Headers:" + sHeaders + "\r\n";
				resHeader += "Access-Control-Max-Age:86400\r\n";

				mg_http_reply(c, 200, resHeader.c_str(), "");
			}
			
		}
		else {
			struct mg_http_serve_opts opts;
			memset(&opts, 0, sizeof(opts));
			string dir = "/=" + rootDir + ",/config/=" + confDir + ",/files/=" + filesDir;
			//string dir = "/=" + rootDir;
			opts.root_dir = dir.c_str();   // Serve local dir
			mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
		}
	}
	else if (ev == MG_EV_WS_MSG) {
		//websocket通道不用于请求，仅用于通知。后续逐步重构
		if (c->pipeSock != 0)
		{
			struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
			string rpcReqStr = str::fromBuff(wm->data.ptr, wm->data.len);
			thread t(thread_handleRpcOverWebsocket, rpcReqStr, c->pipeSock);
			t.detach();
		}
	}
	else if (ev == MG_EV_CLOSE) {
		if (c->is_websocket && c->fn_data != NULL) //如果是websocket，关闭关联的sock
		{
			//从连接的websocket列表中删除
			pWs->m_csWsSessions.lock();
			if (pWs->m_wsSessions.find(c)!= pWs->m_wsSessions.end())
			{
				std::shared_ptr < TDS_SESSION > p = pWs->m_wsSessions[c];
				closesocket(p->sockPipe);
				p->sockPipe = 0;
				p->bConnected = false;
				pWs->m_wsSessions.erase(c);
			}
			else
			{
				LOG("[warn]websocket连接断开，但是在连接列表中未找到");
			}
			pWs->m_csWsSessions.unlock();
		}

		if (c->fn_data != NULL) unlink_conns(c, (mg_connection*)c->fn_data);
	}
}


void webThread(WebServer* pSrv,int port) {
	setThreadName("mongoose polling thread");
	string proto = "http:";
	if (pSrv->enableHttps)
		proto = "https:";
	string url = proto + "//0.0.0.0:" + to_string(port);
	struct mg_mgr mgr;
	//pSrv->pMgr = &mgr;
	mg_mgr_init(&mgr);                                        // Init manager
	// !!!!!非常重要。 mg_http_listen最后一个参数不要传入pSrv等其他外部线程会操作的指针
	//传入pSrv后。由于WebServer::sendToWs会被其他线程调用。可能和mongoose内部发生多线程读写pSrv指针冲突。会导致奔溃
	//问题描述如下：
	//WebServer::sendToWs执行时 ，出现如下错误
	//Exception thrown: read access violation.
	//std::_Tree<std::_Tmap_traits<void*, std::shared_ptr<TDS_SESSION>, std::less<void*>, std::allocator<std::pair<void* const, std::shared_ptr<TDS_SESSION> > >, 0> >::_Get_scary(...)->** _Myhead** was nullptr.
	// map::begin()变成了NULL原因不明。
	// 错误出现在webSrvS中，但是实际测试的时候只用了webSrv
	//测试发现mongoose必须工作过，产生过http交互后，才会出现该问题。因此怀疑是mongoose的代码影响
	//mongoose唯一能拿到这个pSrv指针的地方就是mg_http_listen和userdata。 
	//测试发现usrdata无影响，mg_http_listen传入后就会导致偶先的奔溃
	mg_http_listen(&mgr, url.c_str() , fn , NULL);  // Setup listener 
	mgr.userdata = pSrv;
	for (;;) mg_mgr_poll(&mgr, 1000);                         // Event loop
	mg_mgr_free(&mgr);                                        // Cleanup
}


WebServer::WebServer()
{
	enableHttps = false;
}

WebServer::~WebServer()
{
}

void WebServer::run(int port,bool https)
{
	enableHttps = https;
	if (https)
	{
		LOG("[HTTPS服务	] 端口:" + to_string(port) + ",支持websocket secure");
	}
	else
	{
		LOG("[HTTP服务	] 端口:" + to_string(port) + ",支持websocket");
	}

	thread t(webThread,this,port);
	t.detach();
}


//websocket通过pipe发送的原因是为了使用moogoose的websocket secure功能
//所以不选择直接组装websocket pkt通过socket发送
//但是通过pipe发送会导致粘连包问题
void WebServer::sendToAllWs(string& s)
{
	m_csWsSessions.lock();
	std::map<void*,std::shared_ptr<TDS_SESSION>>::iterator i = m_wsSessions.begin();
	for (;i!=m_wsSessions.end();i++)
	{
		if (i->second->type != TDS_SESSION_TYPE::tdsClient)
			continue;

		WebServer::sendToWs((char*)s.c_str(), s.length(), i->second->sockPipe);
	}
	m_csWsSessions.unlock();
}

int WebServer::sendToAllWebsock(string& s)
{
	if(webSrvS)
		webSrvS->sendToAllWs(s);
	if(webSrv)
		webSrv->sendToAllWs(s);
	if (webSrvS2)
		webSrvS2->sendToAllWs(s);
	if (webSrv2)
		webSrv2->sendToAllWs(s);
	return 0;
}



//同一个websocket上存在多个rpc请求重叠调用时
//例如再等待一个设备响应，时间比较长。 同时在读取服务器缓存
//因此长度头和数据发送必须原子操作。否则会因为多线程并发导致数据错乱.不能调用2次send函数分两次发送
int WebServer::sendToWs(char* p, int len, int sockPipe)
{
	//assert(len + sizeof(len) < MG_IO_SIZE); //websocket通知数据包大小不能大于 c->recv 的ioBuff的大小。大于会导致应用层分包。目前前端不进行应用层组包
	if (len + sizeof(len) > MG_IO_SIZE) {
		LOG("[error]websocket发送数据大小超限，丢弃数据。当前发送长度:%d", len);
		return 0;
	}
	char* pData = new char[sizeof(len) + len];
	memcpy(pData, &len, sizeof(len));
	memcpy(pData + sizeof(len), p, len);
	int iSend = send(sockPipe, pData, len + sizeof(len), MSG_DONTROUTE);
	delete pData;
	return iSend;
}


bool runWebServers()
{
/*
	spec - String, containing log level, can be one of the following values :
	0 - Disable logging
		1 - Log errors only
		2 - Log errors and info messages
		3 - Log errors, intoand debug messages
		4 - Log everything
*/
	mg_log_set("0");//禁用mongoose日志

	rootDir = tds->conf->uiPath;
	confDir = tds->conf->confPath;
	confDir = fs::toAbsolutePath(confDir);
	filesDir = "./files";

	initHMRConf();
	if (tds->conf->debugMode)
	{
		hmr_conf.code = (char*)hmrCodeStr.c_str();
		hmr_conf.len = hmrCodeStr.length();
		hmr_conf.enable = 1;
	}

	LOG("[Web目录	] /       <--> " + rootDir);
	LOG("[Web目录	] /config <--> " + confDir);
	LOG("[Web目录	] /files  <--> " + filesDir);


	//LOG("webSrv init %lx", webSrv);
	//LOG("webSrvS init %lx", webSrvS);

	if (tds->conf->httpPort != 0)
	{
		webSrv->run(tds->conf->httpPort);
	}
	if (tds->conf->httpPort2 != 0)
	{
		webSrv2->run(tds->conf->httpPort2);
	}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (tds->conf->httpsPort != 0)
	{
		string certFile = fs::appPath() + "/cert.pem";
		if (!fs::fileExist(certFile))
		{
			LOG("[error]HTTPS服务缺少证书文件 ./cert.pem");
		}
		string keyFile = fs::appPath() + "/key.pem";
		if (!fs::fileExist(keyFile))
		{
			LOG("[error]HTTPS服务缺少私钥文件 ./key.pem");
		}

		webSrvS->run(tds->conf->httpsPort, true);
	}
	if (tds->conf->httpsPort2 != 0)
	{
		webSrvS2->run(tds->conf->httpsPort2, true);
	}
#endif


	return true;
}