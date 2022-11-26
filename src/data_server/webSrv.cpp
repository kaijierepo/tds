#include "pch.h"
#include "webSrv.h"
#include "tdsSession.h"
#include "rpcHandler.h"
#include "common/common.h"
#include "logger.h"
#include "proto/wsProto.h"
#include "httplib.h"
#include "sha1.hpp"
#include "tools/hmrSrv.h"
#include "ioSrv.h"
#include "prj.h"
#include "masterDs.h"
#include "mp.h"

string rootDir;
string confDir;
string filesDir;


int WS_PKT_HEADER_LEN = sizeof(size_t);


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

//io通信监视会话
//会话数据包 监视会话。不监视自己的数据包发送。
//rpc的实时数据轮询时。 响应线程多线程处理。 会并发调用此发送接口。
vector<std::shared_ptr<TDS_SESSION>> ioPktMonitorClient;
shared_mutex csIoPktMonitorClient;
void sendToPktMonitorClient(char* p, int len)
{
	csIoPktMonitorClient.lock();
	for (int i = 0; i < ioPktMonitorClient.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = ioPktMonitorClient[i];
		if (!session->isConnected())
		{
			ioPktMonitorClient.erase(ioPktMonitorClient.begin() + i);
			i--;
			continue;
		}
	}
	csIoPktMonitorClient.unlock();

	csIoPktMonitorClient.lock_shared();
	for (int i = 0; i < ioPktMonitorClient.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = ioPktMonitorClient[i];
		session->send(p, len, false);
	}
	csIoPktMonitorClient.unlock_shared();
}
void IOLogSend(char* p, int len, bool success,string remoteAddr)
{
	{
		shared_lock<shared_mutex> lock(csIoPktMonitorClient);
		if (ioPktMonitorClient.size() == 0)
			return;
	}


	json j;
	TIME st;
	timeopt::now(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["remoteAddr"] = remoteAddr;
	if (success)
		j["type"] = "发送成功";
	else
		j["type"] = "发送失败";
	j["len"] = len;
	j["data"] = str::bytesToHexStr(p, len);
	string s = j.dump(4);
	sendToPktMonitorClient((char*)s.c_str(), s.length());
}
void IOLogRecv(char* p, int len,string remoteAddr)
{
	{
		shared_lock<shared_mutex> lock(csIoPktMonitorClient);
		if (ioPktMonitorClient.size() == 0)
			return;
	}
	
	try {
		json j;
		TIME st;
		timeopt::now(&st);
		j["time"] = timeopt::st2strWithMilli(st);
		j["remoteAddr"] = remoteAddr;
		j["type"] = "接收";
		j["len"] = len;
		//j["data"] = str::fromBuff(p, len);
		j["data"] = str::bytesToHexStr(p, len);
		string s = j.dump();
		sendToPktMonitorClient((char*)s.c_str(), s.length());
	}
	catch (std::exception& e)
	{
		LOG("[error]接收到非utf8字符串,ioSession=%s,%s", remoteAddr.c_str(), e.what());
	}
}

bool parseIpPort(string host, string& ip, int& port)
{
	if (host.find(":") != string::npos)
	{
		vector<string> v;
		str::split(v, host, ":");
		ip = v[0];
		port = atoi(v[1].c_str());
	}
	else {
		ip = host;
		port = 80;
	}
	return true;
}


static void link_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2;
	c2->fn_data = c1;
}

static void unlink_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2->fn_data = NULL;
}

bool extractWsPkt(mg_iobuf& iobuff, size_t& pktLen) {
	if (iobuff.len < sizeof(size_t))
		return false;

	size_t* pLen = (size_t*)iobuff.buf;
	pktLen = *pLen + sizeof(size_t);
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
			size_t pktLen = 0;
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
			resHeader += "Access-Control-Allow-Private-Network: true\r\n"; //CORS-RFC1918 允许私有网络请求
			mg_http_reply(parent, 200, resHeader.c_str(), (const char*)c->recv.buf);  // Respond!
		}
		unlink_conns(c, parent);
	}
}

//https://blog.csdn.net/weixin_34242509/article/details/86260104
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


void thread_handleRpcOverHttp(string rpcReqStr,int sock,string hostname,int port,bool isHttps)
{
	RPC_RESP resp;

	std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	pSession->hostname = hostname;
	pSession->port = port;
	pSession->isHttps = isHttps;
	rpcSrv.handleRpcCall(rpcReqStr, resp, pSession);

	string resBody = resp.strResp;
	string ctLen = to_string(resBody.length());

	int isend = send(sock,resBody.c_str(), resBody.length(),MSG_DONTROUTE);         
	closesocket(sock);                      // Close the connection
}

void thread_handleDataOverWebsocket(char* pData,int len, int pipeSock, std::shared_ptr<TDS_SESSION> p)
{
	if (WebServer::handleAppLayerData_Bridge(pData, len, p)) {

	}
	else if (p->type == TDS_SESSION_TYPE::tdsClient) {
		std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
		string rpcReqStr = pData;
		RPC_RESP resp;
		rpcSrv.handleRpcCall(rpcReqStr,resp , pSession);
		string ctLen = to_string(resp.strResp.length());
		WebServer::sendToWs((char*)resp.strResp.c_str(), resp.strResp.length(), pipeSock);
	}
	else if (p->type == TDS_SESSION_TYPE::terminal) {

	}
}

bool WebServer::handle_stream_redirect(mg_http_message* hm, struct mg_connection* c) {
	mg_str* mgs_host = mg_http_get_header(hm, "Host");
	string sHost = str::fromBuff(mgs_host->ptr, mgs_host->len);
	string ip; int port;
	parseIpPort(sHost, ip, port);
	string uri = str::fromBuff(hm->uri.ptr, hm->uri.len);
	string tag = str::trimPrefix(uri, "/stream/"); 
	string proto;
	if (tag.find(".flv") != string::npos) {
		tag = str::trimSuffix(tag, ".flv");
		proto = "flv";
	}
	else if (tag.find(".rtc") != string::npos) {
		tag = str::trimSuffix(tag, ".rtc");
		proto = "rtc";
	}
	else if (tag.find(".de") != string::npos) { //数据流只在子服务的情况下需要重定向
		tag = str::trimSuffix(tag, ".de");
		proto = "de";
	}

	json jStreamUrl = rpcSrv.rpc_getStreamUrl(tag,m_isHttps,ip,port);

	if (jStreamUrl[proto] == nullptr) {
		mg_http_reply(c, 404, "", "");
		return true;
	}

	string url = jStreamUrl[proto];
	string sHeader = "location:" + url + "\r\n";
	sHeader += "Cache-Control:max-age=1\r\n";

	LOG("[视频流]url=%s,重定向到 %s", uri.c_str(), url.c_str());


	sHeader += "Access-Control-Allow-Origin:*\r\n";
	sHeader += "Access-Control-Allow-Private-Network: true\r\n"; //CORS-RFC1918 允许私有网络请求
	sHeader += "Access-Control-Allow-Methods:POST,GET,OPTIONS\r\n";
	sHeader += "Access-Control-Max-Age:86400\r\n";

	//307	Temporary Redirect	方法和消息主体都不发生变化。	由于不可预见的原因该页面暂不可用。在这种情况下，搜索引擎不会更新它们的链接。当站点支持非 GET 方法的链接或操作的时候，该状态码优于 302 状态码。
	mg_http_reply(c, 307, sHeader.c_str(), "");
	return true;
}


static void fn(struct mg_connection* c, int ev, void* ev_data, void* fn_data) {
	WebServer* pWs = (WebServer*)c->mgr->userdata;
	if (ev == MG_EV_ACCEPT) {
		if (pWs->m_isHttps)
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
		//websocket请求
		if (s!= NULL && memcmp(s->ptr,"Upgrade",7) == 0) {
			mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to WS
			std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
			p->bConnected = true;
			string uri = str::fromBuff(hm->uri.ptr, hm->uri.len);
			pWs->initWsSessionInfo(uri, p);
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
		//优先判断跨域请求预检。目前在应用中rpc请求可能跨域。
		//向互联网请求最新网页代码，向局域网发起rpc请求
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
				resHeader += "Access-Control-Allow-Private-Network: true\r\n"; //CORS-RFC1918 允许私有网络请求
				resHeader += "Access-Control-Allow-Methods:POST,GET,OPTIONS\r\n";
				resHeader += "Access-Control-Allow-Headers:" + sHeaders + "\r\n";
				resHeader += "Access-Control-Max-Age:86400\r\n";

				mg_http_reply(c, 200, resHeader.c_str(), "");
			}
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
		else if (mg_http_match_uri(hm, "/stream/*"))
		{
			pWs->handle_stream_redirect(hm, c);
		}
		else if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0 || mg_http_match_uri(hm, "/rpc"))
		{
			int sock = mg_mkpipe(c->mgr, pipeCallback, c);                   // Create pipe
			string rpcReqStr = str::fromBuff(hm->body.ptr, hm->body.len);
			mg_str* mgs_host = mg_http_get_header(hm, "Host");
			string sHost = str::fromBuff(mgs_host->ptr, mgs_host->len);
			string ip; int port;
			parseIpPort(sHost, ip, port);
			thread t(thread_handleRpcOverHttp, rpcReqStr, sock,ip,port,pWs->m_isHttps);
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
			mg_http_reply(c, 301, sHeader.c_str(),"");
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
		else if (mg_http_match_uri(hm, "/api"))
		{
			string redirectPath = "/app/apitest";
			string sHeader = "location:" + redirectPath + "\r\n";
			sHeader += "Cache-Control:max-age=1\r\n";
			mg_http_reply(c, 301, sHeader.c_str(), "");
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
		//websocket通道一般不用于请求，仅用于通知。
		//但如果需要启动6个以上的阻塞请求通信时，例如和设备通信的命令
		//由于浏览器有6个以上http连接限制，为提高并发量，此时会使用websockt 
		//目前仅用于设备面板多开的批量配置的场景
		if (c->pipeSock != 0)
		{
			std::shared_ptr<TDS_SESSION> p = pWs->getWsSession(c);
			struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
			int len = wm->data.len;
			char* pData = new char[len+1];
			pData[len] = 0;
			memcpy(pData, wm->data.ptr, wm->data.len);
			thread t(thread_handleDataOverWebsocket, pData,len, c->pipeSock, p);
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
	if (pSrv->m_isHttps)
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
	m_isHttps = false;
}

WebServer::~WebServer()
{
}

void WebServer::run(int port,bool https)
{
	m_isHttps = https;
	if (https)
	{
		string log = str::format("[HTTPS服务	] 端口:%d,支持websocket secure, https://localhost:%d 访问用户界面",port,port);
		LOG(log);
	}
	else
	{
		string log = str::format("[HTTP服务	] 端口:%d,支持websocket, http://localhost:%d 访问用户界面",port,port);
		LOG(log);
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
int WebServer::sendToWs(char* p, size_t len, int sockPipe)
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

std::shared_ptr<TDS_SESSION> WebServer::getWsSession(void* conn)
{
	m_csWsSessions.lock();
	std::shared_ptr<TDS_SESSION> p = m_wsSessions[conn];
	m_csWsSessions.unlock();
	return p;
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

	if (fs::appName() == "tds") { //tdb模式不需要
		LOG("[Web目录	] /config <--> " + confDir);
		LOG("[Web目录	] /files  <--> " + filesDir);
	}

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


void WebServer::initWsSessionInfo(string& strData, std::shared_ptr<TDS_SESSION> tdsSession)
{
	//terminal可以用来打开与某一接口的透传桥接，并发送指令
	if (strData.find("/terminal") != string::npos)
	{
		size_t pos = strData.find("terminal");
		size_t pos1 = strData.find(" ", pos);
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
			p->pSessionClientBridge = tdsSession;
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
			p->pSessionClientBridge = tdsSession;
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
		size_t pos = strData.find("tcp");
		size_t pos1 = strData.find(" ", pos);
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
	else if (strData.find("/iopkt") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::sessionPkt;
		ioPktMonitorClient.push_back(tdsSession);
		tdsSession->setActivityCheck(false);
	}
	else if (strData.find("/commpkt") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::commpkt;
		commpktSessions.push_back(tdsSession);
		tdsSession->setActivityCheck(false);
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
		string tag = str::trimPrefix(strData,"/stream/"); 
		 tag = str::trimSuffix(tag, ".de"); 
		tag = httplib::detail::decode_url(tag, false);
		MP* pmp = prj.GetMPByTag(tag); 
		if (pmp) {
			pmp->m_vecDeStreamSub.push_back(tdsSession);
		}


		//用于 泰默检测ATExpert的Genicam
		//int pos = strData.find("stream");
		//map<string, string> mapParams;
		//getUrlParams(strData, mapParams);
		//string streamId; //支持码流的tag
		//string fmt = ""; //为空，则图像不进行任何转换直接发送
		//int frameRate = 0;
		//if (mapParams.size() > 0)
		//{
		//	if (mapParams.find("streamId") != mapParams.end())
		//	{
		//		streamId = mapParams["streamId"];
		//	}
		//	if (mapParams.find("fmt") != mapParams.end())//fmt is not specified
		//	{
		//		fmt = mapParams["fmt"];
		//	}
		//	if (mapParams.find("frameRate") != mapParams.end())//fmt is not specified
		//	{
		//		frameRate = str::toInt(mapParams["frameRate"]);
		//	}
		//	streamId = httplib::detail::decode_url(streamId, false);


		//	if (streamId != "")
		//	{
		//		streamSrvNode* pVsn = streamSrv.getSrvNode(streamId);
		//		if (pVsn)
		//		{
		//			tdsSession->videoServiceNode = pVsn;
		//			tdsSession->type = TDS_SESSION_TYPE::video;
		//			STREAM_INFO si;
		//			si.pixelFmt = fmt;
		//			si.frameRate = frameRate;
		//			pVsn->addPuller(tdsSession, &si);
		//			string szLog = "[Session会话][开始] 类型:" + tdsSession->type + " 码流ID:" + streamId + " 格式:" + fmt + ",客户端地址:" + tdsSession->ip + ":" + str::fromInt(tdsSession->port);
		//			LOG(szLog);
		//		}
		//	}
		//}
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
		if (tdsSession->type == "")
			tdsSession->type = TDS_SESSION_TYPE::tdsClient;
		tdsSession->iALProto = "tdsRPC";

		string szLog = "[websocket会话][开始] 类型:" + tdsSession->type + ",地址:" + tdsSession->ip + ":" + str::fromInt(tdsSession->port);
		LOG(szLog);
	}
}

void WebServer::getUrlParams(string& url, map<string, string>& mapParams)
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


//应用层数据桥接
bool WebServer::handleAppLayerData_Bridge(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	bool bHandled = true;
	if (tdsSession->type == TDS_SESSION_TYPE::bridgeToLocalCom)
	{
		ioDev* p = ioSrv.getIODev(tdsSession->bridgedLocalCom);
		if (p && p->m_devType == IO_DEV_TYPE::GW::local_serial)
		{
			if (!p->sendData(pData, iLen))
			{
				LOG("[warn][数据桥接]发送数据到串口失败," + tdsSession->bridgedLocalCom + "," + p->m_strErrorInfo);
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
		string s = str::fromBuff(pData, iLen);
		LOG("[IO设备透传]client->dev " + s);
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}