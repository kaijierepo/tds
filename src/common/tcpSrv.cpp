#include "tcpSrv.h"
#include "common.h"


fp_statisSend g_fp_tcpSrv_statisSend = nullptr;

#define SHUT_DOWN_BOTH 2 //SD_BOTH in win,SHUT_RDWR in linux

static void cb(struct mg_connection* c, int ev, void* ev_data) {
	tcpSrv* pSrv = (tcpSrv*) c->mgr->userdata;
	if (ev == MG_EV_READ) {
		if (ev_data) {
			tcpSession* ptcp = (tcpSession*)c->fn_data;
			ptcp->iRecvCount += c->recv.len;
			pSrv->m_pCallBackUser->OnRecvData_TCPServer(c->recv.buf, c->recv.len, ptcp);
		}
		mg_iobuf_del(&c->recv, 0, c->recv.len);   // And discard it
	}
	else if (ev == MG_EV_CONNECT) {
		
	}
	else if (ev == MG_EV_CLOSE) { 
		tcpSession* pts = (tcpSession*)c->fn_data;
		pSrv->m_pCallBackUser->statusChange_tcpSrv(pts, false);
		pSrv->m_csClientVectorLock.lock();
		pSrv->m_mapTcpSessions.erase(pts);
		pSrv->m_csClientVectorLock.unlock();
	}
	else if (ev == MG_EV_ACCEPT) {
		tcpSession* pts = new tcpSession();
		pts->pTcpServer = pSrv;
		pts->pData1 = c;
		unsigned char* ip = (unsigned char*)&c->rem.ip;
		pts->remoteIP = str::format("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		pts->sock = (int) c->fd;  
		pts->remotePort = ntohs(c->rem.port); 
		pts->localIP = pSrv->m_strServerIP;
		pts->localPort = pSrv->m_iServerPort;
		pSrv->m_csClientVectorLock.lock();
		pSrv->m_mapTcpSessions[pts] = pts;
		pSrv->m_csClientVectorLock.unlock();
		c->fn_data = pts;
		pSrv->m_pCallBackUser->statusChange_tcpSrv(pts, true);
	}
	else if (ev == MG_EV_OPEN) {

	}
	else if (ev == MG_EV_WRITE) {

	}
}

void mongoose_tcp_listen_thread(int port, tcpSrv* pSrv) {
	for (;;) mg_mgr_poll(&pSrv->mgr, 1000);                 // Event loop
	mg_mgr_free(&pSrv->mgr);                                // Cleanup
}

bool tcpSrv::run(ITcpServerCallBack* pUser, int port, string strLocalIP /*= ""*/)
{
	m_strServerIP = strLocalIP;
	m_iServerPort = port;
	m_pCallBackUser = pUser;

	mg_mgr_init(&mgr);  // Init manager
	string url = "tcp://0.0.0.0:" + str::fromInt(port);
	mg_connection* c = mg_listen(&mgr, url.c_str() , cb, &mgr);  // Setup listener
	mgr.userdata = this;

	if(c){
		thread t(mongoose_tcp_listen_thread, port,this);
		t.detach();
		return true;
	}
	else{
		return false;
	}
}

void tcpSrv::stop()
{

}

void tcpSrv::disconnect(string remoteAddr)
{
	m_csClientVectorLock.lock();
	if (remoteAddr == "" || remoteAddr == "*")
	{
		for (auto& i : m_mapTcpSessions)
		{
			//目前tcpServer统一使用mongoose的 poll模型。poll模型closesocket不会触发响应,使用shutdown
			shutdown(i.second->sock,SHUT_DOWN_BOTH);
			i.second->sock = 0;
		}
	}
	else
	{
		for (auto& i : m_mapTcpSessions)
		{
			string tmp = str::format("%s:%d", i.second->remoteIP.c_str(), i.second->remotePort);
			if (tmp == remoteAddr)
			{
				shutdown(i.second->sock,SHUT_DOWN_BOTH);
				i.second->sock = 0;
			}
		}
	}
	m_csClientVectorLock.unlock();
}


bool tcpSession::send(char* pData, size_t iLen)
{
	//socket是阻塞式socket，当socket发送缓冲区满时，该函数会阻塞；在发送大数据时会出现阻塞状态
	if (iLen == 0) 
		return false;
	if (sock == 0)
		return false;

	//调用mg_send，必须在mongoose的回调中调用，才能立即发送
	//如果异步调用mg_send,poll函数不会返回，必须等到poll函数timeout才能发送出去
	//mg_connection* mgc = (mg_connection*)pData1;
	//int iRet = mg_send(mgc, pData, iLen);

	int iRet = ::send(sock, pData, iLen, 0);

	if (iRet <=0)
	{
		/*
		int iErr = GetLastError();
		string strError;
		if (iErr == WSAETIMEDOUT)
		{
			strError = "timeout";//客户端不接受数据或者接受处理缓慢可能导致此问题
		}
		else if (iErr == WSAENOTSOCK)
		{
			sock = 0;
		}
		else
		{
			shutdown(sock,SHUT_DOWN_BOTH); 
		}
		strError = sys::getLastError();
		string str = str::format("[tcpSrv][error]send data fail，error=%s,%s:%d", strError.c_str(),remoteIP.c_str(),remotePort);
		logger.logInternal(str);
		*/

		if (sock != 0)
		{
			shutdown(sock, SHUT_DOWN_BOTH);
			sock = 0;
		}
	}

	if (iRet > 0)
		iSendSucCount += iLen;
	else
		iSendFailCount += iLen;

	if (g_fp_tcpSrv_statisSend) {
		g_fp_tcpSrv_statisSend(((tcpSrv*)pTcpServer)->m_iServerPort, iLen);
	}

	return iRet > 0;
}

bool tcpSrv::SendData(char* pData, size_t iLen, string remoteIP)
{
	bool bRet = false;

	std::unique_lock<mutex> lock(m_csClientVectorLock);

	std::map<tcpSession*,tcpSession*>::iterator iter = m_mapTcpSessions.begin();
	//所有该ip的客户端都发送，一个ip有两个连接，发送给了僵尸连接却没有发送给正常连接
	for (; iter != m_mapTcpSessions.end(); ++iter)
	{
		if (iter->second->remoteIP == remoteIP)
		{
			bRet = iter->second->send(pData,iLen);
		}
	}

	return bRet;
}

//广播发送
bool tcpSrv::SendData(char* pData, size_t iLen)
{
	std::unique_lock<mutex> lock(m_csClientVectorLock);
	std::map<tcpSession*, tcpSession*>::iterator iter = m_mapTcpSessions.begin();
	for (; iter != m_mapTcpSessions.end(); ++iter)
	{
		iter->second->send(pData, iLen);
	}
	return true;
}


/*构造函数*/
void tcpSrv::Log(char* sz)
{
	if (pLog)
	{
		pLog(sz);
	}
}

tcpSrv::tcpSrv()
{
	keepAliveTimeout = 0;
	m_bStarted = false;
	pLog = NULL;
	m_bReuseAddr = true;
	m_pCallBackUser = nullptr;
	m_iServerPort = 0;
}

tcpSrv::~tcpSrv()
{

}
