/*
ReverseInterface
以主动连接 数据客户端的方式提供服务，包含以下几种情况

1.作为下级服务主动连接上级服务
2.作为推流器对外推流

*/
#pragma once
#include "tdsSession.h"


class ReverseInterface : public ITcpServerCallBack,public ITcpClientCallBack
{
public:
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn) override;
	void onTcpCltEvent_error(tcpClt* pClt,string error) override;
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn) override;
	void OnRecvData_TCP(char* pData, size_t iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(unsigned char* pData, size_t iLen, tcpSession* pCltInfo) override;
	void OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo) override;

public:
	bool run();
	void stop();
	ReverseInterface();
	virtual ~ReverseInterface();

	void rpc_startStreamPush(json params, RPC_RESP& resp, RPC_SESSION session);

	void sendChildTdsRegPkt(std::shared_ptr<TDS_SESSION> p);
	void sendStreamPusherRegPkt(std::shared_ptr<TDS_SESSION> p, string tag);

	tcpSrv m_tcpSrv;
	map<tcpClt*, tcpClt*> m_tcpClt_ParentTds; //作为子服务连接上级服务的客户端

	mutex m_csTcpClt_streamPusher;
	map<tcpClt*, tcpClt*> m_tcpClt_streamPusher; //推流

	void sendToAllSessions(unsigned char* pData, size_t len, bool specialNotify = false);
	void sendToAllSessions(string& s,bool specialNotify = false);
	//应用层会话
	map<void*,std::shared_ptr<TDS_SESSION>> m_reverseTdsSessions;
	mutex m_mutexSessions;

	//级联功能
	string m_masterTdsIP;
	int m_masterTdsPort;

	//zlm的码流管理
	map<string, TIME> m_mapPullerActive;
};


extern ReverseInterface reverseInterface;