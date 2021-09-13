#include "tdsSession.h"
#include "mp.h"


TDS_SESSION::TDS_SESSION()
{
    Init();
}

TDS_SESSION::~TDS_SESSION()
{
}

void TDS_SESSION::Init()
{
    pTcpSessionClt = NULL;
    role = "";
    encode = "utf8";
    type = "";
    name = "";
    bInitSegSended = false;
    bridgedTcpCltHandler.pTdsSession = this;
    streamMp = NULL;
    sock = 0;
    iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN;
    iALProto = APP_LAYER_PROTO::UNKNOWN;
    pTcpSession = NULL;
    m_alBuf.Init();
    mapTagDataSubscribe.clear();
    bSubAll = false;
    bInitSegSended = false;
}

string TDS_SESSION::GetClientIp()
{
    if (pTcpSessionClt)
        return pTcpSessionClt->m_strServerIP;
    return "";
}

 int TDS_SESSION::send(char* p,int len){
     GetLocalTime(&lastSendTime);
     if(pTLServer) // means lower layer has been disconneted
        return pTLServer->SendAppLayerData(p, len, this);
     if (pTcpSessionClt)
         return pTcpSessionClt->SendData(p, len);
     return 0;
 }

 void TDS_SESSION::CBridgedTcpClientHandler::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
 {
 }

 void TDS_SESSION::CBridgedTcpClientHandler::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
 {
     pTdsSession->send(pData, iLen);
 }

 void TDS_SESSION::onTcpDisconnect()
 {
     //p->pTcpSession is a tcpSession will be deleted after statusChange_tcpSrv callback
     //but TDS_SESSION is not deleted until all users release it
     //so here p->pTcpSession is set to none
     //this is not safe,a critical section should be used for p->pTcpSession
     //[unsafe]
     pTLServer = nullptr;
     pTcpSession = nullptr;
     pTcpSessionClt = nullptr;
     if (pBridgedTcpClient)
     {
         delete pBridgedTcpClient;
     }
     if (streamMp && streamMp->m_streamPusher)
     {
         streamMp->m_streamPusher(false, NULL);
     }
     streamMp = NULL;
     boolConnected = false;
 }

 void TDS_SESSION::setActivityCheck(bool bEnable)
 {
     if (pTcpSession)
     {
         pTcpSession->bEnableActivityCheck = bEnable;
     }
 }
