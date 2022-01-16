#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wsProto.h"
#include "shellServer.h"

shellServer shellSrv;

bool shellServer::run()
{
    m_tcpSrv.run(this,669);
    return true;
}

void shellServer::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{


}

void shellServer::sendResp(string resp)
{
    CWSPPkt wsPkt;
    wsPkt.pack(resp.c_str(),resp.length(), WS_FrameType::WS_TEXT_FRAME);
}



void shellServer::handleCmd(string cmd, tcpSession* pCltInfo)
{
    string resp;
    cmd = str::trim(cmd, "\n");
    SHELL_CMD sc = parseCmd(cmd);

    if (sc.method == "")
    {
     
    }
    else if (sc.method == "help" || sc.method == "h")
    {
        resp = "help h 帮助\n";
        resp += "enableGlobalAlarm  ega  全局报警使能\n";
    }
    else if(sc.method == "enableGlobalAlarm" || sc.method == "ega")
    {
        if (sc.params.size() >= 1)
        {
            string p = sc.params[0];
            if (p == "1")
            {
                tds->conf->enableGlobalAlarm = true;
                resp = "全局报警启用\n";
            }
            else if(p== "0")
            {
                tds->conf->enableGlobalAlarm = false;
                resp = "全局报警禁用\n";
            }
            else
            {
                resp = "参数错误\n";
            }
        }
        else
        {
            resp = "参数错误\n";
        }
    }


    CWSPPkt wsPkt;
    wsPkt.pack(resp.c_str(), resp.length(), WS_FrameType::WS_TEXT_FRAME);
    m_tcpSrv.SendData(wsPkt.data, wsPkt.len, pCltInfo);
}

void shellServer::OnRecvData_TCPServer(char* pData, int iLen, tcpSession* pCltInfo)
{
    string sReq = str::fromBuff(pData, iLen);


    if (sReq.find("HTTP") != string::npos && CWSPPkt::isHandShake(sReq))
    {
        //回复websocket握手
        CWSPPkt req;
        std::string handshakeString = req.GetHandshakeString(sReq);
        send(pCltInfo->sock, handshakeString.c_str(), handshakeString.size(), 0);
    }
    else
    {
        m_s2p.PushStream(pData, iLen);
        while (m_s2p.PopPkt(APP_LAYER_PROTO::PROTOCOL_WEBSOCKET))
        {
            CWSPPkt req;
            WS_FrameType type = req.GetFrameType((char*)m_s2p.pkt, m_s2p.iPktLen);

            if (type == WS_TEXT_FRAME)
            {
                req.unpack((char*)pData, iLen);
                string s = req.payloadData;
                handleCmd(s, pCltInfo);
            }
        }
    }
}

SHELL_CMD shellServer::parseCmd(string req)
{
    vector<string> fields;
    str::split(fields,req, " ");

    for (int i = 0; i < fields.size(); i++)
    {
        string& field = fields[i];
        field = str::trim(field);
        if (field == "")
        {
            fields.erase(fields.begin() + i);
            i--;
        }

    }

    SHELL_CMD sc;

    if (fields.size() > 0)
    {
        sc.method = fields[0];
        for (int i = 1; i < fields.size(); i++)
        {
            string p = fields[i];
            sc.params.push_back(p);
        }
    }

    return sc;
}
