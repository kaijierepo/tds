#include "pch.h"
#include "rproxy.h"
#include "logger.h"
#include "common.hpp"

RProxy* rpProxy = nullptr;

void handleHttpByRProxy(const httplib::Request& req, httplib::Response& res)
{
	for (auto& [key, val] : rpProxy->m_jConf.items())
	{
		if (req.path.find(key) == 0 || req.path.find(key) == 1)
		{	
			string ipport = val.get<string>();
			httplib::Client cli(ipport);
			string path = req.path;
			path = str::trimPrefix(path, key);
			if (path == "")
				path = "/";
			if (auto realSrvRes = cli.Get(path.c_str())) {
				if (realSrvRes->status == 200) {
					std::cout << realSrvRes->body << std::endl;
					res = realSrvRes.value();
				}
			}
			else {
				auto err = realSrvRes.error();
			}
			return;
		}
	}
	
}


void initRpHttpSrv(httplib::Server& svr)
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

	//rpc Post命令处理
	svr.Get("\\/.*", handleHttpByRProxy);
}


void rphttpSrvThread(int port)
{
	//http相关接口需要使用gb2312.因为里面调用了多字节windows api，为支持中文，此处将utf8转为gb2312
	httplib::Server httpSrv;
	initRpHttpSrv(httpSrv);

	httpSrv.set_file_extension_and_mimetype_mapping("json", "text/json");
	httpSrv.set_file_extension_and_mimetype_mapping("html", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("htm", "text/html");
	httpSrv.set_file_extension_and_mimetype_mapping("css", "text/css");
	httpSrv.set_file_extension_and_mimetype_mapping("apk", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("rar", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("doc", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("docx", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("md", "application/octet-stream");
	httpSrv.set_file_extension_and_mimetype_mapping("zip", "application/x-zip-compressed");
	httpSrv.set_file_extension_and_mimetype_mapping("txt", "text/plain");

	httpSrv.listen("0.0.0.0", port);
}


RProxy::RProxy()
{
}

void RProxy::run()
{
	thread t(rphttpSrvThread, 667);
	t.detach();

	string s;
	if (fs::readFile(fs::appPath() + "/rphttp.json", s))
	{
		m_jConf = json::parse(s);
	}
}
