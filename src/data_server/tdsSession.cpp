#include "pch.h"
#include "tdsSession.h"
#include "mp.h"
#include "logger.h"
#include "ioDev.h"
#include "rpcHandler.h"
#include "ioSrv.h"



TDS_SESSION::TDS_SESSION()
{
    Init();
}

TDS_SESSION::~TDS_SESSION()
{
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
    m_IoDevTcpLink = NULL;
}

bool TDS_SESSION::isConnected()
{
    std::unique_lock<recursive_mutex> lock(m_mutexTcpLink);
    return bConnected;
}

string TDS_SESSION::GetClientIp()
{
    if (pTcpSessionClt)
        return pTcpSessionClt->m_remoteIP;
    return "";
}

//会话数据包 监视会话。不监视自己的数据包发送。
//rpc的实时数据轮询时。 响应线程多线程处理。 会并发调用此发送接口。
vector<std::shared_ptr<TDS_SESSION>> sessionPktSessions;
shared_mutex csSessionPktSessions;
void sendToSessionPktSessions(char* p,int len)
{
    csSessionPktSessions.lock();
    for (int i = 0; i < sessionPktSessions.size(); i++)
    {
        std::shared_ptr<TDS_SESSION> session = sessionPktSessions[i];
        if (!session->isConnected())
        {
            sessionPktSessions.erase(sessionPktSessions.begin() + i);
            i--;
            continue;
        }
    }
    csSessionPktSessions.unlock();

    csSessionPktSessions.lock_shared();
    for (int i = 0; i < sessionPktSessions.size(); i++)
    {
        std::shared_ptr<TDS_SESSION> session = sessionPktSessions[i];
        session->send(p, len, false);
    }
    csSessionPktSessions.unlock_shared();
}

void TDS_SESSION::statisOnSend(char* p, int len,bool success)
{
    {
        shared_lock<shared_mutex> lock(csSessionPktSessions);
        if (sessionPktSessions.size() == 0)
            return;
    }
    

    //监视会话的数据包不记录日志
    //if (type == TDS_SESSION_TYPE::sessionPkt ||
    //    type == TDS_SESSION_TYPE::commpkt ||
    //    type == TDS_SESSION_TYPE::log ||
    //    type == TDS_SESSION_TYPE::video) //sessionPkt自己的日志不记录
    //{
    //    return;
    //}


    //仅监视io数据包
    if (type.find(TDS_SESSION_TYPE::iodev) == string::npos)
    {
        return;
    }
    if (!m_bNeedLog)
        return;

    json j;
    SYSTEMTIME st;
    GetLocalTime(&st);
    j["time"] = timeopt::st2strWithMilli(st);
    j["remoteAddr"] = getRemoteAddr();
    if(success)
        j["type"] = "发送成功";
    else
        j["type"] = "发送失败";
    j["len"] = len;
    j["data"] = str::fromBytes(p, len);
    j["sessionType"] = type;
    string s = j.dump(4);

    sendToSessionPktSessions((char*)s.c_str(), s.length());
}


void TDS_SESSION::statisOnRecv(char* p, int len)
{
    {
        shared_lock<shared_mutex> lock(csSessionPktSessions);
        if (sessionPktSessions.size() == 0)
            return;
    }
    //监视会话的数据包不记录日志
    //if (type == TDS_SESSION_TYPE::sessionPkt ||
    //    type == TDS_SESSION_TYPE::commpkt ||
    //    type == TDS_SESSION_TYPE::log ||
    //    type == TDS_SESSION_TYPE::video) //sessionPkt自己的日志不记录
    //{
    //    return;
    //}

    //仅监视io数据包
    if (type.find(TDS_SESSION_TYPE::iodev) == string::npos)
    {
        return;
    }

    try {
        json j;
        SYSTEMTIME st;
        GetLocalTime(&st);
        j["time"] = timeopt::st2strWithMilli(st);
        j["remoteAddr"] = getRemoteAddr();
        j["type"] = "接收";
        j["len"] = len;
        //j["data"] = str::fromBuff(p, len);
        j["data"] = str::fromBytes(p, len);
        j["sessionType"] = type;
        string s = j.dump();
        sendToSessionPktSessions((char*)s.c_str(), s.length());
    }
    catch (std::exception& e)
    {
        LOG("[error]接收到非utf8字符串,tdsSession=%s,%s", getRemoteAddr().c_str(), e.what());
    }
}



 int TDS_SESSION::send(char* p,int len,bool bNeedLog){
     unique_lock<recursive_mutex> lock(m_mutexTcpLink);//使用tcplink
     GetLocalTime(&lastSendTime);

     int iSend = 0;

     if(pTcpSession) // means lower layer has been disconneted
         iSend = ds.SendAppLayerData(p, len, this);
     if (pTcpSessionClt)
         iSend = pTcpSessionClt->SendData(p, len);


     if (bNeedLog)
         statisOnSend(p, len,iSend>0);
     return 0;
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
         //return pTcpSessionClt->
         return 0;
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
         //return pTcpSessionClt->
         return 0;
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
     if (m_IoDevTcpLink)
     {
         m_IoDevTcpLink->setIOSession(NULL);
         m_IoDevTcpLink->m_bOnline = false;
         logger.logInternal("[ioDev]设备掉线,ioAddr=" + m_IoDevTcpLink->getIOAddrStr() + ",tag=" + m_IoDevTcpLink->m_strTagBind);
         m_IoDevTcpLink = NULL;
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

     for (int i = 0; i < m_vecIoDev.size(); i++)
     {  
         string ioAddr = m_vecIoDev[i];
         ioDev* p = ioSrv.getIODev(ioAddr);
         if (p)
         {
             logger.logInternal("[ioDev]设备掉线,ioAddr=" + ioAddr + ",tag=" + p->m_strTagBind);
             p->m_bOnline = false;
             json j;
             p->toJson(j);
             rpcSrv.notify("io.offline", j);
         }
         
     }
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
