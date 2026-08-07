/*
  TDS for iot version 1.0.0
  https://gitee.com/liangtuSoft/tds.git

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2020-present Tao Lu 卢涛

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "pch.h"
#include "tdsConf.h"
#include "logger.h"
#include "tds_imp.h"
#include "tdsWatchDog.h"
#include "database/tDatabase.h"
#include "statusServer.h"
#include "base64.h"
#include "prj.h"
#include "common.h"
#include "rpcHandler.h"


#include "scriptEngine.h"
#include "./func_module/dumpCatch.h"
#include "mongoose.h"

#ifdef _WIN32
#include <shellapi.h>  //for this:  SHELLEXECUTEINFO
#endif

#ifdef _WIN32
bool CmdExecParam(string strParam, uint32_t dwMilliseconds = 0, int nShow = SW_SHOW, const char* lpDirectory = NULL);
#endif

struct mg_http_data {
	std::string head;
	std::string body;
	bool done = false;
	int status = 0;

	void reset() {
		head.clear();
		body.clear();
		done = false;
		status = 0;
	}
};

static void mg_connect_fn(struct mg_connection* connect, int ev, void* ev_data) {
	mg_http_data* data = (mg_http_data*)connect->fn_data;
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message* hm = (struct mg_http_message*)ev_data;

		data->head.assign(hm->head.ptr, hm->head.len);
		data->body.assign(hm->body.ptr, hm->body.len);
		data->status = mg_http_status(hm);

		data->done = true;
		connect->is_closing = 1;
	}
	else if (ev == MG_EV_ERROR) {
		data->done = true;
		connect->is_closing = 1;
	}
}

static std::string mg_get_url_encode(const std::string& str) {
	const char* in = str.c_str();
	size_t inLen = strlen(in);
	size_t outLen = 3 * inLen + 1;
	char* out = new char[outLen];

	size_t resultLen = mg_url_encode(in, inLen, out, outLen);
	std::string result(out, resultLen);

	delete[] out;
	return result;
}

void clearZlmNoReaderPusher() {
	string mediaSrvIP = tds->conf->mediaSrvIP;
	if (mediaSrvIP == "") {
		return;
	}

	string sPort = tds->conf->getStr("httpMediaPort", "669");
	string streamServerUrl = "http://" + mediaSrvIP + ":" + sPort;
	string uri = "/index/api/getMediaList";

	string ip = mediaSrvIP;
	string port = sPort;
	string path = uri + "?secret=" + mg_get_url_encode("Tds-666666");
	string url = "http://" + ip + ":" + port + path;

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	if (connect) {
		mg_printf(connect,
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			path.c_str(), ip.c_str()
		);

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);

	if (data.status == 200) {
		json j = json::parse(data.body);

		if (j["code"] != nullptr && j["code"].get<int>() == 0) {
			json jMediaList = j["data"];

			for (int i = 0; i < jMediaList.size(); i++) {
				json jM = jMediaList[i];
				if (jM["totalReaderCount"].is_number() && jM["totalReaderCount"].get<int>() == 0 && jM["originTypeStr"] == "rtsp_push") {
					string tag = jM["stream"];
					MP* pmp = prj.GetMPByTag(tag, "zh");

					if (pmp) {
						if (pmp->m_srcStreamFetch == "ondemand") {
							std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());

							RPC_SESSION rpcSess;
							rpcSess.remoteAddr = "127.0.0.1";
							rpcSess.remotePort = 0;

							pSession->setRpcSession(&rpcSess);

							json jReq;
							jReq["method"] = "closeStream";

							json jParams;
							jParams["tag"] = tag;

							jReq["id"] = timeopt::nowStr();
							jReq["params"] = jParams;

							string sReq = jReq.dump();
							rpcSrv.handleRpcCallAsyn(sReq, pSession, false);

							LOG("[流媒体  ]位号:%s 无人观看，取流模式为:按需取流,关闭推流", tag.c_str());
						}
						else {
							LOG("[流媒体  ]位号:%s 无人观看，取流模式为:持续取流,保持媒体源连接", tag.c_str());
						}
					}
					else {
						LOG("[流媒体  ]位号:%s 无人观看，没有找到对应的监控点", tag.c_str());
					}
				}
			}
		}
	}
	else {
		LOG("[error]zlm stream server 未响应,url=%s,path=%s", streamServerUrl.c_str(), uri.c_str());
	}
}

void updateEzvizAccessInfo() {
#ifdef ENABLE_OPENSSL
	try {
		for (auto& i : prj.m_mapEzvizAccess) {
			EZVIZ_ACCESS_INFO& info = i.second;

			//半天更新一次
			if (timeopt::CalcTimePassSecond(info.lastUpdate) < 12 * 60 * 60) {
				continue;
			}

			string addr = "https://open.ys7.com";
			httplib::Client cli(addr);
			cli.enable_server_certificate_verification(false);

			//更新token。 token默认过期时间7天
			httplib::Params params = {
				{"appKey",info.appKey},
				{"appSecret",info.secret}
			};

			auto resp = cli.Post("/api/lapp/token/get", params);
			if (resp != nullptr) {
				json jResp = json::parse(resp->body);
				json jData = jResp["data"];
				if (jData != nullptr) {
					json jAccessToken = jData["accessToken"];
					if (jAccessToken != nullptr) {
						info.token = jAccessToken.get<string>();
					}
				}
			}

			//获取新的flvurl。 url默认过期时间 1天
			params = {
				{"accessToken",info.token},
				{"deviceSerial",info.serialNo},
				{"protocol","4"} //flv协议
			};

			resp = cli.Post("/api/lapp/v2/live/address/get", params);
			if (resp != nullptr) {
				json jResp = json::parse(resp->body);
				json jData = jResp["data"];
				if (jData != nullptr) {
					json jUrl = jData["url"];
					if (jUrl != nullptr) {
						info.flvUrl = jUrl.get<string>();
					}
				}
			}


			params = {
				{"accessToken",info.token},
				{"deviceSerial",info.serialNo},
				{"protocol","1"} //ezopen协议
			};

			resp = cli.Post("/api/lapp/v2/live/address/get", params);
			if (resp != nullptr) {
				json jResp = json::parse(resp->body);
				json jData = jResp["data"];
				if (jData != nullptr) {
					json jUrl = jData["url"];
					if (jUrl != nullptr) {
						info.ezopenUrl = jUrl.get<string>();
						info.lastUpdate = timeopt::now();

						LOG("[萤石云] 接入信息更新tag=%s,serialNo=%s,token=%s,ezopenUrl=%s", info.tag.c_str(), info.serialNo.c_str(), info.token.c_str(), info.ezopenUrl.c_str());
					}
				}
				else {
					LOG("[warn][萤石云] 获取接入信息错误 tag=%s,serialNo=%s,token=%s,response=%s", info.tag.c_str(), info.serialNo.c_str(), info.token.c_str(), resp->body.c_str());
				}
			}
			else {
				auto err = resp.error();
				LOG("[warn][萤石云] 请求错误:%s", httplib::to_string(err).c_str());
			}
		}
	}
	catch (exception& e) {
		LOG("[warn] updateEzvizAccessInfo  exception:%s", e.what());
	}
#endif
}

/*
notes:
all string data in memory is utf8 format 

design problem:
> mutithread accessing element in a dynamic list
  1.shared points

代码不安全，未来需优化的地方，全局搜索 [unsafe]
需要改进的问题  全局搜索[问题]
*/

/*
TDS是一个数据服务
数据由数据的生产者ioDev(IO设备)提供给TDS,tdsClt(tds客户端)作为数据的使用者
ioDev虽然一般以tcpClient的方式连接到tds. 但相对于tds来说,设备被看作是服务端

*/


//exe模式下，都会有命令行窗口，通过设置 ui = chrome 或者 miniblink打开 浏览器窗口
//dll模式下，默认没有命名行窗口，通过设置 ui = console 来打开命令行窗口

//命令行参数调用 ，js解释器等模式需要默认控制台，因此默认控制台不隐藏
//terminal模式等需要隐藏，使用其他方式隐藏
//#pragma comment( linker, "/subsystem:windows /entry:mainCRTStartup" )//不显示默认控制台

void runScript(string path) {
	string url = "http://127.0.0.1:667/rpc";

	struct mg_mgr mgr;
	mg_mgr_init(&mgr);

	mg_http_data data;
	struct mg_connection* connect = mg_http_connect(&mgr, url.c_str(), mg_connect_fn, &data);

	string req = str::format(R"({"method":"runScript","params":{"filePath":"%s"}})", path.c_str());

	//logger.logInternal(req);

	if (connect) {
		mg_printf(connect,
			"POST /rpc HTTP/1.1\r\n"
			"Host: 127.0.0.1\r\n"
			"Content-Length: %d\r\n"
			"\r\n"
			"%s", req.size(),req.c_str());

		TIME tStart = timeopt::now();
		while (!data.done && timeopt::calcTimePassMilliSecond(tStart) / 1000.0 < 10.0) {
			mg_mgr_poll(&mgr, 100);
		}
	}

	mg_mgr_free(&mgr);


	//获取摘要盘问
	if (data.done) {
		yyjson_doc* doc = yyjson_read((char*)data.body.c_str(), data.body.size(), 0);
		yyjson_val* root = yyjson_doc_get_root(doc);
		if (root) {
			yyjson_val* yy_result = yyjson_obj_get(root, "result");
			if (yy_result && yyjson_is_arr(yy_result)) {
				size_t arr_size = yyjson_arr_size(yy_result);
				for (size_t i = 0; i < arr_size; ++i) {
					yyjson_val* elem = yyjson_arr_get(yy_result, i);
					if (elem && yyjson_is_str(elem)) {
						string s = yyjson_get_str(elem);
						logger.logInternal(s, false);
					}
				}
			}
		}
	}
}

bool isTdsRunning();
int main(int argc, char** argv)
{
	//取出命令行命令
	vector<string> args;
	for (int i = 0; i < argc; i++) {
		string s = argv[i];
		args.push_back(s);
	}

	if (args.size() == 3 && args[1] == "-s") {
		string sPath = args[2];
		sPath = str::replace(sPath, "\\", "/");
		if (charCodec::isValidGB2312(sPath)) {
			sPath = charCodec::gb_to_utf8(sPath);
		}
		else {
		}
		logger.logInternal("运行脚本:" + sPath);
		runScript(sPath);
		return 0;
	}

#ifdef _WIN32
	g_fp_tcpSrv_statisSend = statisSend;
#endif


	setThreadName("main thread");
	
	//设置当前路径为程序运行目录 tdsConf.loadConf();中的相对路径解析会用到当前路径
	//tdsImp.setWorkingDir();

	//run tds
	tds->run();
	prj.startZlmPoll();
	TIME zlmLastClearPusherTime = timeopt::now();
	vector<MP*> ezvizMp = prj.getAllEzvizMp();

	if (ezvizMp.size() > 0) {
		LOG("共有%d个萤石云视频监控点", ezvizMp.size());
	}

	time_t tPrevious; time(&tPrevious); //用于内存清理
	while (1)
	{
		timeopt::sleepMilli(1000);
		if (prj.m_enableEzviz) {
			ezvizMp = prj.getAllEzvizMp();

			for (int i = 0; i < ezvizMp.size(); i++) {
				MP* pmp = ezvizMp[i];
				if (prj.m_mapEzvizAccess.find(pmp->m_serialNo) == prj.m_mapEzvizAccess.end()) {
					EZVIZ_ACCESS_INFO  info;
					info.tag = pmp->getTag();
					info.serialNo = pmp->m_serialNo;
					info.appKey = pmp->m_appKey;
					info.secret = pmp->m_secret;
					info.token = "";
					timeopt::setAsTimeOrg(info.lastUpdate);
					prj.m_mapEzvizAccess[info.serialNo] = info;
				}
			}

			updateEzvizAccessInfo();
		}

		rpcSrv.cleanRpcSession();

		if (timeopt::CalcTimePassSecond(zlmLastClearPusherTime) > 30) {
			clearZlmNoReaderPusher();
			zlmLastClearPusherTime = timeopt::now();
		}

		//clean memory
		#ifdef _WIN32
		string timeParam = tds->conf->strCleanMemoryInterval;

		if (timeParam.size() >= 2 && timeParam.at(timeParam.length() - 1) == 'h') {
			if (fs::fileExist(fs::appPath() + "/RAMMap.exe")) {
				int secs = atoi(timeParam.substr(0, timeParam.length() - 1).c_str()) * 3600;

				time_t tNow;  time(&tNow);
				if (tNow - tPrevious > secs) {
					tPrevious = tNow;
					CmdExecParam(fs::appPath() + "/RAMMap.exe -Ew", 1, SW_HIDE);

					timeopt::sleepMilli(5000);
					CmdExecParam(fs::appPath() + "/RAMMap.exe -Et", 1, SW_HIDE);
				}
			}
		}
		#endif

	}

	return 0;
}


#ifdef _WIN32
bool CmdExecParam(string strParam, uint32_t dwMilliseconds/* = 0*/, int nShow /*= SW_SHOW*/, const char* lpDirectory /*= NULL*/)
{

	string strExePath = "cmd.exe";
	string cmd = "/c " + strParam;
	SHELLEXECUTEINFO ShExecInfo = { 0 };
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = "open";
	ShExecInfo.lpFile = (LPCTSTR)strExePath.c_str();
	ShExecInfo.lpParameters = (LPCTSTR)cmd.c_str();
	ShExecInfo.lpDirectory = lpDirectory;
	ShExecInfo.nShow = nShow;
	ShExecInfo.hInstApp = NULL;
	if (ShellExecuteEx(&ShExecInfo))
	{
		HANDLE hProcess = ShExecInfo.hProcess;
		if (hProcess != 0)
		{
			WaitForSingleObject(hProcess, dwMilliseconds == 0 ? INFINITE : dwMilliseconds);
			CloseHandle(hProcess);
		}
		return true;
	}
	return false;
	}
#else
// TODO: linux
#endif

#ifdef TDSDLL
#define DllExport   extern "C" __declspec( dllexport )
DllExport i_tds* getTds() {
	return tds;
}
#endif
