#pragma once
#include "httplib.h"
#include "json.hpp"
#include "tcpSrv.h"
#include "tdsSession.h"

using json = nlohmann::json;

struct HMR_SESSION {
	string webHMRPath;
	int sock;
};


//使用mongoose的websocket，减少一个端口占用
//暂时先不完全删除独立tcpServer的代码，是否某些情况下因为要调试http服务器，用独立的hmr的tcpServer更为合适，需要实践试用一段时间

//hmrServer保存需要hmr的页面的 websocket连接及其关联的目录
//当目录文件发生变化时，主动通知浏览器改变
//暂时没有接受浏览器请求指令的机制
//生产环境关闭该功能，因为文件改动监测需要耗费性能，在前端开发时使用
class HMRServer : public ITcpServerCallBack {
public:
	HMRServer();
	void watchFile_process(string dir_path);
	void run(const std::string dir_path);
	void websocketSend(string s, int sock);
	void statusChange_tcpSrv(tcpSession* pTcpSession, bool bIsConn) override;
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess) override;

	tcpSrv* m_httpHotUpdateSrv;
	json m_jConf;

	map<string, std::shared_ptr<TDS_SESSION>> m_mapSessions;
	mutex m_mutexSessions;
};

extern HMRServer hmrServer;
extern string hmrCodeStr;