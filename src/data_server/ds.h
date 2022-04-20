/*
dataserver
*/
#pragma once
#include "wspSrv.h"
#include "proto/wsProto.h"
#include <condition_variable>
#include "tdsSession.h"
#include <memory>
#include "tdscore.h"


#define MAX_CLIENT_NUM int_MaxClients_MAX
#define UID_TIMER_CHECK 1
#define MAX_RECEIVE_LENGTH 512

class database;
class dataServer : public ITcpServerCallBack,public ITcpClientCallBack
{
public:
	void statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn);
	void statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn);
	void OnRecvData_TCP(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	void OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo);
	void OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo);

	void getUrlParams(string& url, map<string, string>& mapParams);

	void onWebsocketSessionOpen(string& strData, std::shared_ptr<TDS_SESSION> tdsSession);



	int SendAppLayerData(char* pData, int iLen, void* pAppLayerCltInfo);
	bool isHttpPkt(string str);

	string checkTransportLayerProto(string& strData, tcpSession* pTcpSess);
	bool httpHandleInternal(string strData,std::shared_ptr<TDS_SESSION> pAppLayerClt);
	shared_ptr<TDS_SESSION> getTDSSession(tcpSession* pTcpSess);
	shared_ptr<TDS_SESSION> getTDSSession(string remoteIP, int remotePort);
	shared_ptr<TDS_SESSION> getTDSSession(string remoteAddr);
	shared_ptr<TDS_SESSION> getTDSSession(tcpSessionClt* pTcpSess);
	int Send(SOCKET sock, char* pBuffer, int iLength);
	string getRDSPage();

public:
	bool runAsCloud();
	bool runAsEdge();
	void stop();
	dataServer();
	virtual ~dataServer();
	tcpSrv* m_tcpSrv; //被动连接的tdsSession
	tcpClt* m_tcpCltEdge; //作为边缘网关时候的客户端
	vector<tcpClt*> m_tcpCltList; //主动连接的tdsSession
	wspSrv m_wspSrv;

	bool onRecvHttpPkt(char* pDataBuf, int iLen, std::shared_ptr<TDS_SESSION> pALC);

	bool handleAppLayerData_IODev(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);

	bool handleAppLayerData_Bridge(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	bool OnRecvAppLayerData(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool isPkt = false);
	void onRecvPkt_ioDev(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession, bool registerPkt = false);
	void onRecvPkt_tdsClient(char* pData, int iLen, std::shared_ptr<TDS_SESSION> tdsSession);
	
	bool OnRecvRawTdsRpc(char* pData, int iLen, std::shared_ptr<TDS_SESSION> pALC);

	vector<std::shared_ptr<TDS_SESSION>> m_vecTdsSession;
	vector<void*> GetSessionList();
	string getSessionStatus(json params);
	mutex m_mutexTdsSessionList;
	FILE* m_pRecFile;
	SYSTEMTIME m_stLastFileRecvTime;
};


extern dataServer ds;