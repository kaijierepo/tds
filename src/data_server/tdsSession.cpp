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
    m_IoDevTcpLink = NULL;
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
         if (p->getIOAddrStr() == ioAddr)
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
     if (m_IoDevTcpLink)
     {
         m_IoDevTcpLink->setIOSession(NULL);
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
