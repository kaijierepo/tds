/*
dataserver
*/
#pragma once
#include "wspSrv.h"
#include "wsProto.h"
#include <condition_variable>
#include "tdsSession.h"
#include <memory>
#include "tdscore.h"


#define MAX_CLIENT_NUM int_MaxClients_MAX
#define UID_TIMER_CHECK 1
#define MAX_RECEIVE_LENGTH 512

class database;
class dataServer : public ITcpServerCallBack, public CALServer,public CTLServer
{
public:
	void ConnStatusChange(tcpSession* pCltInfo, bool bIsConn);
	int SendAppLayerData(char* pData, int iLen, void* pAppLayerCltInfo) override;
	bool isHttpPkt(string str);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	string checkTransportLayerProto(string& strData, tcpSession* pTcpSess);
	bool httpHandleInternal(string strData,std::shared_ptr<TDS_SESSION> pAppLayerClt);
	shared_ptr<TDS_SESSION> getTDSSession(tcpSession* pTcpSess);
	int Send(SOCKET sock, char* pBuffer, int iLength);
	string getRDSPage();
public:
	bool run();
	dataServer();
	virtual ~dataServer();
	tcpSrv* m_tcpSrv;
	wspSrv m_wspSrv;

	void SendData(char* pData, int iLen);
	char arrSendBuf[100000];

	bool onRecvHttpPkt(char* pDataBuf, int iLen, std::shared_ptr<TDS_SESSION> pALC);
	bool OnRecvAppLayerPkt(char* pDataBuf, int iLen, void* pCltInfo) override;
	bool OnRecvRawTdsRpc(char* pData, int iLen, void* pCltInfo);

	vector<std::shared_ptr<TDS_SESSION>> m_vecTdsSession;
	vector<void*> GetSessionList() override;
	mutex m_mutexTdsSessionList;
	FILE* m_pRecFile;
	SYSTEMTIME m_stLastFileRecvTime;
};
extern dataServer ds;