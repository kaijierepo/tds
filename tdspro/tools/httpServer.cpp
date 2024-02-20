#include "pch.h"
#include "httpServer.h"
#include "logger.h"
#include "common.h"
#include "httplib.h"
#include "kvIni.h"

HttpServer::HttpServer()
{
	m_port = 6680;
}

void httpServer_thread(HttpServer* hs) {
	httplib::Server* httpSrv = new httplib::Server;
	string webPath = fs::appPath();
	if (fs::fileExist(webPath))
	{
		string asc_path = charCodec::utf8_to_gb(webPath);
		httpSrv->set_mount_point("/", +asc_path.c_str());
		LOG("[keyinfo][HTTP服务器] 根目录: " + webPath);
	}

	LOG("[keyinfo][HTTP服务器] 端口: " + str::fromInt(hs->m_port));

	httpSrv->listen("0.0.0.0", hs->m_port);
}

void HttpServer::run()
{
	string confPath = fs::appPath() + "/" + m_ProcName + ".ini";
	if (!fs::fileExist(confPath))
	{
		string s = defaultConf();
		fs::writeFile(confPath, s);
	}

	KV_INI tdsIni;
	tdsIni.load(confPath);

	m_port = tdsIni.getValInt("httpPort",6680);
	thread t(httpServer_thread, this);
	t.detach();
}

string HttpServer::defaultConf()
{
	string s = R"(#HTTP服务器配置
httpPort=6680            #http服务端口
)";

	s = str::replace(s, "\n", "\r\n");
	return s;
}
