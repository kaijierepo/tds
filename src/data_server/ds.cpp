#include "pch.h"
#include "ds.h"
#include "logger.h"
#include "prj.h"
#include "mp.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "tcpClt.h"
#include "tdsConf.h"
#include "tds.h"
#include "users/userMng.h"

dataServer ds;

dataServer::dataServer()
{
}

dataServer::~dataServer()
{
}

void dataServer::statusChange_tcpSrv(tcpSession* pTcpSession, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		GetLocalTime(&p->stCreateTime);
		p->bConnected = true;
		p->pTcpSession = pTcpSession;
		p->sock = pTcpSession->sock;
		p->port = pTcpSession->remotePort;
		p->ip = pTcpSession->remoteIP;


		pTcpSession->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();
	}
	else
	{
		if (pTcpSession->pALSession)
		{
			std::shared_ptr<TDS_SESSION> p = NULL;
			//从列表中删除
			m_mutexTdsSessionList.lock();
			for (int i = 0; i < m_vecTdsSession.size(); i++)
			{
				if (m_vecTdsSession.at(i)->pTcpSession == pTcpSession)
				{
					p = m_vecTdsSession[i];
					m_vecTdsSession.erase(m_vecTdsSession.begin() + i);
					break;
				}
			}
			m_mutexTdsSessionList.unlock();

			//更新该session状态。等待其他零散指针引用销毁后自动删除
			p->onTcpDisconnect();
		}
	}
}

void tdsEdgeRegisterThread(std::shared_ptr<TDS_SESSION> p)
{
	Sleep(1000);
	//向服务器发送注册包
	json j;
	j["method"] = "devRegister";
	j["ioAddr"] = tds->conf->deviceID;

	string s = j.dump() + "\n\n";
	p->send((char*)s.c_str(), s.length());
}

void dataServer::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
{
	if (bIsConn)
	{
		std::shared_ptr<TDS_SESSION> p(new TDS_SESSION());
		p->m_bActiveSession = true;
		p->bConnected = true;
		p->pTcpSessionClt = connInfo->tcpClt;
		p->sock = connInfo->sock;
		p->port = connInfo->srvPort;
		p->ip = connInfo->srvIP;
		connInfo->pALSession = p.get();
		m_mutexTdsSessionList.lock();
		m_vecTdsSession.push_back(p);
		m_mutexTdsSessionList.unlock();

		//作为tdsEdge连接上了服务器
		if (connInfo->tcpClt == m_tcpCltEdge)
		{
			LOG("[边缘网关]连接tds服务器成功");
			thread t(tdsEdgeRegisterThread,p);
			t.detach();
		}
	}
	else
	{
		if (connInfo->pALSession)
		{
			m_mutexTdsSessionList.lock();
			for (int i = 0; i < m_vecTdsSession.size(); i++)
			{
				if (m_vecTdsSession.at(i)->pTcpSessionClt == connInfo->tcpClt)
				{
					std::shared_ptr<TDS_SESSION> p = m_vecTdsSession[i]; \
						p->onTcpDisconnect();
					m_vecTdsSession.erase(m_vecTdsSession.begin() + i);
				}
			}
			m_mutexTdsSessionList.unlock();
		}
	}
}

int dataServer::SendAppLayerData(char* pData, int iLen, void* pAppLayerCltInfo)
{
	bool bRet = false;
	TDS_SESSION* pALC = (TDS_SESSION*)pAppLayerCltInfo;
	tcpSession* pCommLayerCltInfo = (pALC)->pTcpSession;
	return pCommLayerCltInfo->send((char*)pData, iLen);
}

int dataServer::Send(SOCKET sock, char* pBuffer, int iLength)
{
	return send(sock, pBuffer, iLength, 0);
}


bool dataServer::runAsEdge()
{
	//tdsEdge连接
	if (tds->conf->edge)
	{
		m_tcpCltEdge = new tcpClt();
		m_tcpCltEdge->run(this, tds->conf->cloudIP, tds->conf->cloudPort);
		LOG("[keyinfo][边缘网关模式] 云服务器地址:%s:%d", tds->conf->cloudIP.c_str(), tds->conf->cloudPort);
	}
	return false;
}

bool dataServer::run()
{
	m_parentTdsIP = tds->conf->getStr("parentTdsIP", "");
	m_parentTdsPort = tds->conf->getInt("parentTdsPort", 0);
	if (m_parentTdsIP != "" && m_parentTdsPort != 0) {

	}

	return false;
}

void dataServer::stop()
{
	LOG("[keyinfo]正在停止数据服务DataServer...");
	LOG("[keyinfo]数据服务已停止");
}



//此处加锁，连接断开现成可能会并发操作此列表
shared_ptr<TDS_SESSION> dataServer::getTDSSession(tcpSession* pTcpSess)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for(int i=0;i<m_vecTdsSession.size();i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		if(p->pTcpSession == pTcpSess)
		{
			return p;
		}
	}
	return nullptr;
}


shared_ptr<TDS_SESSION> dataServer::getTDSSession(string remoteIP,int remotePort)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for (int i = 0; i < m_vecTdsSession.size(); i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		std::unique_lock<recursive_mutex> lock(p->m_mutexTcpLink);
		if (p->isConnected())
		{
			if (p->m_bActiveSession)
			{
				//客户端模式remoteAddr 只有1个，但本地有可以有多个连接，因此使用本地端口+ip作为id
				if (p->pTcpSessionClt->m_strLocalIP == remoteIP && p->pTcpSessionClt->m_iLocalPort == remotePort)
				{
					return p;
				}
			}
			else
			{
				if (p->pTcpSession->remoteIP == remoteIP && p->pTcpSession->remotePort == remotePort)
				{
					return p;
				}
			}
		}
	}
	return nullptr;
}

shared_ptr<TDS_SESSION> dataServer::getTDSSession(string remoteAddr)
{
	int pos = remoteAddr.find(":");
	if (pos < 0)
		return nullptr;
	string ip = remoteAddr.substr(0, pos);
	string sPort = remoteAddr.substr(pos + 1, remoteAddr.length() - pos - 1);
	int iPort = atoi(sPort.c_str());
	return getTDSSession(ip, iPort);
}



shared_ptr<TDS_SESSION> dataServer::getTDSSession(tcpSessionClt* pTcpSess)
{
	lock_guard<mutex> g(m_mutexTdsSessionList);
	for (int i = 0; i < m_vecTdsSession.size(); i++)
	{
		shared_ptr<TDS_SESSION> p = m_vecTdsSession.at(i);
		if (p->pTcpSessionClt == pTcpSess->tcpClt)
		{
			return p;
		}
	}
	return nullptr;
}



/*
生产者-临时消费者模式  
tdsSessionProcessThread  为消费者，临时线程
OnRecvData_TCPServer 为生产者，常驻线程
tdsSession->dataBuff 为任务队列
当任务队列中有数据时，该模式控制 必有1个消费者 且 只有1个消费者

此处使用队列的原因。
不能直接将OnRecvData_TCPServer收到的数据多线程调用tdsSessionProcessThread去处理
因为可能网络中一个大数据包可能会被分包为多次回调，触发多个tdsSessionProcessThread之后，
多线程可能不按照数据流本身的先后顺序执行处理，导致数据包分片数据错误从而导致处理出错
*/
void tdsSessionProcessThread(std::shared_ptr<TDS_SESSION> tdsSession)
{
	//控制只有一个 - m_bSessionProcessing为false才能进入，因此不会出现两个工作者，
	//控制必有一个 - 此处如果return后，创建消费者线程的代码前面的代码已经插入了新任务，并且解锁后已存在工作者一定会进行一次待办任务确认，不会有不被执行的任务
	tdsSession->m_mutexTcpBuff.lock();
	if (tdsSession->m_bSessionProcessing)
	{
		tdsSession->m_mutexTcpBuff.unlock();//必须保证此处unlock后，已有的消费者一定会去检查任务队列
		return;
	}
	tdsSession->m_bSessionProcessing = true;
	tdsSession->m_mutexTcpBuff.unlock();

	while(1)
	{
		//取出任务
		//是否继续工作判断。 当其他线程获得锁，并且m_bSessionProcessing==true时，当前消费者线程一定还在while循环当中
		tdsSession->m_mutexTcpBuff.lock();
		if (tdsSession->dataBuff.size() == 0)
		{
			tdsSession->m_bSessionProcessing = false;
			tdsSession->m_mutexTcpBuff.unlock();
			break;
		}
		TCP_DATA_BUFF tdb = tdsSession->dataBuff.front();
		tdsSession->dataBuff.pop();
		tdsSession->m_mutexTcpBuff.unlock();

		//执行任务
		ds.OnRecvData_TCP(tdb.pData, tdb.iLen, tdsSession);
		delete tdb.pData;
	}
}

void dataServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess)
{
	std::shared_ptr<TDS_SESSION> tdsSession = getTDSSession(pTcpSess);

	std::unique_lock<mutex> g(tdsSession->m_mutexTcpBuff);
	TCP_DATA_BUFF tdb;
	tdb.pData = new char[iLen];
	tdb.iLen = iLen;
	memcpy(tdb.pData, pData, iLen);
	tdsSession->dataBuff.push(tdb);
	//调用临时消费者
	thread t(tdsSessionProcessThread, tdsSession);
	t.detach();
}

void dataServer::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
{
	std::shared_ptr<TDS_SESSION> tdsSession = getTDSSession(connInfo);
	OnRecvData_TCP(pData, iLen, tdsSession);
}





void dataServer::OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	GetLocalTime(&tdsSession->lastRecvTime);

}





//onRecvData需要组包
bool dataServer::OnRecvAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession,bool isPkt)
{
	tdsSession->statisOnRecv(pData, iLen);

	DWORD dwDataLen = iLen;

	//tds rpc over tcp
	if (tdsSession->type == TDS_SESSION_TYPE::tdsClient && isPkt)
	{
		onRecvPkt_tdsClient(pData, iLen, tdsSession);
	}
	
	tdsSession->m_bAppDataRecved = true;
	return true;
}


void dataServer::onRecvPkt_tdsClient(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession)
{
	string req = str::fromBuff(pData,iLen);
	string resp;
	char* binResp = NULL;
	int iBinRespLen = 0;
	bool bNeedLog = true;
	rpcSrv.handleRpcCall(req, resp, binResp, iBinRespLen,bNeedLog, tdsSession);

	if (resp != "")
	{
		tdsSession->sendContent = "text";
		tdsSession->send((char*)resp.data(), resp.length(),bNeedLog);
	}

	if (iBinRespLen > 0)
	{
		tdsSession->sendContent = "binary";
		tdsSession->send(binResp, iBinRespLen, bNeedLog);
	}

	//如果没有任何回复,可能是透传指令,不回复

	if (binResp)
		delete binResp;
}