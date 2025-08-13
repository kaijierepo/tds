#include "pch.h"
#include "tdsSession.h"
#include "mp.h"
#include "logger.h"
#include "ioDev.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "webSrv.h"


TDS_SESSION::TDS_SESSION()
{
    Init();
}

TDS_SESSION::~TDS_SESSION()
{
    if (pBridgedTcpServer) {
        pBridgedTcpServer->stop();
        delete pBridgedTcpServer;
    }
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
    sendedLen = 0;
    recvedLen = 0;
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

RPC_SESSION& TDS_SESSION::getRpcSession()
{
    RPC_SESSION& s = *this;
    s.remoteAddr = getRemoteAddr();
    return s;
}

string TDS_SESSION::getRemoteAddr()
{
    return str::format("%s:%d", remoteIP.c_str(), remotePort);
}

string TDS_SESSION::getLocalAddr() {
    return str::format("%s:%d", localIP.c_str(), localPort);
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
    bridgedTcpSrvHandler.pTdsSession = this;
    sock = 0;
    iTLProto = TRANSFER_LAYER_PROTO::TLT_UNKNOWN;
    iALProto = APP_LAYER_PROTO::UNKNOWN;
    pTcpSession = nullptr;
    m_alBuf.Init();
    m_childTdsHttpPort = 667;
    m_childTdsHttpsPort = 666;
    conn_id = 0;
    webServer = nullptr;
    m_bEnableIO = true;
    pBridgedTcpServer = nullptr;
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
#ifdef _WIN32
        closesocket(pTcpSessionClt->sock);
#else
        // TODO: linux
#endif
    }
    return true;
}

string TDS_SESSION::GetClientIp()
{
    if (pTcpSessionClt)
        return pTcpSessionClt->remoteIP;
    return "";
}

size_t TDS_SESSION::send(char* p, size_t len, bool bNeedLog) {
    return send((unsigned char*)p, len, bNeedLog);
}

size_t TDS_SESSION::send(unsigned char* p,size_t len,bool bNeedLog) {
    if (m_bEnableIO == false) {
        return 0;
    }

     timeopt::now(&lastSendTime);

     int iSend = 0;
     if (conn_id != 0) {
         iSend = ((WebServer*)webServer)->notifyWs(p, len, conn_id);
     }
     else if (pTcpSessionClt) {
         iSend = pTcpSessionClt->pTcpClt->SendData(p, len);
     }
     else {
         unique_lock<recursive_mutex> lock(m_mutexTcpLink);
         if (pTcpSession) { // means lower layer has been disconneted
             iSend = pTcpSession->send((char*)p, len);
         }
     }

     if (bNeedLog && type == TDS_SESSION_TYPE::iodev)
         IOLogSend(p, len, iSend > 0, getRemoteAddr(), getLocalAddr());

     return iSend;
 }

 size_t TDS_SESSION::sendStr(string str, bool bNeedLog) {
     return send((unsigned char*)str.c_str(),str.length(),bNeedLog);
 }

 size_t TDS_SESSION::getSendedBytes() {
     std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession) {
         return pTcpSession->iSendSucCount;
     }
     else if (pTcpSessionClt) {
         return pTcpSessionClt->iSendSucCount;
     }

     return 0;
 }

 size_t TDS_SESSION::getRecvedBytes() {
     std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession) {
         return pTcpSession->iRecvCount;
     }
     else if (pTcpSessionClt) {
         return  pTcpSessionClt->iRecvCount;
     }

     return 0;
 }

 ioDev* TDS_SESSION::getBindDev(string ioAddr) {
     //不指定ioAddr，默认第一个ioDev就是绑定的设备
     if (ioAddr == "") {
         if (m_mapBindIoDev.size() > 0) {
             return m_mapBindIoDev.begin()->second;
         }
     }
     else {
         for (auto& i : m_mapBindIoDev) {
             if (i.first->getIOAddrStr() == ioAddr) {
                 return i.first;
             }
         }
     }

     return nullptr;
 }

 void TDS_SESSION::bindIoDev(ioDev* ioDev)
 {
     m_mapBindIoDev[ioDev] = ioDev;
 }

 void TDS_SESSION::unbindIoDev(ioDev* ioDev)
 {
     m_mapBindIoDev.erase(ioDev);
 }

 void TDS_SESSION::CBridgedTcpClientHandler::statusChange_tcpClt(tcpSessionClt* connInfo, bool bIsConn)
 {
 }

 void TDS_SESSION::CBridgedTcpClientHandler::onRecvData_tcpClt(unsigned char* pData, size_t iLen, tcpSessionClt* connInfo)
 {
     pTdsSession->send(pData, iLen);
 }

 

 void TDS_SESSION::onTcpDisconnect() {
     //p->pTcpSession is a tcpSession will be deleted after statusChange_tcpSrv callback
     //but TDS_SESSION is not deleted until all users release it
     //so here p->pTcpSession is set to none

     //clear tcp link
     m_mutexTcpLink.lock();
     pTcpSession = nullptr;
     pTcpSessionClt = nullptr;
     m_mutexTcpLink.unlock();


     if (pBridgedTcpClient) {
         delete pBridgedTcpClient;
     }

     for(auto& i : m_mapBindIoDev) {
         i.first->unbindIOSession();
         i.first->setOffline();

         logger.logInternal("[ioDev]设备掉线,ioAddr=" + i.first->getIOAddrStr() + ",tag=" + i.first->m_strTagBind);
     }

     m_mapBindIoDev.clear();

     if (bridgedIoSession) {
         bridgedIoSession->bridgedIoSessionClient = NULL;
         bridgedIoSession = NULL;
     }

     if (bridgedIoSessionClient) {
         bridgedIoSessionClient->bridgedIoSession = NULL;
         bridgedIoSessionClient = NULL;
     }

     bConnected = false;
 }

 void TDS_SESSION::setActivityCheck(bool bEnable)
 {
     if (pTcpSession)
     {
         pTcpSession->bEnableActivityCheck = bEnable;
     }
 }
 

 void TDS_SESSION::CBridgedTcpServerHandler::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
 {
     auto mut_doc = yyjson_mut_doc_new(nullptr);
     auto mut_root = yyjson_mut_obj(mut_doc);
     yyjson_mut_doc_set_root(mut_doc, mut_root);

     string remoteAddr = pCltInfo->remoteIP + ":" + str::fromInt(pCltInfo->remotePort);
     yyjson_mut_obj_add_strcpy(mut_doc, mut_root, "remoteAddr", remoteAddr.c_str());
     yyjson_mut_obj_add_bool(mut_doc, mut_root, "connect", bIsConn);

     char* temp = yyjson_mut_write(mut_doc, 0, 0);
     if (temp) {
         string s = temp;
         pTdsSession->sendStr(s, false);

         free(temp);
     }

     yyjson_mut_doc_free(mut_doc);
 }

 void TDS_SESSION::CBridgedTcpServerHandler::onRecvData_tcpSrv(unsigned char* pData, size_t iLen, tcpSession* pTcpSess)
 {
	 auto mut_doc = yyjson_mut_doc_new(nullptr);
	 auto mut_root = yyjson_mut_obj(mut_doc);
	 yyjson_mut_doc_set_root(mut_doc, mut_root);

	 string remoteAddr = pTcpSess->remoteIP + ":" + str::fromInt(pTcpSess->remotePort);
	 yyjson_mut_obj_add_strcpy(mut_doc, mut_root, "remoteAddr", remoteAddr.c_str());
     string strData = str::bytesToHexStr(pData, iLen);
     yyjson_mut_obj_add_strcpy(mut_doc, mut_root, "recv", strData.c_str());

	 char* temp = yyjson_mut_write(mut_doc, 0, 0);
     if (temp) {
         string s = temp;
         pTdsSession->sendStr(s, false);

         free(temp);
     }
	 
	 yyjson_mut_doc_free(mut_doc);
 }
