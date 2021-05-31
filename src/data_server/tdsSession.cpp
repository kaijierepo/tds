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