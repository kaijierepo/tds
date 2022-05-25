#include "pch.h"
#include "webSrv.h"
#include "tdsSession.h"
#include "rpcHandler.h"
#include "common/common.hpp"
#include "logger.h"
#include "proto/wsProto.h"

 
string rootDir;
string confDir;
string filesDir;

//webServer原来以栈内存方式放在 dataServer的成员变量中
//但是会出现WebServer::sendToWs执行时 ，出现如下错误
//Exception thrown: read access violation.
//std::_Tree<std::_Tmap_traits<void*, std::shared_ptr<TDS_SESSION>, std::less<void*>, std::allocator<std::pair<void* const, std::shared_ptr<TDS_SESSION> > >, 0> >::_Get_scary(...)->** _Myhead** was nullptr.
// map::begin()变成了NULL原因不明。
// 错误出现在webSrvS中，但是实际测试的时候只用了webSrv
//改成如下这种方式后就不出现了。
WebServer* webSrv = new WebServer();
WebServer* webSrvS = new WebServer();
WebServer* webSrv2 = new WebServer();
WebServer* webSrvS2 = new WebServer();


static void link_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2;
	c2->fn_data = c1;
}

static void unlink_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2->fn_data = NULL;
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
			mg_ws_send(parent, (const char*)c->recv.buf, c->recv.len, WEBSOCKET_OP_TEXT);
			c->recv.len = 0;
		}
	}
	else if (ev == MG_EV_OPEN) {
		link_conns(c, parent);
	}
	else if (ev == MG_EV_CLOSE) { //http的 pair sock发完就断开
		if (c->is_websocket)
		{

		}
		else
		{
			string resHeader = "Content-Type:application/json;charset=utf-8\r\n";
			mg_http_reply(parent, 200, resHeader.c_str(), (const char*)c->recv.buf);  // Respond!
		}
		unlink_conns(c, parent);
	}
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
			int sock = mg_mkpipe(c->mgr, pipeCallback, c);
			//记录管道发送sock口
			p->sockPipe = (SOCKET) sock;
			//加入websocket连接列表
			pWs->m_csWsSessions.lock();
			pWs->m_wsSessions[c] = p;
			pWs->m_csWsSessions.unlock();
		}
		else if (mg_http_match_uri(hm, "/gzh")) {
			
		}
		else if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0 || mg_http_match_uri(hm, "/rpc"))
		{
			int sock = mg_mkpipe(c->mgr, pipeCallback, c);                   // Create pipe
			string rpcReqStr = str::fromBuff(hm->body.ptr, hm->body.len);
			thread t(thread_handleRpcOverHttp, rpcReqStr, sock);
			t.detach();
		}
		else {
			struct mg_http_serve_opts opts;
			memset(&opts, 0, sizeof(opts));
			string dir = rootDir + ",/config=" + confDir + ",/files=" + filesDir;
			opts.root_dir = dir.c_str();   // Serve local dir
			mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
		}
	}
	else if (ev == MG_EV_WS_MSG) {
		struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
		string rpcReqStr = str::fromBuff(wm->data.ptr, wm->data.len);
		string rpcRespStr;
		string resp;
		char* binResp = NULL;
		int iBinRespLen = 0;
		bool bNeedLog = true;
		std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
		rpcSrv.handleRpcCall(rpcReqStr, rpcRespStr, binResp, iBinRespLen, bNeedLog, pSession);

		if(rpcRespStr.length()>0)
			mg_ws_send(c, rpcRespStr.c_str(), rpcRespStr.length(), WEBSOCKET_OP_TEXT);
	}
	else if (ev == MG_EV_CLOSE) {
		if (c->is_websocket && c->fn_data != NULL) //如果是websocket，关闭关联的sock
		{
			//关闭关联的pipe socket
			mg_connection* pairC = (mg_connection*)c->fn_data;
			SOCKET s = (SOCKET)pairC->fd;
			closesocket(s);
			//从连接的websocket列表中删除
			pWs->m_csWsSessions.lock();
			std::shared_ptr<TDS_SESSION> p = pWs->m_wsSessions[c];
			p->sockPipe = 0;
			p->bConnected = false;
			pWs->m_wsSessions.erase(c);
			pWs->m_csWsSessions.unlock();
		}

		if (c->fn_data != NULL) unlink_conns(c, (mg_connection*)c->fn_data);
	}
}


void webThread(WebServer* pSrv,int port) {
	SetThreadDescription(GetCurrentThread(), L"mongoose polling thread");
	string proto = "http:";
	if (pSrv->enableHttps)
		proto = "https:";
	string url = proto + "//0.0.0.0:" + to_string(port);
	struct mg_mgr mgr;
	pSrv->pMgr = &mgr;
	mg_mgr_init(&mgr);                                        // Init manager
	mg_http_listen(&mgr, url.c_str() , fn , pSrv);  // Setup listener
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

//如果不使用mongoose里面的 tls加密的话，可以直接发原始sock 不发paird sock
void WebServer::sendToWs(string& s)
{
	m_csWsSessions.lock();
	for (auto i : m_wsSessions)
	{
		assert(s.length() < MG_IO_SIZE); //websocket通知数据包大小不能大于 c->recv 的ioBuff的大小。大于会导致应用层分包。目前前端不进行应用层组包
		send(i.second->sockPipe, s.c_str(), s.length(),0);
	}
	m_csWsSessions.unlock();
}
