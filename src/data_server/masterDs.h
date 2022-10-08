/*
masterDs
接受childDs的主动连接
主动同步childDs的配置
主动同步childDs的数据

*/
#pragma once
#include "tdscore.h"
#include "tcpSrv.h"
#include "tdsSession.h"


class MasterDs : public ITcpServerCallBack
{
public:
	void statusChange_tcpSrv(tcpSession* pTcpSess, bool bIsConn);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pTcpSess);
	void OnRecvData(unsigned char* pData, int iLen, std::shared_ptr<TDS_SESSION> childSession);
	void onRecvPkt(string pkt, std::shared_ptr<TDS_SESSION> childSession);

public:
	bool run();
	void stop();

	void workingProc();

	MasterDs();
	virtual ~MasterDs();

	map<void*, std::shared_ptr<TDS_SESSION>> m_vecChildTds;
	mutex m_mutexChildTdsList;

	tcpSrv* m_tcpSrv; 
	int m_masterTdsPort;
	int m_rpcId;
};