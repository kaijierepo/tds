/*
dataserver
rpc服务的tcp服务接口
parentTdsServer功能
childTdsServer功能

*/
#pragma once
#include "wspSrv.h"
#include "proto/wsProto.h"
#include <condition_variable>
#include "tdsSession.h"
#include <memory>
#include "tdscore.h"
#include "webSrv.h"


#define MAX_CLIENT_NUM int_MaxClients_MAX
#define UID_TIMER_CHECK 1
#define MAX_RECEIVE_LENGTH 512

class dataServer : public ITcpServerCallBack,public ITcpClientCallBack
{
public:
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	void OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
	int SendAppLayerData(char* pData, int iLen, void* pAppLayerCltInfo);
	shared_ptr<TDS_SESSION> getTDSSession(tcpSession* pTcpSess);
	shared_ptr<TDS_SESSION> getTDSSession(string remoteIP, int remotePort);
	shared_ptr<TDS_SESSION> getTDSSession(string remoteAddr);
	shared_ptr<TDS_SESSION> getTDSSession(tcpSessionClt* pTcpSess);
	int Send(SOCKET sock, char* pBuffer, int iLength);

public:
	bool runAsEdge();
	bool run();
	void stop();
	dataServer();
	virtual ~dataServer();
	tcpClt* m_tcpCltEdge; //作为边缘网关时候的客户端
	tcpClt* m_tcpCltChildServer; //作为子服务连接上级服务的客户端

	bool OnRecvAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);
	void onRecvPkt_tdsClient(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	vector<std::shared_ptr<TDS_SESSION>> m_vecTdsSession;
	mutex m_mutexTdsSessionList;

	//级联功能
	string m_parentTdsIP;
	int m_parentTdsPort;
	int m_childTdsPort;
};


extern dataServer ds;