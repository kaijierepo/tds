#include "pch.h"
#include "webSrv.h"
#include "tdsSession.h"
#include "rpcHandler.h"
#include "rtdb.h"
#include "common.h"
#include "logger.h"

#include "ioSrv.h"
#include "prj.h"
#include "mp.h"
#include "userMng.h"
#include "ioChan.h"
#include "statusServer.h"
#include "fileUploadSrv.h"
#include "base64.h"
#include "miniz.h"
#include "video/streamServer.h"
#include <fstream>
#include <mutex>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

#define SHUT_DOWN_BOTH 2 //SD_BOTH in win,SHUT_RDWR in linux

string rootDir;
string ui_common_dir;
string confDir;
string filesDir;
string fmsDir;
string topoDir;
string dbDir;

std::map<string,fn_http_handler> g_mapHttpHandler;

// http请求黑名单： apiserver和ioserver
std::map<string, string> apiBlackList; // apiIP des
std::shared_mutex g_csApiBlackList;  //注意避免共享锁可能锁两次的问题
std::map<string, string> ioBlackList; // ioIP des
int g_bl_timeStamp=0; //ip黑名单的最新时间戳 重启恢复0  仅用来区分是否检测过

struct thread_data {
	struct mg_mgr* mgr;
	unsigned long conn_id;  // Parent connection ID
	struct mg_str message;  // Original HTTP request
};

int WS_PKT_HEADER_LEN = sizeof(size_t);

vector<WebServer*> g_WebServerList;
bool g_enableWsNotify = true;
int g_fnCount = 0;

WebServer* getWebServer(int port, bool isHttps) {
	for (int i = 0; i < g_WebServerList.size(); i++) {
		if (g_WebServerList[i]->m_port == port) {
			return g_WebServerList[i];
		}
	}
	return nullptr;
}



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


		session->send((unsigned char*)s.c_str(), s.length());
	}
}

//io通信监视会话
//会话数据包 监视会话。不监视自己的数据包发送。
//rpc的实时数据轮询时。 响应线程多线程处理。 会并发调用此发送接口。
vector<std::shared_ptr<TDS_SESSION>> ioPktMonitorClient;
shared_mutex csIoPktMonitorClient;
void sendToPktMonitorClient(char* p, size_t len)
{
	csIoPktMonitorClient.lock_shared();
	for (int i = 0; i < ioPktMonitorClient.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = ioPktMonitorClient[i];
		size_t iSend = session->send(p, len, false);
		if (iSend <= 0) {//发不成功删除
			ioPktMonitorClient.erase(ioPktMonitorClient.begin() + i);
			i--;
			continue;
		}
	}
	csIoPktMonitorClient.unlock_shared();
}


void IOLogSend(unsigned char* p, size_t len, bool success,string remoteAddr,string localAddr,string ioAddr)
{
	{
		shared_lock<shared_mutex> lock(csIoPktMonitorClient);
		if (ioPktMonitorClient.size() == 0)
			return;
	}

	string type = success ? "发送成功" : "发送失败";
	TIME st;
	timeopt::now(&st);
	string s = "{"
		"\"time\":\"" + timeopt::st2strWithMilli(st) + "\","
        "\"localAddr\":\"" + localAddr + "\","
        "\"remoteAddr\":\"" + remoteAddr + "\","
        "\"ioAddr\":\"" + ioAddr + "\","
        "\"type\":\"" + type + "\","
        "\"len\":" + to_string(len) + ","
        "\"data\":\"" + str::bytesToHexStr(p, len) + "\""
        "}";
	sendToPktMonitorClient((char*)s.c_str(), s.length());
}

void IOLogRecv(unsigned char* p, size_t len, UDP_SESSION udpSession)
{
	string r = str::format("UDP-%s:%d", udpSession.remoteIP.c_str(), udpSession.remotePort);
	string l = str::format("UDP-%s:%d", udpSession.localIP.c_str(), udpSession.localPort);
	IOLogRecv(p, len, r, l,"");
}


void IOLogRecv(unsigned char* p, size_t len,string remoteAddr,string localAddr,string ioAddr)
{
	{
		shared_lock<shared_mutex> lock(csIoPktMonitorClient);
		if (ioPktMonitorClient.size() == 0)
			return;
	}
	
	try {
		TIME st;
		timeopt::now(&st);
		string s = "{"
			"\"time\":\"" + timeopt::st2strWithMilli(st) + "\","
			"\"localAddr\":\"" + localAddr + "\","
			"\"remoteAddr\":\"" + remoteAddr + "\","
			"\"ioAddr\":\"" + ioAddr + "\","
			"\"type\":\"接收\","
			"\"len\":" + to_string(len) + ","
			"\"data\":\"" + str::bytesToHexStr(p, len) + "\""
			"}";
		sendToPktMonitorClient((char*)s.c_str(), s.length());
	}
	catch (std::exception& e)
	{
		LOG("[error]接收到非utf8字符串,ioSession=%s,%s", remoteAddr.c_str(), e.what());
	}
}
vector<std::shared_ptr<TDS_SESSION>> rpcPktMonitorClient;
shared_mutex csRpcPktMonitorClient;
void sendToRpcPktMonitorClient(char* p, size_t len)
{
	csRpcPktMonitorClient.lock_shared();
	for (int i = 0; i < rpcPktMonitorClient.size(); i++)
	{
		std::shared_ptr<TDS_SESSION> session = rpcPktMonitorClient[i];
		size_t iSend = session->send(p, len, false);
		if (iSend <= 0) {//发不成功删除
			rpcPktMonitorClient.erase(rpcPktMonitorClient.begin() + i);
			i--;
			continue;
		}
	}
	csRpcPktMonitorClient.unlock_shared();
}
void RpcLogSend(unsigned char* p, size_t len, bool success, string& remoteIP,int remotePort) {
	{
		shared_lock<shared_mutex> lock(csRpcPktMonitorClient);
		if (rpcPktMonitorClient.size() == 0)
			return;
	}

	string remoteAddr = str::format("%s:%d", remoteIP.c_str(), remotePort);
	yyjson_mut_doc* yy_mdoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* yyv_root = yyjson_mut_obj(yy_mdoc);
	TIME st;
	timeopt::now(&st);
	yyjson_mut_val* yyv_time_key = yyjson_mut_strcpy(yy_mdoc, "time");
	yyjson_mut_val* yyv_time_val = yyjson_mut_strcpy(yy_mdoc, timeopt::st2strWithMilli(st).c_str());
	yyjson_mut_obj_put(yyv_root, yyv_time_key, yyv_time_val);
	yyjson_mut_val* yyv_remoteAddr_key = yyjson_mut_strcpy(yy_mdoc, "remoteAddr");
	yyjson_mut_val* yyv_remoteAddr_val = yyjson_mut_strcpy(yy_mdoc, remoteAddr.c_str());
	yyjson_mut_obj_put(yyv_root, yyv_remoteAddr_key, yyv_remoteAddr_val);
	yyjson_mut_val* yyv_type_key = yyjson_mut_strcpy(yy_mdoc, "type");
	yyjson_mut_val* yyv_type_val = yyjson_mut_strcpy(yy_mdoc, "response");
	yyjson_mut_obj_put(yyv_root, yyv_type_key, yyv_type_val);
	if (len > 2000) {
		yyjson_mut_val* yyv_len_key = yyjson_mut_strcpy(yy_mdoc, "len");
		yyjson_mut_val* yyv_len_val = yyjson_mut_int(yy_mdoc,2000);
		yyjson_mut_obj_put(yyv_root, yyv_len_key, yyv_len_val);
	
		yyjson_mut_val* yyv_data_key = yyjson_mut_strcpy(yy_mdoc, "data");
		yyjson_mut_val* yyv_data_val = yyjson_mut_strn(yy_mdoc,(char*)p,2000);
		yyjson_mut_obj_put(yyv_root, yyv_data_key, yyv_data_val);
	}
	else {
		yyjson_mut_val* yyv_len_key = yyjson_mut_strcpy(yy_mdoc, "len");
		yyjson_mut_val* yyv_len_val = yyjson_mut_int(yy_mdoc, len);
		yyjson_mut_obj_put(yyv_root, yyv_len_key, yyv_len_val);

		yyjson_mut_val* yyv_data_key = yyjson_mut_strcpy(yy_mdoc, "data");
		yyjson_mut_val* yyv_data_val = yyjson_mut_strn(yy_mdoc, (char*)p, len);
		yyjson_mut_obj_put(yyv_root, yyv_data_key, yyv_data_val);
	}

	size_t wlen = 0;
	char* s = yyjson_mut_val_write(yyv_root, 0, &wlen);
	sendToRpcPktMonitorClient((char*)s, wlen);

	if (s) {
		free(s);
	}

	yyjson_mut_doc_free(yy_mdoc);
}
void RpcLogRecv(unsigned char* pHead, size_t headLen, unsigned char* pBody, size_t bodyLen, string remoteAddr) {
	{
		shared_lock<shared_mutex> lock(csRpcPktMonitorClient);
		if (rpcPktMonitorClient.size() == 0)
			return;
	}

	string shead,sbody;
	str::fromBuff(pHead, headLen,shead);

	if (bodyLen > 0) {
		char* out = new char[bodyLen * 2];
		memset(out, 0, bodyLen * 2);
		base64_encode(pBody, bodyLen, out);
		sbody = out;
		delete[] out;
	}


	json j;
	TIME st;
	timeopt::now(&st);
	j["time"] = timeopt::st2strWithMilli(st);
	j["remoteAddr"] = remoteAddr;
	j["len"] = bodyLen;
	j["head"] = shead;
	j["body"] = sbody;
	j["type"] = "request";
	string s = j.dump(4);
	sendToRpcPktMonitorClient((char*)s.c_str(), s.length());
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

//https://blog.csdn.net/weixin_34242509/article/details/86260104
//微信公众号在配置服务器时，填写了服务器地址与token后，点确定会进入此步验证
//如果验证不通过，记得检查地址是否是  /gzh
//token填写是否正确
void handleGet_gzh(mg_http_message* hm, string& resHeader,string& respBody)
{
	map<string, string> mapParams;
	string query = str::fromBuff(hm->query.ptr, hm->query.len);
	vector<string> vecParams;
	str::split(vecParams, query, "&");
	for (auto& i : vecParams)
	{
		size_t pos = i.find("=");
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

	//nsSHA1::SHA1 checksum;
	//checksum.update(s);
	//string hash = checksum.final();
	//LOG("signature calc  " + hash + "\n");

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




void thread_handleGzhReq(string req,int pipeSock)
{
	string resHeader;
	string resBody;
	handlePost_gzh(req, resHeader, resBody);

	//send 到 pair sock 在pair sock的回调中 发送http 响应
	int isend = send(pipeSock, resBody.c_str(), (int)resBody.length(), MSG_DONTROUTE);
	//closesocket(pipeSock);                      // this sock is a paired pipe sock,should be closed outside mongoose,otherwise causes handle leak
#ifdef _WIN32
	closesocket(pipeSock);   
#else
// todo_linux
#endif
}


static void* thread_handleRpcOverHttp(void* param,RPC_SESSION* pRpcSession) {
	struct thread_data* p = (struct thread_data*)param;

	RPC_RESP resp;
	std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	pSession->setRpcSession(pRpcSession);

	rpcSrv.handleRpcCall(pRpcSession->req, resp, pSession);

	mg_wakeup(p->mgr, p->conn_id, resp.strResp.c_str(), (int)resp.strResp.length());  // Respond to parent

	free((void*)p->message.ptr);            // Free all resources that were
	free(p);                                  // passed to us
	delete pRpcSession;
	return nullptr;
}

void thread_handleDataOverWebsocket(thread_data* data, std::shared_ptr<TDS_SESSION> p)
{
	if (WebServer::handleAppLayerData_Bridge((unsigned char*)data->message.ptr, data->message.len, p)) {

	}
	else if (p->type == TDS_SESSION_TYPE::tdsClient) {
		std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
		string rpcReqStr = (char*)data->message.ptr;
		RPC_RESP resp;
		rpcSrv.handleRpcCall(rpcReqStr,resp , pSession);
		string ctLen = to_string(resp.strResp.length());
		
		mg_wakeup(data->mgr, data->conn_id, resp.strResp.c_str(), (int)resp.strResp.length());  // Respond to parent
		free((void*)data->message.ptr);            // Free all resources that were
		free(data);
	}
	else if (p->type == TDS_SESSION_TYPE::terminal) {

	}
}

void thread_asynOpenStream(string tag) {
	MP* pmp = prj.GetMPByTag(tag, "zh");
	if (pmp) {
		STREAM_OPEN_PARAM op;
		op.tag = tag;
		op.streamUrl = "/" + tag;
		op.originPullUrl = pmp->m_mediaUrl;
		streamSrv.openStream(op);
	}
}

void thread_asynOpenChildTdsStream(ioDev* pChildTds,string tag) {
	string childTdsTag = pChildTds->m_strTagBind;
	LOG("[流媒体   ]该监控点属于子服务:%s,启动子服务流中转", childTdsTag.c_str());
	string tagInChild = TAG::trimRoot(tag, childTdsTag);
	json params;
	params["tag"] = tagInChild;
	params["pushTo"] = tag;

	//如果配置了非本机的流媒体服务地址，通知子服务推流到该地址。如果为本机，子服务根据tcp连接获得主服务的ip地址
	if(tds->conf->mediaSrvIP != "" && tds->conf->mediaSrvIP !="localhost" && tds->conf->mediaSrvIP !="127.0.0.1")
		params["pushToIP"] = tds->conf->mediaSrvIP;
	json err, rlt;
	pChildTds->call("openStream", params, nullptr, rlt, err);
	LOG("[流媒体   ]子服务API请求 openStream,params=" + params.dump());

	if (rlt != nullptr) {
		LOG("[流媒体   ]启动子服务流中转成功，位号:" + tag);
	}

	if (err != nullptr) {
		LOG("[流媒体   ]启动子服务流中转失败，位号:%s,错误信息:%s", tag.c_str(), err.dump().c_str());
	}
}

bool WebServer::handle_zlmhook(mg_http_message* hm, struct mg_connection* c) {
	string uri = str::fromBuff(hm->uri.ptr, hm->uri.len);
	string hookData = str::fromBuff(hm->body.ptr, hm->body.len);
	
	//zlm中的中文被编码成url格式.格式如%E8%89%AF%E9%80%94%E8%BD
	hookData = str::url_decode(hookData);

	//解析部分unicode编码。webRTC的hook会出现unicode编码
	//hookData = charCodec::utf16Str_to_utf8(hookData); 

	json j = json::parse(hookData);
	string urlParam = j["params"];

	LOG("[ZLMediaKit] webhook,uri=%s,\r\n%s", uri.c_str(), hookData.c_str());

	if(mg_http_match_uri(hm, "/zlmhook/on_play"))//播放鉴权
	{
		json jQuery = parseParamFromQuery(urlParam);
		bool bAuthentication = false;
		//用户名+token鉴权模式
		if (jQuery["user"]!=nullptr&& jQuery["token"] != nullptr)
		{
			string user = jQuery["user"];
			string token = jQuery["token"];
			bAuthentication = userMng.checkToken(user, token);
		}
		//用户名+密码鉴权模式
		else if (jQuery["user"] != nullptr && jQuery["pwd"] != nullptr) {
			string user = jQuery["user"];
			string pwd = jQuery["pwd"];

			bAuthentication = userMng.checkPwd(user, pwd);
			LOG("[warn]拉流鉴权,用户名:%s,密码:%s", user.c_str(), pwd.c_str());
		}

		json resp;
		if (bAuthentication)
		{
			resp["code"] = 0;
			resp["msg"] = "success";
		}
		else 
		{
			resp["code"] = 1;
			resp["msg"] = "Authentication fail";
		}

		string resHeader, resBody;
		resBody = resp.dump(2);
		mg_http_reply(c, 200, resHeader.c_str(), resBody.c_str());
	}
	else if(mg_http_match_uri(hm, "/zlmhook/on_stream_not_found")) {
		json jQuery = parseParamFromQuery(urlParam); 
		string tag;
		if (jQuery.contains("stream")) {
			tag = jQuery["stream"];
		}
		else {
			tag = j["stream"];
		}

		ioDev* pChildTds = ioSrv.getOwnerChildTdsDev(tag);

		LOG("[流媒体   ]播放监控点:%s", tag.c_str());

		//如果是子服务的位号，通知子服务中转流（先拉流，后推流）
		if (pChildTds) {
			thread t(thread_asynOpenChildTdsStream, pChildTds,tag);
			t.detach();
		}
		//本地
		else {
			LOG("[流媒体   ]该监控点属于本地服务，打开媒体源");
			thread t(thread_asynOpenStream, tag);
			t.detach();
		}

		json resp;
		resp["code"] = 0;
		resp["msg"] = "success";

		string resHeader, resBody;
		resBody = resp.dump(2);
		mg_http_reply(c, 200, resHeader.c_str(), resBody.c_str());
	}
	else {
		json resp;
		resp["code"] = 0;
		resp["msg"] = "success";

		string resHeader, resBody;
		resBody = resp.dump(2);
		mg_http_reply(c, 200, resHeader.c_str(), resBody.c_str());
	}

	return true;
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
	else if (tag.find(".hls") != string::npos) {
		tag = str::trimSuffix(tag, ".hls");
		proto = "hls";
	}
	else if (tag.find(".rtsp") != string::npos) {
		tag = str::trimSuffix(tag, ".rtsp");
		proto = "rtsp";
	}

	tag = str::url_decode(tag);
	MP* pmp = prj.GetMPByTag(tag,"zh");
	if (!pmp) {
		mg_http_reply(c, 404, "", "");
		return true;
	}

	//获得该url视频地址的源地址
	json jStreamUrl = rpcSrv.rpc_getStreamUrl(pmp,tag,m_isHttps,ip,port);

	if (jStreamUrl[proto] == nullptr) {
		mg_http_reply(c, 404, "", "");
		return true;
	}

	//流媒体服务器为本机，连接媒体源
	//if (pmp->m_bServeStream && !jStreamUrl["isChildTds"].get<bool>()) {
	//	rpcSrv.zlm_openStream(tag, pmp->m_mediaUrl);
	//}

	string url = jStreamUrl[proto];

	string urlParam = str::fromBuff(hm->query.ptr, hm->query.len);
	if (urlParam != "") {
		if (url.find("?") != -1)
			url += "&" + urlParam;
		else
			url += "?" + urlParam;
	}

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


void parseCookie(string& cookie, map<string, string>& mapParams)
{
	vector<string> kv;
	str::split(kv, cookie, ";");
	for (int i = 0; i < kv.size(); i++) {
		string pair = kv[i];
		vector<string> vecKv;
		str::split(vecKv, pair, "=");
		if (vecKv.size() == 2) {
			mapParams[vecKv[0]] = vecKv[1];
		}
	}
}

#ifdef ENABLE_OPENSSL
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/bn.h>
#include <openssl/err.h>

void createTestCert() {
	// 初始化OpenSSL库
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	// 创建EVP_PKEY对象来存放RSA密钥对
	EVP_PKEY* pkey = EVP_PKEY_new();
	if (pkey == nullptr) {
		fprintf(stderr, "创建EVP_PKEY对象失败\n");
		return;
	}

	// 使用EVP_PKEY_keygen来生成RSA密钥对
	EVP_PKEY_CTX* pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
	if (pkey_ctx == nullptr) {
		fprintf(stderr, "EVP_PKEY_CTX_new_id失败\n");
		EVP_PKEY_free(pkey);
		return;
	}

	if (EVP_PKEY_keygen_init(pkey_ctx) <= 0) {
		fprintf(stderr, "EVP_PKEY_keygen_init失败\n");
		EVP_PKEY_CTX_free(pkey_ctx);
		EVP_PKEY_free(pkey);
		return;
	}

	// 设置RSA密钥长度
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) <= 0) {
		fprintf(stderr, "设置RSA密钥长度失败\n");
		EVP_PKEY_CTX_free(pkey_ctx);
		EVP_PKEY_free(pkey);
		return;
	}

	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
		fprintf(stderr, "密钥生成失败\n");
		EVP_PKEY_CTX_free(pkey_ctx);
		EVP_PKEY_free(pkey);
		return;
	}

	EVP_PKEY_CTX_free(pkey_ctx); // 完成后释放上下文

	// 创建X509证书对象
	X509* x509 = X509_new();
	if (x509 == nullptr) {
		fprintf(stderr, "创建X509证书对象失败\n");
		EVP_PKEY_free(pkey);
		return;
	}

	// 设置证书版本号
	X509_set_version(x509, 2);

	// 设置证书序列号
	ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

	// 设置证书有效期
	X509_gmtime_adj(X509_get_notBefore(x509), 0);
	X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 有效期为1年

	// 设置证书公钥
	X509_set_pubkey(x509, pkey);

	// 设置证书自签名
	if (X509_sign(x509, pkey, EVP_sha256()) <= 0) {
		fprintf(stderr, "证书签名失败\n");
		X509_free(x509);
		EVP_PKEY_free(pkey);
		return;
	}

	// 将证书保存到文件
	FILE* certFile = fopen("cert.pem", "wb");
	if (certFile == nullptr) {
		fprintf(stderr, "无法打开证书文件\n");
		X509_free(x509);
		EVP_PKEY_free(pkey);
		return;
	}
	if (PEM_write_X509(certFile, x509) != 1) {
		fprintf(stderr, "写入证书文件失败\n");
		fclose(certFile);
		X509_free(x509);
		EVP_PKEY_free(pkey);
		return;
	}
	fclose(certFile);

	// 将私钥保存到文件
	FILE* keyFile = fopen("key.pem", "wb");
	if (keyFile == nullptr) {
		fprintf(stderr, "无法打开私钥文件\n");
		X509_free(x509);
		EVP_PKEY_free(pkey);
		return;
	}

	// 写入私钥
	if (PEM_write_PrivateKey(keyFile, pkey, NULL, NULL, 0, NULL, NULL) != 1) {
		fprintf(stderr, "写入私钥文件失败\n");
		fclose(keyFile);
		X509_free(x509);
		EVP_PKEY_free(pkey);
		return;
	}
	fclose(keyFile);

	// 释放资源
	X509_free(x509);
	EVP_PKEY_free(pkey);

	// 清理OpenSSL
	ERR_free_strings();
	EVP_cleanup();

	printf("测试证书已成功生成！\n");
}

#endif

void getSessionInfo(RPC_SESSION* pSession, mg_connection* c, mg_http_message* hm, WebServer* pWs) {
	mg_str* mgs_compressed = mg_http_get_header(hm, "Compressed");

	if (mgs_compressed) {
		string compressed = str::fromBuff(mgs_compressed->ptr, mgs_compressed->len);

		//basic info
		if (compressed == "1") {
			size_t outBufSize = 10 * 1024 * 1024;
			unsigned char* outBuf = new unsigned char[outBufSize];
			mz_ulong outLen = outBufSize;

			int status = uncompress(outBuf, &outLen, (unsigned char*)hm->body.ptr, hm->body.len);
			if (status == Z_OK) {
				pSession->req.resize(outLen);
				memcpy(pSession->req.data(), outBuf, outLen);
			}
			else {
			}

			delete[] outBuf;
		}
		else {
			pSession->req.resize(hm->body.len);
			memcpy(pSession->req.data(), hm->body.ptr, hm->body.len);
		}
	}
	else {
		pSession->req.resize(hm->body.len);
		memcpy(pSession->req.data(), hm->body.ptr, hm->body.len);
	}
	
	pSession->isHttps = pWs->m_isHttps;

	//local addr info
	mg_str* mgs_host = mg_http_get_header(hm, "Host");
	string sLocalAddr = str::fromBuff(mgs_host->ptr, mgs_host->len);
	string ip; int port;
	parseIpPort(sLocalAddr, ip, port);
	pSession->localIP = ip;
	pSession->localPort = port;

	//remote addr info
	unsigned char* pIP = (unsigned char*)&c->rem.ip;
	pSession->remoteIP = str::format("%d.%d.%d.%d", pIP[0], pIP[1], pIP[2], pIP[3]);;
	pSession->remotePort = c->rem.port;;
	pSession->remoteAddr = str::format("%s:%d", pSession->remoteIP.c_str(), pSession->remotePort);


	//is debug request. do not need log
	if (mg_http_match_uri(hm, "/debug") || mg_http_match_uri(hm, "/rpc/debug")) {
		pSession->isDebug = true;
	}

	//parse user info by cookie
	mg_str* mgs_cookie = mg_http_get_header(hm, "Cookie");
	if (mgs_cookie != nullptr) {
		string sCookie = str::fromBuff(mgs_cookie->ptr, mgs_cookie->len);
		map<string, string> mapParams;
		parseCookie(sCookie, mapParams);
		if (mapParams.find("user") != mapParams.end()) {
			pSession->user = mapParams["user"];
		}
	}
}


bool isNamingStyle_1(vector<fs::FILE_INFO>& list,string& certPath,string& keyPath) {
	for (int i = 0; i < list.size(); i++) {
		fs::FILE_INFO& fi = list[i];
		if (fi.name == "key.pem") {
			certPath = tds->conf->confPath + "/cert.pem";
			keyPath = tds->conf->confPath + "/key.pem";
			return true;
		}
	}
	return false;
}

bool isNamingStyle_3(vector<fs::FILE_INFO>& list, string& certPath, string& keyPath) {
	for (int i = 0; i < list.size(); i++) {
		fs::FILE_INFO& fi = list[i];
		if (fi.name.find("key") != string::npos) {
			keyPath = tds->conf->confPath + "/" + fi.name;
		}
		if (fi.name.find("cert") != string::npos) {
			certPath = tds->conf->confPath + "/" + fi.name;
		}
	}

	if (certPath != "" && keyPath != "")
		return true;

	return false;
}

bool isNamingStyle_2(vector<fs::FILE_INFO>& list,string& certPath, string& keyPath) {
	for (int i = 0; i < list.size(); i++) {
		fs::FILE_INFO& fi = list[i];
		if (fi.name.find(".key") != string::npos) {
			keyPath = tds->conf->confPath + "/" + fi.name;
		}

		if (fi.name.find(".cert") != string::npos) {
			certPath = tds->conf->confPath + "/" + fi.name;
		}
	}

	if (certPath != "" && keyPath != "")
		return true;

	//一个文件通过后缀区分，另外一个文件则根据.pem后缀判断
	if (certPath != "" && keyPath == "") {
		for (int i = 0; i < list.size(); i++) {
			fs::FILE_INFO& fi = list[i];
			if (fi.name.find(".pem") != string::npos) {
				keyPath = tds->conf->confPath + "/" + fi.name;
				return true;
			}
		}
	}

	if (certPath == "" && keyPath != "") {
		for (int i = 0; i < list.size(); i++) {
			fs::FILE_INFO& fi = list[i];
			if (fi.name.find(".pem") != string::npos) {
				certPath = tds->conf->confPath + "/" + fi.name;
				return true;
			}
		}
	}

	return false;
}

bool getSSLCertPath(string& certPath, string& keyPath) {
	vector<fs::FILE_INFO> list;
	fs::getFileList(list, tds->conf->confPath,false,".pem");
	fs::getFileList(list, tds->conf->confPath, false, ".key");
	fs::getFileList(list, tds->conf->confPath, false, ".cert");
	if (isNamingStyle_1(list,certPath,keyPath)) {
		return true;
	}
	else if (isNamingStyle_2(list, certPath, keyPath)) {
		return true;
	}
	else if (isNamingStyle_3(list, certPath, keyPath)) {
		return true;
	}
	return false;
}

string uriFormbuff(string buff)
{
	if (buff.find(" ") != -1)
	{
		return buff.substr(0, buff.find(" "));
	}

	return "";
}

// 分片上传状态：文件名 -> 块索引 -> 是否已上传
std::map<std::string, std::map<int, bool>> uploaded_chunks;
// 线程安全锁
std::mutex chunk_mutex;

bool parse_form_data(const struct mg_http_message* hm,
	std::map<std::string, std::string>& fields,
	std::map<std::string, std::pair<std::string, std::string>>& files) {
	// 1. 清空输出容器，避免残留旧数据
	fields.clear();
	files.clear();

	// 2. 空指针+空请求体保护
	if (hm == nullptr || hm->body.ptr == nullptr || hm->body.len == 0) {
		return false;
	}

	// 3. 获取并验证 Content-Type（Mongoose 7.13 原生函数实现，无未定义函数）
	struct mg_str* ct = mg_http_get_header(const_cast<struct mg_http_message*>(hm), "Content-Type");
	const char* target_prefix = "multipart/form-data";
	const size_t prefix_len = 19;
	if (ct == nullptr || ct->len < prefix_len ||
		mg_ncasecmp(ct->ptr, target_prefix, prefix_len) != 0) {
		return false;
	}

	// 4. 循环解析 multipart 表单（普通字段 + 文件分片二进制）
	struct mg_http_part part;
	size_t ofs = 0;
	while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
		if (part.name.ptr == nullptr || part.name.len == 0) continue;
		std::string field_key(part.name.ptr, part.name.len);

		if (part.filename.len == 0) {
			// 解析普通字段（chunk_index/chunk_total/filename 等）
			char value_buf[4096] = { 0 };
			mg_url_decode(part.body.ptr, part.body.len, value_buf, sizeof(value_buf), 0);
			fields[field_key] = std::string(value_buf);
		}
		else {
			// 解析文件分片（直接保留二进制数据，不解码）
			std::string file_name(part.filename.ptr, part.filename.len);
			std::string file_content(part.body.ptr, part.body.len);
			files[field_key] = { file_name, file_content };
		}
	}

	// 5. 解析成功：至少存在一个字段或一个文件分片
	return !fields.empty() || !files.empty();
}

// 保存分片数据到本地文件
void save_chunk(const std::string& filename, int chunk_index, const std::string& chunk_data) {
	// 替换为你的文件存储路径
	std::string oss = "./upload_temp/" + filename + ".part" + std::to_string(chunk_index);

	// 创建目录（如果不存在）
	size_t pos = oss.find_last_of("/");
	if (pos != std::string::npos) {
		std::string dir = oss.substr(0, pos);
#ifdef _WIN32
		// Windows 创建目录
		std::string cmd = "mkdir \"" + dir + "\" 2>nul";
#else
		// Linux/Mac 创建目录
		std::string cmd = "mkdir -p \"" + dir + "\"";
#endif
		system(cmd.c_str());
	}

	// 写入分片数据
	std::ofstream ofs(oss.c_str(), std::ios::binary);
	if (ofs) {
		ofs.write(chunk_data.c_str(), chunk_data.size());
		ofs.close();
		LOG(str::format("[文件上传服务]文件片段: %s 已保存", (filename + ".part" + std::to_string(chunk_index)).c_str()).c_str());
	}
	else {
		LOG(str::format("[文件上传服务]文件片段: %s 保存失败", (filename + ".part" + std::to_string(chunk_index)).c_str()).c_str());
	}
}

// 合并所有分片为完整文件
void merge_chunks(const std::string& filename, int total_chunks) {
	std::string totalFilePath = "./uploads/" + filename;

	// 创建上传目录（如果不存在）
	size_t pos = totalFilePath.find_last_of("/");
	if (pos != std::string::npos) {
		std::string dir = totalFilePath.substr(0, pos);
#ifdef _WIN32
		std::string cmd = "mkdir \"" + dir + "\" 2>nul";
#else
		std::string cmd = "mkdir -p \"" + dir + "\"";
#endif
		system(cmd.c_str());
	}

	std::ofstream ofs(totalFilePath.c_str(), std::ios::binary);
	if (ofs) {
		for (int i = 0; i < total_chunks; ++i) {
			std::string OneChunksPath = "./upload_temp/" + filename + ".part" + std::to_string(i);
			std::ifstream ifs(OneChunksPath.c_str(), std::ios::binary);

			if (ifs) {
				// 获取文件大小
				ifs.seekg(0, std::ios::end);
				std::streamsize size = ifs.tellg();
				ifs.seekg(0, std::ios::beg);

				// 读取并写入数据
				std::vector<char> buffer(size);
				ifs.read(buffer.data(), size);
				ofs.write(buffer.data(), size);

				ifs.close();
				// 删除临时分片文件
				std::remove(OneChunksPath.c_str());
			}
			else {
				LOG(str::format("[文件上传服务]文件: %s 打开失败,导致合并失败", (filename + ".part" + std::to_string(i)).c_str()).c_str());
				break;
			}
		}
		ofs.close();
		LOG(str::format("[文件上传服务]文件: %s 合并成功", filename.c_str()).c_str());
	}
	else {
		LOG(str::format("[文件上传服务]文件: %s 打开失败,合并失败", filename.c_str()).c_str());
	}
}

//static void fn(struct mg_connection* c, int ev, void* ev_data, void* fn_data) 
static void fn(struct mg_connection* c, int ev,void* ev_data)
{
	WebServer* pWs = (WebServer*)c->mgr->userdata;
	if (!pWs) 
		return; //刚启动runWebServers有时会出现NULL
	if (ev == MG_EV_ACCEPT) {
		// 黑名单判断		
		unsigned char* pIP = (unsigned char*)&c->rem.ip;
		string wsip = str::format("%d.%d.%d.%d", pIP[0], pIP[1], pIP[2], pIP[3]);;

		// api找到在黑名单，返回
		{
			std::shared_lock<shared_mutex> lock(g_csApiBlackList);
			if (!apiBlackList.empty() && apiBlackList.find(wsip) != apiBlackList.end())
			{
				string resHeader = "Connection: close\r\n";
				string body = "";
				mg_http_reply(c, 403, resHeader.c_str(), body.c_str());
				c->is_closing = 1;
				c->isBlackIp = true;
				c->bl_timeStamp = g_bl_timeStamp;
				return;
			}
			else {
				c->isBlackIp = false;
				c->bl_timeStamp = g_bl_timeStamp;
			}
		}
		if (pWs->m_isHttps)
		{
#ifdef ENABLE_OPENSSL
			struct mg_tls_opts opts;
			memset(&opts, 0, sizeof(opts));

			string certPath, keyPath;
			if (pWs->m_certData != "" && pWs->m_keyData!="") {
				opts.cert =  mg_str(pWs->m_certData.c_str()),
				opts.key = mg_str(pWs->m_keyData.c_str());
				mg_tls_init(c, &opts);
			}
#endif
		}
	}
	else if (ev == MG_EV_HTTP_MSG)
	{
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;
#ifdef _WIN32
		statusSrv.statisRecv(pWs->m_port, hm->message.len);
		statusSrv.m_srvStatus.webReqCount++;
#endif

		struct mg_str* s = mg_http_get_header(hm, "Connection");
		//websocket请求
		if (s!= NULL && memcmp(s->ptr,"Upgrade",7) == 0) {
			mg_ws_upgrade(c, hm, NULL);  // Upgrade HTTP to WS

			//init session connection info
			std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
			p->bConnected = true;
			string uri = uriFormbuff(hm->uri.ptr) != "" ? uriFormbuff(hm->uri.ptr) : str::fromBuff(hm->uri.ptr, hm->uri.len);
			p->conn_id = c->id;
			p->webServer = pWs;
			getSessionInfo(p.get(), c, hm, pWs);
			c->app_layer_data = p.get();

			//init session app type info
			//先执行上面代码完成TdsSession的初始化，然后执行下一句。下一句中，tdsSession可能马上会被使用
			//例如conn_id可能会马上用来发送数据。因此先初始化
			pWs->initWsSessionInfo(uri, p);

			//加入websocket连接列表.必须先执行initWsSessionInfo，内部会判断session类型
			//该列表仅记录tdsClient类型，该类型会接收到tdsRPC通知
			if (p->type == TDS_SESSION_TYPE::tdsClient || 
				p->type == TDS_SESSION_TYPE::bridgeToiodev ||
				p->type == TDS_SESSION_TYPE::bridgeToTcpClient ||
				p->type == TDS_SESSION_TYPE::bridgeToTcpServer) {
				pWs->m_csWsSessions.lock();
				pWs->m_wsSessions[c] = p;
				pWs->m_csWsSessions.unlock();
			}
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
			//string httpReqStr = str::fromBuff(hm->message.ptr, hm->message.len);
			//string resHeader, resBody;
			//httplib::Server srv;
			//if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0)
			//{
			//	int pipeSock = mg_mkpipe(c->mgr, pipeCallback, c, false);                   // Create pipe
			//	string req = str::fromBuff(hm->body.ptr, hm->body.len);
			//	thread t(thread_handleGzhReq, req, pipeSock);
			//	t.detach();
			//}
			//else
			//{
			//	handleGet_gzh(hm, resHeader, resBody);
			//	mg_http_reply(c, 200, resHeader.c_str(), resBody.c_str());
			//}
		}
		else if (mg_http_match_uri(hm, "/stream/*"))
		{
			pWs->handle_stream_redirect(hm, c);
		}
		else if (mg_http_match_uri(hm, "/zlmhook/*")) {
			pWs->handle_zlmhook(hm, c);
		}
		else if (mg_http_match_uri(hm, "/licence/*") || mg_http_match_uri(hm, "/licence/*/*")) {
			 auto it = g_mapHttpHandler.find("/licence/*");
			 if (it != g_mapHttpHandler.end()) {
				 it->second(hm, c);
			 }
		}
		else if (mg_http_match_uri(hm, "/rtdb/*") || mg_http_match_uri(hm, "/rtdb/*/*") || mg_http_match_uri(hm, "/rtdb")) {
			 rtdb_handler(hm, c);
		}
		else if (mg_http_match_uri(hm, "/rpc/*"))  // path after rpc is method name,use url param to hold rpc params
		{
			//string uri = str::fromBuff(hm->uri.ptr, hm->uri.len);
			//string query = str::fromBuff(hm->query.ptr, hm->query.len);
			//query = httplib::detail::decode_url(query, false);
			//string method = str::trimPrefix(uri, "/rpc/");
			//json jReq;
			//jReq["method"] = method;
			//jReq["params"] = pWs->parseParamFromQuery(query);
			//jReq["id"] = pWs->m_restApiID++;

			//int pipeSock = mg_mkpipe(c->mgr, pipeCallback, c, false);
			//RPC_SESSION* pSession = new RPC_SESSION;
			//getSessionInfo(pSession, c, hm, pWs);
			//pSession->req = jReq.dump();
			//c->app_layer_data = pSession;
			//if (!pSession->isDebug)
			//	RpcLogRecv((unsigned char*)hm->message.ptr, hm->message.len, pSession->remoteAddr);
			//thread t(thread_handleRpc_respBodyOnlyRltOrErr, pSession, pipeSock);
			//t.detach();
		}
		else if (memcmp(hm->method.ptr, "POST", hm->method.len) == 0)
		{
			if (mg_http_match_uri(hm, "/api/rpc") || mg_http_match_uri(hm, "/rpc") || mg_http_match_uri(hm, "/debug"))//增加"/api/rpc"是为了保证tds与JHD的接口兼容
			{
				//黑白名单处理
				if (c->bl_timeStamp != g_bl_timeStamp) {
					unsigned char* pIP = (unsigned char*)&c->rem.ip;
					string sip = str::format("%d.%d.%d.%d", pIP[0], pIP[1], pIP[2], pIP[3]);;
					std::shared_lock<shared_mutex> lock(g_csApiBlackList);
					if (!apiBlackList.empty() && apiBlackList.find(sip) != apiBlackList.end())
					{
						string resHeader = "Connection: close\r\n";
						string body = "";
						mg_http_reply(c, 403, resHeader.c_str(), body.c_str());
						c->is_closing = 1;
						c->isBlackIp = true;
						return;
					}
					else {
						c->isBlackIp = false;
					}
					c->bl_timeStamp = g_bl_timeStamp;
				}
				else {
					if (c->isBlackIp) {
						string resHeader = "Connection: close\r\n";
						mg_http_reply(c, 403, resHeader.c_str(), "");
						c->is_closing = 1;
						return;
					}
				}

				RPC_SESSION* pSession = new RPC_SESSION;
				getSessionInfo(pSession, c, hm, pWs);

				if (!pSession->isDebug)
					RpcLogRecv((unsigned char*)hm->head.ptr, hm->head.len, (unsigned char*)hm->body.ptr, hm->body.len, pSession->remoteAddr);

				c->sessionInfo = nullptr;
				if (!pSession->isDebug) {
					std::map<string, SESSION_STATIS*>::iterator iter = pWs->m_httpSessions.find(pSession->remoteIP);
					SESSION_STATIS* pSs;
					if (iter == pWs->m_httpSessions.end()) { //目前数组元素不删除
						pSs = new SESSION_STATIS;
						pWs->m_httpSessions[pSession->remoteIP] = pSs;
						pSs->remoteIP = pSession->remoteIP;
						pSs->remotePort = pSession->remotePort;
					}
					else {
						pSs = iter->second;
					}
					pSs->reqCount++;
					pSs->recv += hm->message.len;
					pSs->lastRecvTime = timeopt::now();
					c->sessionInfo = pSs;
				}

				struct thread_data* data = (thread_data*)calloc(1, sizeof(*data));  // Worker owns it
				data->message = mg_strdup(hm->message);               // Pass message
				data->conn_id = c->id;
				data->mgr = c->mgr;
				thread t(thread_handleRpcOverHttp, data, pSession);
				t.detach();
			}
			else if (mg_http_match_uri(hm, "/upload_chunk")) {
				std::map<std::string, std::string> fields;
				std::map<std::string, std::pair<std::string, std::string>> files;

				// 解析表单数据
				if (!parse_form_data(hm, fields, files)) {
					mg_http_reply(c, 400,
						"Access-Control-Allow-Origin: *\r\n"
						"Access-Control-Allow-Private-Network: true\r\n",
						"Invalid form data");
					return;
				}

				try {
					// 提取上传参数
					std::string filename = fields["filename"];
					int chunk_index = std::stoi(fields["chunk_index"]);
					int chunk_total = std::stoi(fields["chunk_total"]);
					std::string chunk_data = files["chunk"].second; // 获取分片数据

					// 线程安全处理
					std::lock_guard<std::mutex> lock(chunk_mutex);

					// 保存分片数据
					save_chunk(filename, chunk_index, chunk_data);

					// 更新已上传分片状态
					uploaded_chunks[filename][chunk_index] = true;

					// 检查是否所有分片都已上传
					bool all_chunks_uploaded = true;
					if (uploaded_chunks[filename].size() >= chunk_total) {
						for (int i = 0; i < chunk_total; ++i) {
							if (uploaded_chunks[filename].find(i) == uploaded_chunks[filename].end() ||
								!uploaded_chunks[filename][i]) {
								all_chunks_uploaded = false;
								break;
							}
						}

						// 所有分片上传完成，执行合并
						if (all_chunks_uploaded) {
							merge_chunks(filename, chunk_total);
							uploaded_chunks.erase(filename); // 清除状态
						}
					}

					// 返回成功响应（支持跨域）
					mg_http_reply(c, 200,
						"Access-Control-Allow-Origin: *\r\n"
						"Access-Control-Allow-Private-Network: true\r\n"
						"Content-Type: text/plain\r\n",
						"Chunk uploaded successfully");

				}
				catch (const std::exception& e) {
					// 异常处理
					mg_http_reply(c, 500,
						"Access-Control-Allow-Origin: *\r\n"
						"Access-Control-Allow-Private-Network: true\r\n",
						"Error: %s", e.what());
					LOG(str::format("[文件上传服务]处理请求异常: %s", e.what()).c_str());
				}
			}
		}
		else if (mg_http_match_uri(hm, "/release"))
		{
			
			string localPath = fs::appPath() + "/files/release";
			string redirectPath = "/files/release/";
			if (!fs::fileExist(localPath)) {
				localPath = fs::appPath() + "/files/发布版本";
				redirectPath = "/files/发布版本/";
			}

			vector<fs::FILE_INFO> fl;
			fs::getFileList(fl, localPath);
			

			map<string, fs::FILE_INFO> fl2;

			for (auto& i : fl)
			{
				fl2[i.modifyTime] = i;
			}

			string sHeader;
			if (fl2.size() > 0) //默认按照时间的升序排列
			{
				string newestPath = fl2.rbegin()->second.path;
				redirectPath += str::trimPrefix(newestPath, localPath);
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
			vector<fs::FILE_INFO> fl;
			fs::getFileList(fl, localPath);
			//重定向到最新的apk文件并下载
			string redirectPath = "/files/apk/";

			map<string,fs::FILE_INFO> fl2;
			for (auto& i : fl)
			{
				fl2[i.modifyTime] = i;
			}

			string sHeader;
			if (fl2.size() > 0) //默认按照时间的升序排列
			{
				string newestApkPath = fl2.rbegin()->second.path;
				string rPath = str::trimPrefix(newestApkPath, localPath);
				redirectPath += rPath;
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
			string dir = "/=" + rootDir + ",/common/=" + ui_common_dir + ",/config/=" + confDir + ",/files/=" + filesDir + ",/app/topo/res/=" + topoDir + ",/app//topo/res/=" + topoDir + ",/db/=" + dbDir;
			if (fmsDir != "") {
				dir += ",/fmsPath/=" + fmsDir;
			}
			opts.root_dir = dir.c_str();   // Serve local dir
			mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
		}
	}
	else if (ev == MG_EV_WAKEUP) {
		struct mg_str* data = (struct mg_str*)ev_data;

		if (c->is_websocket) {
			string s = str::fromBuff(data->ptr, data->len);
			mg_ws_send(c, data->ptr, data->len, WEBSOCKET_OP_TEXT);
			TDS_SESSION* p = (TDS_SESSION*)c->app_layer_data;
			p->sendedLen += data->len;
#ifdef _WIN32
			statusSrv.m_wsNetStatus.send += data->len;
#endif
		}
		else {
			if (c->sessionInfo) {
				SESSION_STATIS* pss = (SESSION_STATIS*)c->sessionInfo;
				pss->send += data->len;
			}

			string contentLen = to_string(data->len);
			//string resHeader = "Content-Type:application/json;charset=utf-8\r\n";
			string resHeader =
				"HTTP/1.1 200 OK\r\n"
				"Access-Control-Allow-Origin:*\r\n"  //允许所有源，也可以指定请求中的源
				"Access-Control-Allow-Private-Network: true\r\n" //CORS-RFC1918 允许私有网络请求
				"Content-Type: application/json\r\n"
				"Content-Length:" + contentLen + "\r\n\r\n";

			

			mg_send(c, resHeader.c_str(), resHeader.size());
			mg_send(c, data->ptr, data->len);
			c->is_resp = 0;

			if (c->sessionInfo) { //非调试类命令会话，该字段不为空。调试类会话为请求 /debug 的rpc请求
				SESSION_STATIS* pss = (SESSION_STATIS*)c->sessionInfo;
				RpcLogSend((unsigned char*)c->send.buf, c->send.len,true, pss->remoteIP, pss->remotePort);
			}

#ifdef _WIN32
			statusSrv.statisSend(pWs->m_port, data->len);
#endif
		}
	}
	else if (ev == MG_EV_WS_MSG) {
		//websocket通道一般不用于请求，仅用于通知。
		//但如果需要启动6个以上的阻塞请求通信时，例如和设备通信的命令
		//由于浏览器有6个以上http连接限制，为提高并发量，此时会使用websockt 
		//目前仅用于设备面板多开的批量配置的场景

		std::shared_ptr<TDS_SESSION> p = pWs->getWsSession(c);
		struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
		struct thread_data* data = (thread_data*)calloc(1, sizeof(*data));  // Worker owns it
		data->message = mg_strdup(wm->data);               // Pass message
		data->conn_id = c->id;
		data->mgr = c->mgr;
		p->recvedLen += wm->data.len;

#ifdef _WIN32
		statusSrv.m_wsNetStatus.recv += wm->data.len;
		statusSrv.m_srvStatus.webReqCount++;
#endif

		thread t(thread_handleDataOverWebsocket, data, p);
		t.detach();
	}
	else if (ev == MG_EV_CLOSE) {
		if (c->is_websocket) //如果是websocket，关闭关联的sock
		{
			pWs->removeWsSession(c);
		}
	}
	else if (ev != MG_EV_POLL) {
		g_fnCount++;
	}
}

void WebServer::removeWsSession(mg_connection* c) {
	bool removed = false;
	//从连接的websocket列表中删除
	m_csWsSessions.lock();
	if (m_wsSessions.find(c) != m_wsSessions.end())
	{
		std::shared_ptr < TDS_SESSION > p = m_wsSessions[c];
		p->bConnected = false;
		m_wsSessions.erase(c);
		removed = true;
	}
	m_csWsSessions.unlock();

	m_csWsBridgeSessions.lock();
	if (m_wsBridgeSessions.find(c) != m_wsBridgeSessions.end())
	{
		std::shared_ptr < TDS_SESSION > p = m_wsBridgeSessions[c];
		p->bConnected = false;
		m_wsBridgeSessions.erase(c);
		removed = true;
		if (p->bridgedIoSession) {
			p->bridgedIoSession->bridgedIoSessionClient = nullptr;
		}
	}
	m_csWsBridgeSessions.unlock();

	if(!removed)
	{
		LOG("[warn]websocket连接断开，但是在连接列表中未找到");
	}
}

void webThread(WebServer* pSrv,int port) {
	setThreadName("mongoose polling thread");
	for (;;) mg_mgr_poll(&pSrv->m_mgr, 1000);                         // Event loop
	mg_mgr_free(&pSrv->m_mgr);                                        // Cleanup
}


WebServer::WebServer() {
	m_isHttps = false;
	m_restApiID = 0;
}

WebServer::~WebServer()
{
}

void WebServer::run(int port, bool https) {
	m_isHttps = https;
	m_port = port;

	string proto = "http:";
	if (m_isHttps)
		proto = "https:";

	string url = proto + "//0.0.0.0:" + to_string(port);

	struct mg_mgr& mgr = m_mgr;
	mg_mgr_init(&mgr);  // Init manager

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
	mg_connection* c = mg_http_listen(&mgr, url.c_str() , fn , NULL);  // Setup listener 
	mg_wakeup_init(&mgr);  // Initialise wakeup socket pair
	mgr.userdata = this;

	string log;
	if (https) {
		log = str::format("[HTTPS服务	] 端口:%d,支持websocket secure, https://localhost:%d 访问用户界面",port,port);
	}
	else {
		log = str::format("[HTTP服务	] 端口:%d,支持websocket, http://localhost:%d 访问用户界面",port,port);
	}

	if(c){
		log += ",启动成功";
	}
	else{
		log ="[error]" + log + ",启动失败";
	}

	LOG(log);

	if(c){
		thread t(webThread, this, port);
		t.detach();
	}
}


//websocket通过pipe发送的原因是为了使用moogoose的websocket secure功能
//所以不选择直接组装websocket pkt通过socket发送
//但是通过pipe发送会导致粘连包问题
void WebServer::notifyAllWs(const string& method, const string& tag, const string& s)
{
	if (!g_enableWsNotify) {
		return;
	}

	m_csWsSessions.lock();
	std::map<void*,std::shared_ptr<TDS_SESSION>>::iterator i = m_wsSessions.begin();
	for (;i!=m_wsSessions.end();i++)
	{
		if (i->second->type != TDS_SESSION_TYPE::tdsClient)
			continue;

#ifdef _WIN32
		statusSrv.statisSend(i->second->localPort, s.length());
#endif
		std::shared_ptr<TDS_SESSION > pSess = i->second;

		if (pSess->isSubscribed(method, tag)) {
			notifyWs((unsigned char*)s.c_str(), s.length(), i->second->conn_id);
		}
	}
	m_csWsSessions.unlock();
}

int WebServer::notifyAllSrvAllWs(const string& method, const string& tag, const string& s)
{
	if (!g_enableWsNotify) {
		return 0;
	}
	for (auto& iter : g_WebServerList) {
		iter->notifyAllWs(method, tag, s);
	}
	return 0;
}


int WebServer::notifyWs(unsigned char* p, size_t len, unsigned long conn_id)
{
	if (!g_enableWsNotify) {
		return 0;
	}
	mg_wakeup(&m_mgr, conn_id, p, len);
	return len;
}

std::shared_ptr<TDS_SESSION> WebServer::getWsSession(void* conn)
{
	std::shared_ptr<TDS_SESSION> p = nullptr;
	m_csWsSessions.lock();
	std::map<void*, std::shared_ptr<TDS_SESSION>>::iterator iter = m_wsSessions.find(conn);
	if(iter!=m_wsSessions.end())
		p = iter->second;
	m_csWsSessions.unlock();

	if (p == nullptr) {
		m_csWsBridgeSessions.lock();
		std::map<void*, std::shared_ptr<TDS_SESSION>>::iterator iter2 = m_wsBridgeSessions.find(conn);
		if (iter2 != m_wsBridgeSessions.end())
			p = iter2->second;
		m_csWsBridgeSessions.unlock();
	}
	return p;
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <iostream>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/rand.h>

void generate_self_signed_cert(std::string& cert_str, std::string& key_str) {
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	EVP_PKEY* pkey = nullptr;
	X509* x509 = nullptr;
	BIO* cert_bio = nullptr;
	BIO* key_bio = nullptr;

	do {
		// 1. 生成密钥对
		EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
		if (!pctx) break;
		if (EVP_PKEY_keygen_init(pctx) <= 0) { EVP_PKEY_CTX_free(pctx); break; }
		if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) <= 0) { EVP_PKEY_CTX_free(pctx); break; }
		if (EVP_PKEY_keygen(pctx, &pkey) <= 0) { EVP_PKEY_CTX_free(pctx); break; }
		EVP_PKEY_CTX_free(pctx);

		// 2. 创建X509证书
		x509 = X509_new();
		if (!x509) break;
		X509_set_version(x509, 2);

		// 3. 设置序列号
		ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

		// 4. 设置有效期
		X509_gmtime_adj(X509_get_notBefore(x509), 0);
		X509_gmtime_adj(X509_get_notAfter(x509), 315360000L); // 10年

		// 5. 设置证书主题和颁发者
		X509_NAME* name = X509_get_subject_name(x509);
		X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char*)"CN", -1, -1, 0);
		X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char*)"TDS Community", -1, -1, 0);
		X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)"TDS Test Server", -1, -1, 0);
		X509_set_issuer_name(x509, name);

		// 6. 设置公钥
		X509_set_pubkey(x509, pkey);

		// 7. 添加扩展
		X509V3_CTX ctx;
		X509V3_set_ctx_nodb(&ctx);
		X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);

		X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "CA:FALSE");
		if (ext) { X509_add_ext(x509, ext, -1); X509_EXTENSION_free(ext); }
		ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "digitalSignature,keyEncipherment");
		if (ext) { X509_add_ext(x509, ext, -1); X509_EXTENSION_free(ext); }

		// 8. 签名
		if (!X509_sign(x509, pkey, EVP_sha256())) break;

		// 9. PEM输出
		cert_bio = BIO_new(BIO_s_mem());
		key_bio = BIO_new(BIO_s_mem());
		if (!PEM_write_bio_X509(cert_bio, x509)) break;
		if (!PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) break;

		char* cert_data = nullptr;
		long cert_len = BIO_get_mem_data(cert_bio, &cert_data);
		cert_str.assign(cert_data, cert_len);

		char* key_data = nullptr;
		long key_len = BIO_get_mem_data(key_bio, &key_data);
		key_str.assign(key_data, key_len);

	} while (0);

	if (cert_bio) BIO_free(cert_bio);
	if (key_bio) BIO_free(key_bio);
	if (x509) X509_free(x509);
	if (pkey) EVP_PKEY_free(pkey);

	ERR_free_strings();
	EVP_cleanup();
}
#endif


bool runWebServers() {
/*
	spec - String, containing log level, can be one of the following values :
	0 - Disable logging
	1 - Log errors only
	2 - Log errors and info messages
	3 - Log errors, intoand debug messages
	4 - Log everything
*/
	mg_log_set(1);//禁用mongoose日志

	// 读取黑名单
	loadApiBlackList();

	rootDir = tds->conf->uiPath; 
	ui_common_dir = tds->conf->uiCommonPath;

	confDir = tds->conf->confPath;
	confDir = fs::toAbsolutePath(confDir);

	dbDir = tds->conf->dbPath;
	dbDir = fs::toAbsolutePath(dbDir);

	filesDir = "./files";
	topoDir = tds->conf->confPath + "/topo/res";
	fmsDir = tds->conf->fmsPath;

	LOG("[Web目录	] /       <--> " + rootDir);

	if (fs::appName() == "tds") { //tdb模式不需要
		LOG("[Web目录	] /config <--> " + confDir);
		LOG("[Web目录	] /files  <--> " + filesDir);
		LOG("[Web目录	] /db  <--> " + dbDir);

		if (fmsDir != "") {
			LOG("[文件管理服务] /fmsPath  <--> " + fmsDir);
		}
	}

	if (tds->conf->httpPort != 0) {
		WebServer* pws = new WebServer();
		pws->run(tds->conf->httpPort);
		g_WebServerList.push_back(pws);
	}

	if (tds->conf->httpPort2 != 0) {
		WebServer* pws = new WebServer();
		pws->run(tds->conf->httpPort2);
		g_WebServerList.push_back(pws);
	}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
	if (tds->conf->httpsPort != 0)
	{
		WebServer* pws = new WebServer();
		string cert, key;
		if (!getSSLCertPath(cert, key)) {
			if (cert == "") {
				LOG("[warn]没有找到证书文件");
			}
			if (key == "") {
				LOG("[warn]没有找到私钥文件");
			}
			LOG("[warn]生成测试用自签名证书与私钥");
			generate_self_signed_cert(pws->m_certData, pws->m_keyData);
			cert = tds->conf->confPath + "/cert.pem";
			key = tds->conf->confPath + "/key.pem";
			fs::writeFile(cert, pws->m_certData);
			fs::writeFile(key, pws->m_keyData);
		}
		else {
			LOG("加载证书文件:" + cert);
			LOG("加载私钥文件:" + key);
			fs::readFile(cert, pws->m_certData);
			fs::readFile(key, pws->m_keyData);

			//cert.pem文件通常包含公钥证书，也称为X.509证书。公钥证书用于验证服务器的身份，并用于加密通信中的密钥交换。它包含了服务器的公钥、证书颁发机构（CA）的签名以及其他相关信息。客户端可以使用公钥证书来验证服务器的身份，并确保与服务器之间的通信是安全的。
			//key.pem文件通常包含私钥，也称为密钥。私钥用于对通信进行解密和签名。私钥应该始终保密，并且只有服务器才能访问它。私钥与公钥证书配对使用，以确保通信的机密性和完整性。
		}

		pws->run(tds->conf->httpsPort, true);
		g_WebServerList.push_back(pws);
	}
	//if (tds->conf->httpsPort2 != 0)
	//{
	//	WebServer* pws = new WebServer();
	//	pws->run(tds->conf->httpsPort2, true);
	//	g_WebServerList.push_back(pws);
	//}
#endif

	//if (tds->conf->fileUploadPort != 0) {
	//	fileUploadServer.run(tds->conf->fileUploadPort);
	//}

	return true;
}

bool loadApiBlackList()
{
	std::shared_lock<shared_mutex> lock(g_csApiBlackList);
	apiBlackList.clear();

	// tds->conf->confPath
	string apiJsonPath = fs::appPath() + "/blacklist.api.json";
	string apiStr;
	if (fs::readFile(apiJsonPath, apiStr))
	{
		//LOG("[keyinfo]加载API黑名单blacklist.api.json");
		try {
			json apiJson = json::parse(apiStr.c_str());
			for (auto it : apiJson)
			{
				apiBlackList[it["ip"].get<string>()] = it["desc"].get<string>();
			}
		}
		catch (std::exception& e)
		{
			string error = e.what();
			LOG("[error]解析API黑名单blacklist.api.json失败," + error);
			return false;
		}

	}
	return true;
}

bool saveApiBlackList()
{
	json apiJson = json::array();
	std::shared_lock<shared_mutex> lock(g_csApiBlackList);
	for (auto itm : apiBlackList)
	{
		json apiObj;		
		apiObj["ip"] = itm.first;
		apiObj["desc"] = itm.second;
		apiJson.push_back(apiObj);
	}

	if (apiJson.size())
	{
		string str = apiJson.dump(4);
		if (!fs::writeFile(fs::appPath() + "/blacklist.api.json", str))
		{
			return false;
		}
	}
	return true;
}

bool loadIoBlackList()
{
	// tds->conf->confPath
	string ioJsonPath = fs::appPath() + "/blacklist.io.json";
	string ioStr;
	if (fs::readFile(ioJsonPath, ioStr))
	{
		LOG("[keyinfo]加载IO黑名单blacklist.io.json");
		try {
			json ioJson = json::parse(ioStr.c_str());
			for (auto it : ioJson)
			{
				ioBlackList[it["ip"].get<string>()] = it["desc"].get<string>();
			}
		}
		catch (std::exception& e)
		{
			string error = e.what();
			LOG("[error]解析IO黑名单blacklist.io.json失败," + error);
			return false;
		}

	}
	return true;
}

bool saveIoBlackList()
{
	json ioJson = json::array();
	for (auto itm : ioBlackList)
	{
		json apiObj;
		apiObj["ip"] = itm.first;
		apiObj["desc"] = itm.second;
		ioJson.push_back(apiObj);
	}

	if (ioJson.size())
	{
		string str = ioJson.dump(4);
		if (!fs::writeFile(fs::appPath() + "/blacklist.io.json", str))
		{
			return false;
		}
	}
	return true;
}

bool addBlackList(string type, json paramVal)
{
	string ip = paramVal["ip"].get<string>();
	string desc = paramVal["desc"].get<string>();

	if (type == "apiServer") {
		std::shared_lock<shared_mutex> lock(g_csApiBlackList);
		apiBlackList[ip] = desc;
		saveApiBlackList();
		g_bl_timeStamp = time(NULL);
	}
	else if (type == "ioServer") {
		ioBlackList[ip] = desc;
		saveIoBlackList();
	}
	return true;
}

bool delBlackList(string type, string ip)
{

	if (type == "apiServer") {
		std::shared_lock<shared_mutex> lock(g_csApiBlackList);
		apiBlackList.erase(ip);
		saveApiBlackList();
		g_bl_timeStamp = time(NULL);
	}
	else if (type == "ioServer") {
		ioBlackList.erase(ip);
		saveIoBlackList();
	}	
	return true;
}

bool setBlackList(string type, json listVal)
{
	if (type == "apiServer") {
		std::shared_lock<shared_mutex> lock(g_csApiBlackList);
		apiBlackList.clear();
		for (auto it : listVal)
		{
			apiBlackList[it["ip"].get<string>()] = it["desc"].get<string>();
		}
		saveApiBlackList();
		g_bl_timeStamp = time(NULL);
	}
	else if (type == "ioServer") {
		ioBlackList.clear();
		for (auto it : listVal)
		{
			ioBlackList[it["ip"].get<string>()] = it["desc"].get<string>();
		}

		saveIoBlackList();
	}

	return true;
}

bool getBlackList(string type, json& listVal)
{
	if (type == "apiServer") {
		std::shared_lock<shared_mutex> lock(g_csApiBlackList);
		for (auto itm : apiBlackList)
		{
			json apiObj;
			apiObj["ip"] = itm.first;
			apiObj["desc"] = itm.second;
			listVal.push_back(apiObj);
		}
	}
	else if (type == "ioServer") {
		for (auto itm : ioBlackList)
		{
			json ioObj;
			ioObj["ip"] = itm.first;
			ioObj["desc"] = itm.second;
			listVal.push_back(ioObj);
		}
	}
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
		else if (p && p->m_devType == DEV_TYPE_local_serial)
		{
			tdsSession->setActivityCheck(false);
			tdsSession->type = TDS_SESSION_TYPE::bridgeToLocalCom;
			tdsSession->bridgedLocalCom = ioAddr;
			p->pSessionClientBridge = tdsSession;
		}
		else
		{
			//closesocket(tdsSession->sock);
			shutdown(tdsSession->sock, SHUT_DOWN_BOTH);
			return;
		}
	}
	else if (strData.find("/hmr") != string::npos) {
		//string hmrPath = str::trimPrefix(strData, "/hmr");
		//tdsSession->webHMRPath = hmrPath;
		//tdsSession->type = TDS_SESSION_TYPE::webHMR;
		//hmrServer.m_mapSessions[hmrPath] = tdsSession;
		//tdsSession->setActivityCheck(false);
	}
	else if (strData.find("/COM") != string::npos)
	{
		size_t pos = strData.find("COM");
		size_t pos1 = strData.find(" ", pos);
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
			//closesocket(tdsSession->sock);
			shutdown(tdsSession->sock, SHUT_DOWN_BOTH);
			return;
		}
	}
	else if (strData.find("tcpclient") != string::npos)
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
			//closesocket(tdsSession->sock);
			shutdown(tdsSession->sock, SHUT_DOWN_BOTH);
			return;
		}
	}
	else if (strData.find("tcpserver") != string::npos)
	{
		size_t pos = strData.find("tcpserver");
		size_t pos1 = strData.find("port=");
		size_t pos2 = strData.find("#", pos1) != -1 ? strData.find("#", pos1) : strData.size();
		string sport = (pos1 != -1) ? strData.substr(pos1 + 5, pos2 - pos1 - 5) : "9090";
		int port = atoi(sport.c_str());
		tdsSession->pBridgedTcpServer = new tcpSrv();
		tdsSession->type = TDS_SESSION_TYPE::bridgeToTcpServer;
		tdsSession->setActivityCheck(false);
		if (tdsSession->pBridgedTcpServer->run(&tdsSession->bridgedTcpSrvHandler, port))
		{
			LOG("bridge websocket to tcp server ,port %s success", sport.c_str());
		}
		else
		{
			LOG("bridge websocket to tcp server ,port %s fail", sport.c_str());
			delete tdsSession->pBridgedTcpServer;
			tdsSession->pBridgedTcpServer = NULL;
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
	else if (strData.find("/iopkt") != string::npos) //在debugio中使用
	{
		tdsSession->type = TDS_SESSION_TYPE::iopkt;
		ioPktMonitorClient.push_back(tdsSession);
		tdsSession->setActivityCheck(false);
	}
	else if (strData.find("/apipkt") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::apipkt;
		rpcPktMonitorClient.push_back(tdsSession);
		tdsSession->setActivityCheck(false);
	}
	else if (strData.find("stream") != string::npos)
	{
		tdsSession->type = TDS_SESSION_TYPE::dataStream;
		string tag = str::trimPrefix(strData,"/stream/"); 
		 tag = str::trimSuffix(tag, ".de"); 
		tag = str::url_decode(tag);
		ioChannel* pChan = ioSrv.getChanByTag(tag);
		//本地端
		if (pChan) {
			pChan->m_vecStreamPuller.push_back(tdsSession);
		}
		//云端
		else {
			MP* pmp = prj.GetMPByTag(tag,"zh");
			if (pmp) {
				//注册websocket拉流客户端
				//从io会话中找到推流会话，找到则加入拉流端
				std::shared_ptr<TDS_SESSION> p = ioSrv.getStreamPusher(tag);
				size_t pullerCount = 0;
				if (p != nullptr) {
					p->m_csPuller.lock();
					p->m_vecPuller.push_back(tdsSession);
					pullerCount = p->m_vecPuller.size();
					p->m_csPuller.unlock();
				}
				else {
					//没找到则在pmp中暂存,当推流端上线时，拷贝到推流会话。如果一直没有推流端上线，考虑增加一个超时主动断开机制。后续考虑
					pmp->m_csPuller.lock();
					pmp->m_vecPuller.push_back(tdsSession);
					pullerCount = pmp->m_vecPuller.size();
					pmp->m_csPuller.unlock();
				}


				LOG("[数据流   ]websocket拉流客户端连接成功,位号:%s,拉流客户端数目:%d", tag.c_str(), pullerCount);


				//启动下级服务器推流
				ioDev* pChlidTdsDev = ioSrv.getOwnerChildTdsDev(tag);
				bool pusherStarted = false;
				if (pChlidTdsDev) {
					string childTdsTag = pChlidTdsDev->m_strTagBind;
					string tag = pmp->getTag();

					string srcTag = TAG::trimRoot(tag, childTdsTag);
					json params;
					params["srcTag"] = srcTag;
					params["destTag"] = tag;
					params["port"] = tds->conf->tdspPort;
					params["socketType"] = "tcp";

					json childRlt, childErr;
					pChlidTdsDev->call("startPushStream", params, nullptr, childRlt, childErr, true);
					if (childRlt != nullptr) {
						pusherStarted = true;
					}
					else if (childErr != nullptr) {
						LOG("[warn]启动下级服务推流失败，位号:%s,错误信息:%s",tag.c_str(), childErr.dump().c_str());
					}
				}
				else {
					LOG("[warn]启动下级服务推流失败，位号:%s,没有找到可以获得流的下级服务", tag.c_str());
				}
			}
			else {
				LOG("[数据流   ]开始拉流，位号不存在,位号:%s", tag.c_str());
			}
		}
	}
	else //连接根地址 默认为rpc连接
	{
		if (strData.find("tdsClient") != string::npos)
		{

		}

		map<string, string> mapParams;
		parseParamFromUrl(strData, mapParams);
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
			tdsSession->send((unsigned char*)s.data(), s.length());
		}
		if (tdsSession->type == "")
			tdsSession->type = TDS_SESSION_TYPE::tdsClient;
		tdsSession->iALProto = "tdsRPC";

		string szLog = "[websocket会话][开始] 类型:" + tdsSession->type + ",地址:" + tdsSession->remoteIP + ":" + str::fromInt(tdsSession->remotePort);
		LOG(szLog);
	}
}

void WebServer::parseParamFromUrl(string& url, map<string, string>& mapParams)
{
	size_t paramStart = url.find('?', 0);
	if (paramStart != string::npos)//解析携带参数
	{
		size_t paramEnd = url.find(' ', paramStart);
		string paramStr = url.substr(paramStart + 1, paramEnd - paramStart - 1);
		parseParamFromQuery(paramStr, mapParams);
	}
}

void WebServer::parseParamFromQuery(string& query, map<string, string>& mapParams)
{
	vector<string> params;
	str::split(params, query, "&");

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

json WebServer::parseParamFromQuery(string& query)
{
	vector<string> params;
	str::split(params, query, "&");
	json j;
	for (int i = 0; i < params.size(); i++)
	{
		string oneP = params[i];
		vector<string> pkv;
		str::split(pkv, oneP, "=");
		if (pkv.size() == 2)
		{
			j[pkv[0]] = pkv[1];
		}
	}
	return j;
}


//应用层数据桥接
bool WebServer::handleAppLayerData_Bridge(unsigned char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	bool bHandled = true;
	if (tdsSession->type == TDS_SESSION_TYPE::bridgeToLocalCom)
	{
		ioDev* p = ioSrv.getIODev(tdsSession->bridgedLocalCom);
		if (p && p->m_devType == DEV_TYPE_local_serial)
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
		string s = str::fromBuff((char*)pData, iLen);
		LOG("[IO设备透传]client->dev " + s);
	}
	else if (tdsSession->type == TDS_SESSION_TYPE::bridgeToTcpServer)
	{
		if (tdsSession->pBridgedTcpServer)
			tdsSession->pBridgedTcpServer->SendData((char*)pData, iLen);
	}
	else
	{
		bHandled = false;
	}
	return bHandled;
}