#include "pch.h"
#include "tdsSession.h"
#include "mp.h"
#include "logger.h"
#include "ioDev.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "webSrv.h"
#include "httplib.h"
#include "wsProto.h"


TDS_SESSION::TDS_SESSION()
{
    Init();
}

TDS_SESSION::~TDS_SESSION()
{
}

TDS_SESSION::TDS_SESSION(tcpSession* p)
{
    Init();
    timeopt::now(&stCreateTime);
    bConnected = true;
    pTcpSession = p;
    sock = p->sock;
    remotePort = p->remotePort;
    remoteIP = p->remoteIP;
    p->pALSession = this;
    type = TDS_SESSION_TYPE::iodev;
}

TDS_SESSION::TDS_SESSION(tcpSessionClt* p)
{
    Init();
    bConnected = true;
    pTcpSessionClt = &p->pTcpClt->m_session;
    sock = p->sock;
    remotePort = p->remotePort;
    remoteIP = p->remoteIP;
}

void TDS_SESSION::setRpcSession(RPC_SESSION* pRpc)
{
    RPC_SESSION* pThis = (RPC_SESSION*)this;
    *pThis = *pRpc;
}

RPC_SESSION TDS_SESSION::getRpcSession()
{
    RPC_SESSION s = *this;
    s.remoteAddr = getRemoteAddr();
    return s;
}

string TDS_SESSION::getRemoteAddr()
{
    return str::format("%s:%d", remoteIP.c_str(), remotePort);
}

string TDS_SESSION::getRemoteIP()
{
    return remoteIP;
}

void TDS_SESSION::Init()
{
    m_bSingleDevMode = true;
    m_bAppDataRecved = false;
    abandonLen = 0;
    m_bNeedLog = true;
    bConnected = false;
    pTcpSession = nullptr;
    pTcpSessionClt = nullptr;
    pBridgedTcpClient = nullptr;
    type = "";
    name = "";
    bridgedTcpCltHandler.pTdsSession = this;
    sock = 0;
    iTLProto = TRANSFER_LAYER_PROTO_TYPE::TLT_UNKNOWN;
    iALProto = APP_LAYER_PROTO::UNKNOWN;
    pTcpSession = nullptr;
    m_alBuf.Init();
    m_IoDev = nullptr;
    m_childTdsHttpPort = 667;
    m_childTdsHttpsPort = 666;
}

bool TDS_SESSION::isConnected()
{
    std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);
    return bConnected;
}

bool TDS_SESSION::disconnect()
{
    std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
    if (pTcpSession)
    {
        //目前tcpServer统一使用mongoose的 poll模型。poll模型closesocket不会触发响应,使用shutdown
        //closesocket(pTcpSession->sock);
        int shutDownBoth = 2;
        shutdown(pTcpSession->sock, shutDownBoth);
    }
    else if (pTcpSessionClt)
    {
        closesocket(pTcpSessionClt->sock);
    }
    return true;
}

string TDS_SESSION::GetClientIp()
{
    if (pTcpSessionClt)
        return pTcpSessionClt->remoteIP;
    return "";
}

size_t TDS_SESSION::send(char* p, size_t len, bool bNeedLog)
{
    return send((unsigned char*)p, len, bNeedLog);
}


size_t TDS_SESSION::send(unsigned char* p,size_t len,bool bNeedLog){
     timeopt::now(&lastSendTime);
     int iSend = 0;

     if (sockPipe != 0)
     {
         iSend = ServiceInterface::sendToWs(p, len, sockPipe);
     }
     else if (pTcpSessionClt)
     {
         iSend = pTcpSessionClt->pTcpClt->SendData(p, len);
     }
     else 
     {
         unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
         if (pTcpSession) // means lower layer has been disconneted
         {
             //远端是websocket客户端
             if (iTLProto == TRANSFER_LAYER_PROTO_TYPE::TLT_WEB_SOCKET)
             {
                 CWSPPkt wsp;
                 wsp.pack((char*)p, len, WS_TEXT_FRAME);
                 iSend = pTcpSession->send((char*)wsp.data, wsp.len);
             }
             else {
                 iSend = pTcpSession->send((char*)p, len);
             }
         }
     }


     if (bNeedLog && type == TDS_SESSION_TYPE::iodev)
         IOLogSend(p, len,iSend>0,getRemoteAddr());
     return iSend;
 }

 size_t TDS_SESSION::sendStr(string str, bool bNeedLog)
 {
     return send((unsigned char*)str.c_str(),str.length(),bNeedLog);
 }

 size_t TDS_SESSION::getSendedBytes()
 {
     std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession)
     {
         return pTcpSession->iSendSucCount;
     }
     else if (pTcpSessionClt)
     {
         return pTcpSessionClt->iSendSucCount;
     }
     return 0;
 }

 size_t TDS_SESSION::getRecvedBytes()
 {
     std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession)
     {
         return pTcpSession->iRecvCount;
     }
     else if (pTcpSessionClt)
     {
         return  pTcpSessionClt->iRecvCount;
     }
     return 0;
 }

 ioDev* TDS_SESSION::getIODev(string ioAddr)
 {
     for (int i = 0; i < m_vecIoDev.size(); i++)
     {
         string p = m_vecIoDev.at(i);
         if (p == ioAddr)
         {
             return ioSrv.getIODev(ioAddr);
         }
     }
     return nullptr;
 }

 void TDS_SESSION::CBridgedTcpClientHandler::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
 {
 }

 void TDS_SESSION::CBridgedTcpClientHandler::OnRecvData_TCPClient(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo)
 {
     pTdsSession->send(pData, iLen);
 }

 

 void TDS_SESSION::onTcpDisconnect()
 {
     //p->pTcpSession is a tcpSession will be deleted after statusChange_tcpSrv callback
     //but TDS_SESSION is not deleted until all users release it
     //so here p->pTcpSession is set to none
     unique_lock<recursive_mutex> lock(m_mutexTcpLink);//修改tcplink
     pTcpSession = nullptr;
     pTcpSessionClt = nullptr;
     if (pBridgedTcpClient)
     {
         delete pBridgedTcpClient;
     }
     if (m_IoDev)
     {
         m_IoDev->bindIOSession(NULL);
         m_IoDev->setOffline();
         logger.logInternal("[ioDev]设备掉线,ioAddr=" + m_IoDev->getIOAddrStr() + ",tag=" + m_IoDev->m_strTagBind);
         m_IoDev = NULL;
     }
     if (bridgedIoSession)
     {
         bridgedIoSession->bridgedIoSessionClient = NULL;
         bridgedIoSession = NULL;
     }
     if (bridgedIoSessionClient)
     {
         bridgedIoSessionClient->bridgedIoSession = NULL;
         bridgedIoSessionClient = NULL;
     }

     bConnected = false;

     //暂时取消1个链接上线多台设备机制
     //此处ioSrv.getIODev会锁住ioSrv，如果此时刚好进行doCycleTask周期采集。会导致死锁
     // m_mutexTcpLink 套 ioSrv.m_csThis  和 ioSrv.m_csThis 套m_mutexTcpLink导致。doCycleTask后面的发送会锁m_mutexTcpLink
     //for (int i = 0; i < m_vecIoDev.size(); i++)
     //{  
     //    string ioAddr = m_vecIoDev[i];
     //    ioDev* p = ioSrv.getIODev(ioAddr);
     //    if (p)
     //    {
     //        logger.logInternal("[ioDev]设备掉线,ioAddr=" + ioAddr + ",tag=" + p->m_strTagBind);
     //        p->setOffline();
     //    }
     //}
 }

 void TDS_SESSION::setActivityCheck(bool bEnable)
 {
     if (pTcpSession)
     {
         pTcpSession->bEnableActivityCheck = bEnable;
     }
 }
 
