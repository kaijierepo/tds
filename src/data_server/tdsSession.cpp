#include "pch.h"
#include "tdsSession.h"
#include "mp.h"
#include "logger.h"
#include "ioDev.h"
#include "rpcHandler.h"



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
    videoServiceNode = NULL;
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
     unique_lock<mutex> lock(m_mutexTcpLink);//使用tcplink
     GetLocalTime(&lastSendTime);

     if(pTcpSession) // means lower layer has been disconneted
        return ds.SendAppLayerData(p, len, this);
     if (pTcpSessionClt)
         return pTcpSessionClt->SendData(p, len);
     return 0;
 }

 int TDS_SESSION::getSendedBytes()
 {
     std::unique_lock<mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession)
     {
         return pTcpSession->iSendSucCount;
     }
     else if (pTcpSessionClt)
     {
         //return pTcpSessionClt->
         return 0;
     }
     return 0;
 }

 ioDev* TDS_SESSION::getIODev(string ioAddr)
 {
     for (int i = 0; i < m_vecIoDev.size(); i++)
     {
         ioDev* p = m_vecIoDev.at(i);
         if (p->getIOAddr().ToString() == ioAddr)
         {
             return p;
         }
     }
     return nullptr;
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
     unique_lock<mutex> lock(m_mutexTcpLink);//修改tcplink
     pTcpSession = nullptr;
     pTcpSessionClt = nullptr;
     if (pBridgedTcpClient)
     {
         delete pBridgedTcpClient;
     }
     videoServiceNode = NULL;
     bConnected = false;

     for (int i = 0; i < m_vecIoDev.size(); i++)
     {  
         ioDev* p = m_vecIoDev[i];
         p->m_bOnline = false;
         json j;
         p->toJson(j);
         tdsSrv.notify("io.offline", j);
     }
 }

 void TDS_SESSION::setActivityCheck(bool bEnable)
 {
     if (pTcpSession)
     {
         pTcpSession->bEnableActivityCheck = bEnable;
     }
 }
