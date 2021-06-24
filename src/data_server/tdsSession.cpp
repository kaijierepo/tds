#include "tdsSession.h"


TDS_SESSION::TDS_SESSION()
{
    pTcpServ = NULL;
    role = "";
    encode = "utf8";
    type = "";
    name = "";
    bVideoStream = false;
    bInitSegSended = false;
    bridgedTcpCltHandler.pTdsSession = this;

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
    if (pTcpServ)
        return pTcpServ->m_strServerIP;
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
