#include "tcpSrv.h"
#include "tds.h"
#include <thread>

namespace tcpServer {
	TIME str2time(const std::string& s) {
		TIME t;
		//2022-02-22 11:11:11.123   23bytes
		if (s.length() == 23) {
			sscanf(s.c_str(), "%hu-%hu-%hu %hu:%hu:%hu.%hu",
				&t.wYear,
				&t.wMonth,
				&t.wDay,
				&t.wHour,
				&t.wMinute,
				&t.wSecond,
				&t.wMilliseconds);
		}
		//2022-02-22 11:11:11   19bytes
		else if (s.length() == 19) {
			t.wMilliseconds = 0;
			sscanf(s.c_str(), "%hu-%hu-%hu %hu:%hu:%hu",
				&t.wYear,
				&t.wMonth,
				&t.wDay,
				&t.wHour,
				&t.wMinute,
				&t.wSecond);
		}
		return t;
	}

	time_t time2unixstamp(TIME t)
	{
		tm temptm = { t.wSecond, t.wMinute, t.wHour,
			t.wDay, t.wMonth - 1, t.wYear - 1900, t.wDayOfWeek, 0, 0 };
		time_t iReturn = mktime(&temptm);
		return iReturn;
	}

	int calcTimePassSecond(string sTime) {
		time_t now = time(nullptr);
		TIME tlast;
		tlast.fromStr(sTime);
		time_t last = tlast.toUnixTime();
		return now - last;
	}

	string getNowStr() {
		TIME t;
		t.setNow();
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

			if (pSrv->m_pCallBackUser) {
				pSrv->m_pCallBackUser->onRecvData_tcpSrv(c->recv.buf, c->recv.len, ptcp);
			}
		}
		mg_iobuf_del(&c->recv, 0, c->recv.len);   // And discard it
	}
	else if (ev == MG_EV_CONNECT) {
		
	}
	else if (ev == MG_EV_CLOSE) { 
		tcpSession* pts = (tcpSession*)c->fn_data;
		if (pSrv->m_pCallBackUser) {
			pSrv->m_pCallBackUser->statusChange_tcpSrv(pts, false);			
		}

		pSrv->m_csClientVectorLock.lock();
		pSrv->m_mapTcpSessions.erase(pts);
		pSrv->m_csClientVectorLock.unlock();
		int lastErr = 0;
#ifdef _WIN32
		lastErr = WSAGetLastError();
#endif
		printf("MG_EV_CLOSE,lastErr=%d,remoteIp=%s,remotePort=%d\r\n",lastErr, pts->remoteIP.c_str(), pts->remotePort);
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

		if (pSrv->m_pCallBackUser) {
			pSrv->m_pCallBackUser->statusChange_tcpSrv(pts, true);
		}
	}
	else if (ev == MG_EV_OPEN) {

	}
	else if (ev == MG_EV_WRITE) {

	}
}

void mongoose_tcp_listen_thread(int port, tcpSrv* pSrv) {
	pSrv->m_bStarted = true;
	for (;;) {
		if (pSrv->m_stop) {
			pSrv->m_pCallBackUser = nullptr;
			break;
		}
		mg_mgr_poll(&pSrv->mgr, 1000);
	}// Event loop
	mg_mgr_free(&pSrv->mgr);                                // Cleanup
	pSrv->m_bStarted = false;
}

string g_cmdListScockIP;
string g_cmdListScockPort;

bool tcpSrv::run(ICallback_tcpSrv* pUser, int port, string strLocalIP /*= ""*/) {
	m_strServerIP = strLocalIP;
	m_iServerPort = port;
	m_pCallBackUser = pUser;

	mg_mgr_init(&mgr);  // Init manager

	char sz[50] = { 0 };
	if (!g_cmdListScockIP.empty() && !g_cmdListScockPort.empty()) {
		sprintf(sz, "tcp://%s:%s", g_cmdListScockIP, g_cmdListScockPort);
	}
	else {
		sprintf(sz, "tcp://0.0.0.0:%d", port);
	}

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
	m_stop = true;
	while (1) {
#ifdef _WIN32
		Sleep(1);
		if (!m_bStarted)
			break;
#endif
	}
	m_stop = false;
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
#ifdef _WIN32
		int errCode = WSAGetLastError();
		string str = str::format("[tcpSrv]send data fail，ret=%d,errorCode=%d,%s:%d",iRet, errCode,remoteIP.c_str(),remotePort);
		printf(str.c_str());
#endif
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
	m_stop = false;
}

tcpSrv::~tcpSrv()
{
	stop();
}
