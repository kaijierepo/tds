/*
dataserver
rpc服务的tcp服务接口
作为子服务主动连接master服务

*/
#pragma once
#include "wspSrv.h"
#include "proto/wsProto.h"
#include <condition_variable>
#include "tdsSession.h"
#include <memory>
#include "tdscore.h"
#include "webSrv.h"


class dataServer : public ITcpServerCallBack,public ITcpClientCallBack
{
public:
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	void OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);
	int Send(SOCKET sock, char* pBuffer, int iLength);

public:
	bool runAsEdge();
	bool run();
	void stop();
	dataServer();
	virtual ~dataServer();
	tcpClt* m_tcpCltEdge; //作为边缘网关时候的客户端
	map<tcpClt*, tcpClt*> m_tcpCltChildServer; //作为子服务连接上级服务的客户端

	bool OnRecvAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);
	void onRecvPkt_tdsClient(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);


	void sendToAllSessions(char* pData, int len);
	void sendToAllSessions(string& s);
	//应用层会话
	map<void*,std::shared_ptr<TDS_SESSION>> m_Sessions;
	mutex m_mutexSessions;

	//级联功能
	string m_masterTdsIP;
	int m_masterTdsPort;

	//zlm的码流管理
	map<string, TIME> m_mapPullerActive;
};


extern dataServer ds;