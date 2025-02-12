#include "tcpSrv.h"

namespace tcpServer {
	SYSTEMTIME str2time(const std::string& s) {
		SYSTEMTIME t; t.wMilliseconds = 0;
		sscanf(s.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
			&t.wYear,
			&t.wMonth,
			&t.wDay,
			&t.wHour,
			&t.wMinute,
			&t.wSecond);
		return t;
	}

	time_t time2unixstamp(SYSTEMTIME t)
	{
		tm temptm = { t.wSecond, t.wMinute, t.wHour,
			t.wDay, t.wMonth - 1, t.wYear - 1900, t.wDayOfWeek, 0, 0 };
		time_t iReturn = mktime(&temptm);
		return iReturn;
	}

	int calcTimePassSecond(string sTime) {
		time_t now = time(nullptr);
		SYSTEMTIME tlast = str2time(sTime);
		time_t last = time2unixstamp(tlast);
		return now - last;
	}

	string getNowStr() {
		SYSTEMTIME t;
		GetLocalTime(&t);
		char buff[50] = { 0 };
		sprintf(buff, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3d",
			t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		return buff;
	}
}

fp_statisSend g_fp_tcpSrv_statisSend = nullptr;

#define SHUT_DOWN_BOTH 2 //SD_BOTH in win,SHUT_RDWR in linux

static void cb(struct mg_connection* c, int ev, void* ev_data) {
	tcpSrv* pSrv = (tcpSrv*) c->mgr->userdata;
	if (ev == MG_EV_READ) {
		if (ev_data) {
			tcpSession* ptcp = (tcpSession*)c->fn_data;
			ptcp->iRecvCount += c->recv.len;
			pSrv->m_pCallBackUser->onRecvData_tcpSrv(c->recv.buf, c->recv.len, ptcp);
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
		char buff[50] = { 0 };
		sprintf(buff, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		pts->remoteIP = buff;
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

bool tcpSrv::run(ICallback_tcpSrv* pUser, int port, string strLocalIP /*= ""*/)
{
	m_strServerIP = strLocalIP;
	m_iServerPort = port;
	m_pCallBackUser = pUser;

	mg_mgr_init(&mgr);  // Init manager
	char sz[50] = { 0 };
	sprintf(sz, "tcp://0.0.0.0:%d", port);
	string url = sz;
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
			//tcpServer use mongoose  poll mode. int poll mode ,closesocket will not trigger event,use shutdown instead
			shutdown(i.second->sock,SHUT_DOWN_BOTH);
			i.second->sock = 0;
		}
	}
	else
	{
		for (auto& i : m_mapTcpSessions)
		{
			char sz[50] = { 0 };
			sprintf(sz, "%s:%d", i.second->remoteIP.c_str(), i.second->remotePort);
			string tmp = sz;
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
	//socket is blocking socket ,when socket is full in send buffer,this function will block
	//could be block when send big size data
	if (iLen == 0) 
		return false;
	if (sock == 0)
		return false;

	//call mg_send, must be in mongoose call, then will send immediatly
	//if call mg_send in other threads,poll will not return,untill poll timeout,then data is send
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
			strError = "timeout";//when client do not recv data or recv too slow
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
	for (; iter != m_mapTcpSessions.end(); ++iter)
	{
		if (iter->second->remoteIP == remoteIP)
		{
			bRet = iter->second->send(pData,iLen);
		}
	}
	return bRet;
}


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
