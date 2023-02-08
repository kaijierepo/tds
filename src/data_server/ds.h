/*
dataserver
rpc服务的tcp服务接口
作为子服务主动连接master服务

*/
#pragma once
#include "tdsSession.h"


class dataServer : public ITcpServerCallBack,public ITcpClientCallBack
{
public:
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	void OnRecvData_TCP(char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(char* pData, size_t iLen, tcpSession* pCltInfo) override;
	void OnRecvData_TCPClient(char* pData, size_t iLen, tcpSessionClt* connInfo) override;
	int Send(SOCKET sock, char* pBuffer, size_t iLength);

public:
	bool run();
	void stop();
	dataServer();
	virtual ~dataServer();

	void rpc_startStreamPush(json params, RPC_RESP& resp, RPC_SESSION session);

	void sendChildTdsRegPkt(std::shared_ptr<TDS_SESSION> p);
	void sendStreamPusherRegPkt(std::shared_ptr<TDS_SESSION> p, string tag);

	map<tcpClt*, tcpClt*> m_tcpClt_ParentTds; //作为子服务连接上级服务的客户端

	mutex m_csTcpClt_streamPusher;
	map<tcpClt*, tcpClt*> m_tcpClt_streamPusher; //推流

	void sendToAllSessions(unsigned char* pData, size_t len);
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