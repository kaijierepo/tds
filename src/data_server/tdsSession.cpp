#include "tdsSession.h"
#include "mp.h"


TDS_SESSION::TDS_SESSION()
{
    pTcpSessionActive = NULL;
    role = "";
    encode = "utf8";
    type = "";
    name = "";
    bInitSegSended = false;
    bridgedTcpCltHandler.pTdsSession = this;
    streamMp = NULL;
}

TDS_SESSION::~TDS_SESSION()
{
}

void TDS_SESSION::Init()
{
     DS_TRANS_LAYER_SESSION::Init();
    mapTagDataSubscribe.clear();
    bSubAll = false;
    bInitSegSended = false;
}

string TDS_SESSION::GetClientIp()
{
    if (pTcpSessionActive)
        return pTcpSessionActive->m_strServerIP;
    return "";
}

 int TDS_SESSION::send(char* p,int len){
     std::lock_guard<std::mutex> gd(m_mutex);
     if(pTLServer == nullptr) // means lower layer has been disconneted
        return false; 
     return pTLServer->SendAppLayerData(p, len, this);
 }

 void TDS_SESSION::CBridgedTcpClientHandler::ConnStatusChange(ConnInfo* connInfo, bool bIsConn)
 {
 }

 void TDS_SESSION::CBridgedTcpClientHandler::OnRecvData_TCPClient(char* pData, int iLen, ConnInfo* connInfo)
 {
     pTdsSession->send(pData, iLen);
 }

 void TDS_SESSION::onTcpDisconnect()
 {
     //p->pTcpSession is a tcpSession will be deleted after ConnStatusChange callback
     //but TDS_SESSION is not deleted until all users release it
     //so here p->pTcpSession is set to none
     //this is not safe,a critical section should be used for p->pTcpSession
     //[unsafe]
     pTLServer = nullptr;
     pTcpSession = nullptr;
     if (pBridgedTcpClient)
     {
         delete pBridgedTcpClient;
     }
     if (streamMp)
     {
         streamMp->m_streamPusher(false, NULL);
     }
     streamMp = NULL;
 }
