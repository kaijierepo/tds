#include "tSockSrv.h"
#ifdef TDS
#include "logger.h"
#elif
void LOG(const char* pszFmt, ...){}
#endif

tSockSrv sockSrv;

tSockSrv::tSockSrv()
{
	m_pCallback = nullptr;
}

tSockSrv::~tSockSrv()
{
	stop();
}


void tSockSrv::statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<SOCK_SESSION> p = std::make_shared<SOCK_SESSION>(pTcpSess);
		m_mutexSessions.lock();
		m_sockSessions[pTcpSess->sock] = p;
		m_mutexSessions.unlock();
	}
	else
	{
		m_mutexSessions.lock();
		m_sockSessions.erase(pTcpSess->sock);
		m_mutexSessions.unlock();
	}
}

void deleteTcpClt(tcpClt* p) {
	delete p;
}

void tSockSrv::statusChange_tcpClt(tcpSessionClt* pTcpSess, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<SOCK_SESSION> p = std::make_shared<SOCK_SESSION>(pTcpSess);
		m_mutexSessions.lock();
		m_sockSessions[pTcpSess->sock] = p;
		m_mutexSessions.unlock();
		LOG("[SockSrv]Tcp Client connect to server:%s:%d", pTcpSess->remoteIP.c_str(), pTcpSess->remotePort);
			
		if (m_tcpClientRegPkt.length() > 0) {
			::send(pTcpSess->sock, m_tcpClientRegPkt.c_str(), m_tcpClientRegPkt.length(),0);
		}
	}
	else
	{
		LOG("[warn][SockSrv] tcpClient disconnect from server:%s:%d",pTcpSess->remoteIP.c_str(), pTcpSess->remotePort);

		m_mutexSessions.lock();
		m_sockSessions.erase(pTcpSess->sock);
		m_mutexSessions.unlock();

		m_csTcpClt_streamPusher.lock();
		if (m_tcpClt_streamPusher.find(pTcpSess->pTcpClt) != m_tcpClt_streamPusher.end()) {
			m_tcpClt_streamPusher.erase(pTcpSess->pTcpClt);
			thread t(deleteTcpClt, pTcpSess->pTcpClt);
			t.detach();
		}
		m_csTcpClt_streamPusher.unlock();
	}
}

void tSockSrv::onTcpCltEvent_error(tcpClt* pClt, string error)
{
	LOG("[warn][SockSrv]TcpClient,remoteAddr=%s:%d,ErrorInfo,%s",pClt->m_remoteIP.c_str(),pClt->m_remotePort,error.c_str());
}

bool tSockSrv::run(string masterTdsAddrs, string childTdsIP)
{
	if (masterTdsAddrs != "") {
		vector<string> vecAddrs;
		str::split(vecAddrs, masterTdsAddrs, ",");

		LOG("[SockSrv]Connect to master service %s,local addr:%s", masterTdsAddrs.c_str(), childTdsIP.c_str());

		for (int i = 0; i < vecAddrs.size(); i++) {
			string addr = vecAddrs[i];
			tcpClt* pTcpClt = new tcpClt();

			string ip;
			int port;
			str::parseIpPort(addr, ip, port);

			pTcpClt->m_keepAliveTimeout = tds->conf->tcpKeepAliveDS;
			pTcpClt->run(this, addr, childTdsIP);
			m_tcpClt_ParentTds[pTcpClt] = pTcpClt;
		}
	}

	int tcpPort = tds->conf->getInt("tcpPort", 670);
	if (tcpPort > 0) {
		LOG("[SockSrv] Tcp Port:%d", tcpPort);
		m_tcpSrv.run(this, tcpPort);
	}

	int udpPort = tds->conf->getInt("udpPort", 666);
	if (udpPort > 0) {
		LOG("[SockSrv] Udp Port:%d", udpPort);
		m_udpSrv.run(this, udpPort);
	}

	return false;
}

void tSockSrv::stop()
{
	LOG("[keyinfo]stopping sock server...");
}

void tSockSrv::OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pTcpSess)
{
	m_mutexSessions.lock();
	std::shared_ptr<SOCK_SESSION> sockSess = m_sockSessions[pTcpSess->sock];
	m_mutexSessions.unlock();
	OnRecvData_TCP((char*)pData, iLen, sockSess);
}

void tSockSrv::OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* pTcpSess)
{
	m_mutexSessions.lock();
	std::shared_ptr<SOCK_SESSION> sockSess = m_sockSessions[pTcpSess->sock];
	m_mutexSessions.unlock();
	OnRecvData_TCP((char*)pData, iLen, sockSess);
}

void tSockSrv::OnRecvUdpData(unsigned char* recvData, size_t recvDataLen, UDP_SESSION udpSession)
{
	//string req;
	//str::fromBuff(recvData, recvDataLen,req);
	//std::shared_ptr<TDS_SESSION> pSession(new TDS_SESSION());
	//RPC_SESSION rpcSess;
	//rpcSess.remoteAddr = udpSession.remoteIP;
	//rpcSess.remotePort = udpSession.remotePort;
	//pSession->setRpcSession(&rpcSess);
	//rpcSrv.handleRpcCallAsyn(req, pSession,false);
}

void tSockSrv::OnRecvData_TCP(char* pData, size_t iLen, std::shared_ptr<SOCK_SESSION> ss)
{
	stream2pkt& tlBuf = ss->recvBuff;
	tlBuf.PushStream((unsigned char*)pData, iLen);
	while (tlBuf.PopPkt(IsValidPkt_textEnd_LFLF, false) || tlBuf.PopPkt(IsValidPkt_textEnd_CRLFCRLF, false))
	{
		if(m_pCallback)
			m_pCallback((char*)tlBuf.pkt, tlBuf.iPktLen, ss);
	}
}

bool tSockSrv::sendToSockSession(std::shared_ptr<SOCK_SESSION>  sockSession, unsigned char* pData, size_t len) {
	if (sockSession->type == TCP_SOCK) {
		if (len <= 0)
			return false;
		if (sockSession->sock <= 0)
			return false;
		int ret = ::send(sockSession->sock, (char*)pData, len, 0);
		if (ret <= 0) {
			int SHUT_DOWN_BOTH = 2; //SD_BOTH in win,SHUT_RDWR in linux
			shutdown(sockSession->sock, SHUT_DOWN_BOTH);
			return false;
		}
		return true;
	}
	else if (sockSession->type == UDP_SOCK) {

	}
	else {
		return false;
	}
}

void tSockSrv::sendToAllSessions(unsigned char* pData, size_t len, bool specialNotify)
{
	m_mutexSessions.lock();
	for (auto& i : m_sockSessions) {
		sendToSockSession(i.second, pData, len);
	}
	m_mutexSessions.unlock();
}

void tSockSrv::sendToAllSessions(string& s, bool specialNotify)
{
	sendToAllSessions((unsigned char*)s.c_str(), s.length(), specialNotify);
}
