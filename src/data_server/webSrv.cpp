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


static void link_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2;
	c2->fn_data = c1;
}

static void unlink_conns(struct mg_connection* c1, struct mg_connection* c2) {
	c1->fn_data = c2->fn_data = NULL;
}

// Pipe event handler
static void pcb(struct mg_connection* c, int ev, void* ev_data, void* fn_data) {
	struct mg_connection* parent = (struct mg_connection*)fn_data;
	//MG_INFO(("%lu %p %d %p", c->id, c->fd, ev, parent));
	if (parent == NULL) {  // If parent connection closed, close too
		c->is_closing = 1;
	}
	else if (ev == MG_EV_READ) {  // Got data from the worker thread

	}
	else if (ev == MG_EV_OPEN) {
		link_conns(c, parent);
	}
	else if (ev == MG_EV_CLOSE) {
		unlink_conns(c, parent);
		string resHeader = "Content-Type:application/json;charset=utf-8\r\n";
		mg_http_reply(parent, 200, resHeader.c_str(), "%.*s\n", c->recv.len, c->recv.buf);  // Respond!
		c->recv.len = 0;             // Tell Mongoose we've consumed data
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
	WebServer* pWs = (WebServer*)fn_data;
	if (ev == MG_EV_ACCEPT) {
		if (pWs->enableHttps)
		{
			struct mg_tls_opts opts;
			memset(&opts, 0, sizeof(opts));
			opts.cert = "cert.pem";
			opts.certkey = "key.pem";
			mg_tls_init(c, &opts);
		}
	}
	else if (ev == MG_EV_HTTP_MSG)
	{
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;
		struct mg_str* s = mg_http_get_header(hm, "Connection");
		if (mg_http_match_uri(hm, "/rpc")) {
			int sock = mg_mkpipe(c->mgr, pcb, c);                   // Create pipe
			string rpcReqStr = str::fromBuff(hm->body.ptr, hm->body.len);
			thread t(thread_handleRpcOverHttp, rpcReqStr, sock);
			t.detach();
		}
		else if (s!= NULL && memcmp(s->ptr,"Upgrade",7) == 0) {
			mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to WS
			std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
			p->sock = (SOCKET) c->fd;
			pWs->m_csWsSessions.lock();
			pWs->m_wsSessions[c] = p;
			pWs->m_csWsSessions.unlock();
		}
		else if (mg_http_match_uri(hm, "/gzh")) {
			
		}
		else if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0)
		{
			int sock = mg_mkpipe(c->mgr, pcb, c);                   // Create pipe
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
		SOCKET s = (SOCKET)c->fd;
		pWs->m_csWsSessions.lock();
		pWs->m_wsSessions.erase(c);
		pWs->m_csWsSessions.unlock();

		if (c->fn_data != NULL) unlink_conns(c, (mg_connection*)c->fn_data);
	}
}


void webThread(WebServer* pSrv,int port) {
	string proto = "http:";
	if (pSrv->enableHttps)
		proto = "https:";
	string url = proto + "//localhost:" + to_string(port);
	struct mg_mgr mgr;
	pSrv->pMgr = &mgr;
	mg_mgr_init(&mgr);                                        // Init manager
	mg_http_listen(&mgr, url.c_str() , fn , pSrv);  // Setup listener
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

void WebServer::sendToWs(string& s)
{
	m_csWsSessions.lock();
	for (auto& i : m_wsSessions)
	{
		CWSPPkt resp;
		resp.pack(s.c_str(), s.length(), WS_TEXT_FRAME);
		send(i.second->sock, resp.data, resp.len,0);
	}
	m_csWsSessions.unlock();
}
