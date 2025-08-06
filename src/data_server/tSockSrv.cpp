#include "tSockSrv.h"
#include <iostream>
#include <sstream>
#include <thread>

#ifdef TDS
#include "logger.h"
#else
namespace sockServer {
	void LOG(const char* pszFmt, ...) {};
}
using namespace sockServer;
#endif

tSockSrv sockSrv;

tSockSrv::tSockSrv()
{
	m_pCallback = nullptr;
	m_pStatusCallback = nullptr;
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

		if (m_conf.tcpServerRegPkt.length() > 0) {
			::send(pTcpSess->sock, m_conf.tcpServerRegPkt.c_str(), m_conf.tcpServerRegPkt.length(), 0);
		}

		if (m_pStatusCallback) {
			m_pStatusCallback(true, p);
		}
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
			
		if (m_conf.tcpClientRegPkt.length() > 0) {
			::send(pTcpSess->sock, m_conf.tcpClientRegPkt.c_str(), m_conf.tcpClientRegPkt.length(),0);
		}

		if (m_pStatusCallback) {
			m_pStatusCallback(true, p);
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

std::vector<std::string> splitStr(const std::string& str, char delimiter) {
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(str);

	while (std::getline(tokenStream, token, delimiter)) {
		tokens.push_back(token);
	}

	return tokens;
}

bool getIpPort(string s, string& ip, int& port)
{
	size_t ipos = s.find(":");
	if (ipos == string::npos)
		return false;
	string sip = s.substr(0, ipos);
	string sport = s.substr(ipos + 1, s.length() - ipos - 1);
	ip = sip;
	if (sport == "")
		return false;
	port = atoi(sport.c_str());
	return true;
}

bool tSockSrv::run(SOCK_SRV_CONF& conf)
{
	m_conf = conf;

	if (m_conf.masterTdsAddrs != "") {
		vector<string> vecAddrs = splitStr(m_conf.masterTdsAddrs, ',');

		LOG("[SockSrv]Connect to master service %s,local addr:%s", m_conf.masterTdsAddrs.c_str(), m_conf.childTdsIP.c_str());

		for (int i = 0; i < vecAddrs.size(); i++) {
			string addr = vecAddrs[i];
			tcpClt* pTcpClt = new tcpClt();

			string ip;
			int port;
			getIpPort(addr, ip, port);

			pTcpClt->m_keepAliveTimeout = m_conf.tcpKeepAliveSec;
			pTcpClt->run(this, addr, m_conf.childTdsIP);
			m_tcpClt_ParentTds[pTcpClt] = pTcpClt;
		}
	}

	string sMasterDsConf;
	//fs::readFile()

	int tcpPort = m_conf.tcpSrvPort;
	if (tcpPort > 0) {
		LOG("[SockSrv] Tcp Port:%d", tcpPort);
		m_tcpSrv.run(this, tcpPort);
	}

	int udpPort = m_conf.udpSrvPort;
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

void tSockSrv::onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pTcpSess)
{
	m_mutexSessions.lock();
	std::shared_ptr<SOCK_SESSION> sockSess = m_sockSessions[pTcpSess->sock];
	m_mutexSessions.unlock();
	OnRecvData_TCP((char*)pData, iLen, sockSess);
}

void tSockSrv::onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* pTcpSess)
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

bool tSockSrv::sendToSockSession(std::shared_ptr<SOCK_SESSION> sockSession, unsigned char* pData, size_t len) {
	if (sockSession->type == TCP_SOCK) {
		if (len <= 0) {
			return false;
		}

		if (sockSession->sock <= 0) {
			return false;
		}

		int ret = ::send(sockSession->sock, (char*)pData, len, 0);
		if (ret <= 0) {
			int ShutDownBoth = 2; //SD_BOTH in win,SHUT_RDWR in linux
			shutdown(sockSession->sock, ShutDownBoth);
			return false;
		}

		return true;
	}
	else if (sockSession->type == UDP_SOCK) {

	}
	else {
		return false;
	}

	return false;
}

void tSockSrv::sendToAllSessions(unsigned char* pData, size_t len, bool specialNotify) {
	m_mutexSessions.lock();
	for (auto& i : m_sockSessions) {
		sendToSockSession(i.second, pData, len);
	}
	m_mutexSessions.unlock();
}

void tSockSrv::sendToAllSessions(string& s, bool specialNotify) {
	sendToAllSessions((unsigned char*)s.c_str(), s.length(), specialNotify);
}
