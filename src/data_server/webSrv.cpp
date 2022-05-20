#include "pch.h"
#include "webSrv.h"
#include "tdsSession.h"
#include "rpcHandler.h"
#include "common/common.hpp"
#include "logger.h"

string rootDir;
string confDir;
string filesDir;

struct HANDLE_PARAM {
	int sock;
	string rpcReq;
};

static void start_thread(void (*f)(void*), void* p) {
#ifdef _WIN32
	_beginthread((void(__cdecl*)(void*)) f, 0, p);
#else
#define closesocket(x) close(x)
#include <pthread.h>
	pthread_t thread_id = (pthread_t)0;
	pthread_attr_t attr;
	(void)pthread_attr_init(&attr);
	(void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&thread_id, &attr, (void* (*) (void*)) f, p);
	pthread_attr_destroy(&attr);
#endif
}

static void thread_function(void* param) {

}

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
	MG_INFO(("%lu %p %d %p", c->id, c->fd, ev, parent));
	if (parent == NULL) {  // If parent connection closed, close too
		c->is_closing = 1;
	}
	else if (ev == MG_EV_READ) {  // Got data from the worker thread
		mg_http_reply(parent, 200, "Host: foo.com\r\n", "%.*s\n", c->recv.len,
			c->recv.buf);  // Respond!
		c->recv.len = 0;             // Tell Mongoose we've consumed data
	}
	else if (ev == MG_EV_OPEN) {
		link_conns(c, parent);
	}
	else if (ev == MG_EV_CLOSE) {
		unlink_conns(c, parent);
	}
}

// HTTP request callback
static void fn1(struct mg_connection* c, int ev, void* ev_data, void* fn_data) {
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;
		if (mg_http_match_uri(hm, "/fast")) {
			// Single-threaded code path, for performance comparison
			// The /fast URI responds immediately
			mg_http_reply(c, 200, "Host: foo.com\r\n", "hi\n");
		}
		else {
			// Multithreading code path
			int sock = mg_mkpipe(c->mgr, pcb, c);                   // Create pipe
			start_thread(thread_function, (void*)(size_t)sock);  // Start thread
		}
	}
	else if (ev == MG_EV_CLOSE) {
		if (c->fn_data != NULL) unlink_conns(c, (mg_connection *) c->fn_data);
	}
}



void thread_handleRpcOverHttp(string rpcReqStr)
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
	string resHeader = "Content-Length:" + ctLen + "\r\n";
	resHeader += "Content-Type:application/json;charset=utf-8\r\n";
}


static void fn(struct mg_connection* c, int ev, void* ev_data, void* fn_data) {
	if (ev == MG_EV_ACCEPT) {
		if (tds->conf->enableHttps)
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
		if (mg_http_match_uri(hm, "/rpc")) {
			string header, body;
			//handleRpcOverHttp_webSrv(hm, header, body);
			//mg_http_reply(c, 200,header.c_str(),body.c_str());  // Serve dynamic content
		}
		else if (mg_http_match_uri(hm, "/ws")) {
			mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to WS
		}
		else if (mg_http_match_uri(hm, "/gzh")) {
			
		}
		else if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0)
		{
			int sock = mg_mkpipe(c->mgr, pcb, c);                   // Create pipe

			//thread t(thread_handleRpcOverHttp,)
			//start_thread(thread_function, (void*)(size_t)sock);  // Start thread
			//string header, body;
			//handleRpcOverHttp_webSrv(hm, header, body);
			//mg_http_reply(c, 200, header.c_str(), body.c_str());  // Serve dynamic content
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
		if (c->fn_data != NULL) unlink_conns(c, (mg_connection*)c->fn_data);
	}
}

void webThread(int port) {
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);                                        // Init manager
	mg_http_listen(&mgr, "http://localhost:30001", fn, &mgr);  // Setup listener
	for (;;) mg_mgr_poll(&mgr, 1000);                         // Event loop
	mg_mgr_free(&mgr);                                        // Cleanup
}


WebServer::WebServer()
{
}

WebServer::~WebServer()
{
}

void WebServer::run(int port)
{
	rootDir = "./ui";
	confDir = tds->conf->confPath;
	filesDir = fs::appPath() + "/files";

	thread t(webThread,port);
	t.detach();
}
