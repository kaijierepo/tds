#ifdef WEB_SERVER2
#include "pch.h"
#include "webSrv2.h"
#include "httplib.h"
#include "users/userMng.h"
#include "logger.h"
#include "sha1.hpp"
#include "rpcHandler.h"
#include "tools/hmrSrv.h"

WebServer2::WebServer2()
{
}

WebServer2::~WebServer2()
{
}

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

void handleGet_tdsRelease(const httplib::Request& req, httplib::Response& res)
{
	res.status = 301;
	string localPath = fs::appPath() + "/files/release";
	vector<string> fl;
	fs::getFileList(fl, localPath);
	string host = req.get_header_value("Host");
	string redirectPath = "http://" + host + "/files/release/";

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
		redirectPath += "tds.zip";
		res.set_header("location", redirectPath);
		res.set_header("Cache-Control", "max-age=1");
	}
}

//自动找到 files/apk 文件夹下面最新的apk文件并下载
void handleGet_apk(const httplib::Request& req, httplib::Response& res)
{
	res.status = 301;
	string localPath = fs::appPath() + "/files/apk";
	vector<string> fl;
	fs::getFileList(fl, localPath);
	string host = req.get_header_value("Host");
	string redirectPath = "http://" + host + "/files/apk/";

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
	string timestamp = req.get_param_value("timestamp");
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

	nsSHA1::SHA1 checksum;
	checksum.update(s);
	string hash = checksum.final();
	LOG("signature calc  " + hash + "\n");

	res.set_content(echostr, "text/plain;charset=UTF-8");
}

void handlePost_gzh(const httplib::Request& req, httplib::Response& res)
{
	LOG("[微信公众号] Post请求\n" + req.body);

	string respBody = tds->gzhServer->getReply(req.body);

	LOG("[微信公众号] Post回复\n" + respBody);

	string resp = respBody;
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
	rpcSrv.handleRpcCall(strRpc, resp, binResp, iBinRespLen, bNeedLog, pSession);

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


void handleAfterFileRead(const Request& req, Response& resp)
{
	if (!tds->conf->debugMode) return;

	//调试模式不缓存任何数据
	resp.set_header("cache-control", "max-age=0");


	//html插入热更新代码
	bool isHtml = false;

	if (req.path.find("html") != string::npos)isHtml = true;
	if (req.path[req.path.length() - 1] == '/') isHtml = true;

	if (!isHtml)return;

	string s = hmrCodeStr + "</body>";

	resp.body = str::replace(resp.body, "</body>", s);
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

	//微信公众号消息处理
	svr.Get("\\/gzh.*", handleGet_gzh);
	svr.Post("\\/gzh.*", handlePost_gzh);

	//rpc Post命令处理
	svr.Post("\\/.*", handleRpcOverHttp);
	svr.Get("\\/rpc.*", handleRpcOverHttp);

	//最新版本的apk下载
	svr.Get("\\/apk", handleGet_apk);

	//最新版本的tds发布包
	svr.Get("\\/release", handleGet_tdsRelease);

	//有权限控制的文件下载服务
	svr.set_pre_routing_handler(handleFilePermission);

	//插入HMR代码
	svr.set_file_request_handler(handleAfterFileRead);

	//数据库文件上传Post命令处理
	svr.Post("\\/db.*",
		[&](const Request& req, Response& res, const ContentReader& content_reader) {
			string pathReq = charCodec::ansi2Utf8(req.path);
			pathReq = pathReq.substr(3, pathReq.length() - 3);
			string dbPath = db.m_path + pathReq;
			dbPath = str::replace(dbPath, "\\", "/");
			if (fs::fileExist(dbPath))
			{
				if (!fs::deleteFile(dbPath))return;
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
					[&](const MultipartFormData& file) {
						files.push_back(file);
						return true;
					},
					[&](const char* data, size_t data_length) {
						files.back().content.append(data, data_length);
						return true;
					});
			}
			else {
				std::string body;
				content_reader([&](const char* data, size_t data_length)
					{
						fwrite(data, 1, data_length, fp);
						return true;
					});
				res.set_content(body, "text/plain");
			}

			if (fp)
				fclose(fp);
		});
}
#endif