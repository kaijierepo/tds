#include "pch.h"
#include "tdsSession.h"
#include "mp.h"
#include "logger.h"
#include "ioDev.h"
#include "rpcHandler.h"
#include "ioSrv.h"
#include "webSrv.h"
#include "httplib.h"
#include "streamServer.h"


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
    port = p->remotePort;
    ip = p->remoteIP;
    p->pALSession = this;
    type = TDS_SESSION_TYPE::iodev;
}

TDS_SESSION::TDS_SESSION(tcpSessionClt* p)
{
    Init();
    m_bActiveSession = true;
    bConnected = true;
    pTcpSessionClt = p->tcpClt;
    sock = p->sock;
    port = p->srvPort;
    ip = p->srvIP;
    p->pALSession = this;
    type = TDS_SESSION_TYPE::iodev;
}

RPC_SESSION TDS_SESSION::getRpcSession()
{
    RPC_SESSION s = *this;
    s.remoteAddr = getRemoteAddr();
    return s;
}

string TDS_SESSION::getId()
{
    return getRemoteAddr();
}

string TDS_SESSION::getRemoteAddr()
{
    unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
    if (pTcpSession)
    {
       return pTcpSession->remoteIP + ":" + str::fromInt(pTcpSession->remotePort);
    }
    return "";
}

bool TDS_SESSION::getTcpSession(tcpSession& ts)
{
    std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);
    if (pTcpSession)
    {
        ts = *pTcpSession;
        return true;
    }
    else
    {
        return false;
    }
}

void TDS_SESSION::Init()
{
    m_bSingleDevMode = false;
    m_bAppDataRecved = false;
    abandonLen = 0;
    m_bStateLessSession = false;
    m_bActiveSession = false;
    m_bNeedLog = true;
    bConnected = false;
    pTcpSession = NULL;
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
    m_IoDev = NULL;
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
        closesocket(pTcpSession->sock);
    }
    else if (pTcpSessionClt)
    {
        closesocket(pTcpSessionClt->m_session.sock);
    }
    return true;
}

string TDS_SESSION::GetClientIp()
{
    if (pTcpSessionClt)
        return pTcpSessionClt->m_remoteIP;
    return "";
}


 int TDS_SESSION::send(char* p,size_t len,bool bNeedLog){
     timeopt::now(&lastSendTime);
     int iSend = 0;

     if (sockPipe != 0)
     {
         iSend = WebServer::sendToWs(p, len, sockPipe);
     }
     else if (pTcpSessionClt)
     {
         iSend = pTcpSessionClt->SendData(p, len);
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
                 wsp.pack(p, len, WS_TEXT_FRAME);
                 pTcpSession->send((char*)wsp.data, wsp.len);
             }
             else {
                 pTcpSession->send((char*)p, len);
             }
         }
     }


     if (bNeedLog && type == TDS_SESSION_TYPE::iodev)
         IOLogSend(p, len,iSend>0,getRemoteAddr());
     return 0;
 }

 int TDS_SESSION::sendStr(string str, bool bNeedLog)
 {
     return send((char*)str.c_str(),str.length(),bNeedLog);
 }

 int TDS_SESSION::getSendedBytes()
 {
     std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession)
     {
         return pTcpSession->iSendSucCount;
     }
     else if (pTcpSessionClt)
     {
         return pTcpSessionClt->m_session.iSendSucCount;
     }
     return 0;
 }

 int TDS_SESSION::getRecvedBytes()
 {
     std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     if (pTcpSession)
     {
         return pTcpSession->iRecvCount;
     }
     else if (pTcpSessionClt)
     {
         return  pTcpSessionClt->m_session.iRecvCount;
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

 void TDS_SESSION::CBridgedTcpClientHandler::OnRecvData_TCPClient(char* pData, int iLen, tcpSessionClt* connInfo)
 {
     pTdsSession->send(pData, iLen);
 }

 

 void TDS_SESSION::onTcpDisconnect()
 {
     //p->pTcpSession is a tcpSession will be deleted after statusChange_tcpSrv callback
     //but TDS_SESSION is not deleted until all users release it
     //so here p->pTcpSession is set to none
     unique_lock<recursive_mutex> lock(m_mutexTcpLink);//修改tcplink
     dsCltStream = nullptr;
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

     videoServiceNode = NULL;
     bConnected = false;
     m_fileUploader.stopWrite();

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

 bool FILE_WRITER::startWrite(string path, long len)
 {
     if (fp)
     {
         stopWrite();
     }

     totalLen = len;
     filePath = path;
     fs::createFolderOfPath(path);
     wstring wpath = charCodec::autoToUtf16(path);

     fp = _wfopen(wpath.c_str(), L"wb");
     if (fp)
     {
         return true;
     }
     else
     {
         int  iError = GetLastError();
         std::cout << "open file failed,error=" << iError << "," << path << std::endl;
     }
     return false;
 }

 void FILE_WRITER::stopWrite()
 {
     if (fp)
     {
         fclose(fp);
         fp = nullptr;
     }
 }

 long FILE_WRITER::write(char* pData, long iLen)
 {
     if (fp)
     {
         long leftLen = totalLen - writedLen;
         long wlen = leftLen > iLen ? iLen : leftLen;

         fwrite(pData, 1, wlen, fp);
         writedLen += wlen;
         
         if (writedLen == totalLen)
         {
             stopWrite();
         }
         else if (writedLen > totalLen)
             assert(false);

         return wlen;
     }
     return 0;
 }

 bool bTestStream = false;
 int ThreadfMp4OverWS(std::shared_ptr<TDS_SESSION> pTestSess) {
     bTestStream = true;
     string str = fs::appPath() + "\\test.mp4";
     //string str = "E:\\VideoTest\\1.avi";
     char* pData = NULL;
     int iDataLen = 0;
     FILE* f = fopen(str.c_str(), "rb");
     if (f)
     {
         fseek(f, 0, SEEK_END);
         iDataLen = ftell(f);
         pData = new char[iDataLen];
         fseek(f, 0, SEEK_SET);
         fread(pData, 1, iDataLen, f);
         fclose(f);
     }
     if (iDataLen == 0)
     {
         bTestStream = false;
         return 0;
     }

     Sleep(500);
     while (pTestSess->type == TDS_SESSION_TYPE::video)
     {
         pTestSess->type = TDS_SESSION_TYPE::video;
         for (int i = 0; i < iDataLen;)
         {
             int iSend = 20000;
             if (i + iSend > iDataLen)
                 iSend = iDataLen - i;
             if (!pTestSess->send(pData + i, iSend))
             {
                 pTestSess->type = TDS_SESSION_TYPE::none;
                 break;
             }
             i += iSend;
             Sleep(40);
         }
     }
     pTestSess->type = TDS_SESSION_TYPE::none;
     delete pData;
     bTestStream = false;
     return 0;
 }

 
