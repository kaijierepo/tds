#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "proto/wsProto.h"
#include "shellServer.h"
#include "ioSrv.h"
#include "tds_imp.h"

shellServer shellSrv;

shellServer::shellServer()
{
    tds->shellServer = &shellSrv;
}

bool shellServer::run()
{
    //m_tcpSrv.run(this,671);
    //暂时不再使用，如果后续需要使用，使用 666,667端口的   /shell 路径下的websocket连接
    return true;
}

bool shellServer::init()
{
    return true;
}

void shellServer::statusChange_tcpSrv(tcpSession* pCltInfo, bool bIsConn)
{


}

void shellServer::packGzhResp(string resp)
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
        resp += "命令简写        完整命令                 命令功能\n";
        resp += "ega            enableGlobalAlarm       全局报警使能\n";
        resp += "sca            stopCycleAcq            关闭周期采集\n";
        resp += "rca            runCycleAcq             启动周期采集\n";
        resp += "sai * 30       setAcqInterval * 30     所有设备采集周期设置为30秒\n";
        resp += "io.sc          io.saveConf             保存io配置\n";
        resp += "io.taca        io.triggerAllCycleAcq   触发所有设备进行一次周期采集\n";
    }
    else if (sc.method == "io.saveConf")
    {
        ioSrv.saveConf();
        resp = "保存io配置到io.json";
    }
    else if (sc.method == "getTdsConf")
    {
        json j = tdsImp.tdsConf.toJson();
        resp = j.dump(2);
    }
    else if (sc.method == "io.loadConf")
    {
        ioSrv.loadConf();
        resp = "加载io配置从io.json";
    }
    else if (sc.method == "io.stopCycleAcq" || sc.method == "io.sca" || sc.method == "sca")
    {
        ioSrv.m_stopCycleAcq = true;
        resp = "暂停所有周期采集";
    }
    else if (sc.method == "io.runCycleAcq" || sc.method == "io.rca" || sc.method == "rca")
    {
        ioSrv.m_stopCycleAcq = false;
        resp = "启动所有周期采集";
    }
    else if (sc.method == "io.triggerAllCycleAcq")
    {
        ioSrv.lock_conf_unique();
        for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
        {
            ioDev* p = ioSrv.m_vecChildDev[i];
            p->triggerCycleAcq();
        }
        ioSrv.unlock_conf_unique();
        resp += "通知所有设备进行一次周期采集";
    }
    else if (sc.method == "io.setAcqInterval" || sc.method == "io.sai")
    {
        if (sc.params.size() >= 2)
        {
            string p = sc.params[0];
            string interval = sc.params[1];
            float fInterval = atof(interval.c_str());
            if (p == "*")
            {
                ioSrv.lock_conf_unique();
                for (int i = 0; i < ioSrv.m_vecChildDev.size(); i++)
                {
                    ioDev* p = ioSrv.m_vecChildDev[i];
                    p->m_fAcqInterval = fInterval;
                }
                ioSrv.unlock_conf_unique();
                resp += "所有设备的周期采集间隔设置为" + str::fromFloat(fInterval) + "秒";
            }
            else
            {
                resp = "参数错误";
            }
        }
        else
        {
            resp = "参数错误";
        }
    }
    else if(sc.method == "enableGlobalAlarm" || sc.method == "ega")
    {
        if (sc.params.size() >= 1)
        {
            string p = sc.params[0];
            if (p == "1")
            {
                tds->conf->enableGlobalAlarm = true;
                resp = "全局报警启用";
            }
            else if(p== "0")
            {
                tds->conf->enableGlobalAlarm = false;
                resp = "全局报警禁用";
            }
            else
            {
                resp = "参数错误";
            }
        }
        else
        {
            resp = "参数错误";
        }
    }
    else {
        resp = "未知命令";
    }

    resp += "\n";
    CWSPPkt wsPkt;
    wsPkt.pack(resp.c_str(), resp.length(), WS_FrameType::WS_TEXT_FRAME);
    pCltInfo->send((char*)wsPkt.data, wsPkt.len);
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
        m_s2p.Init();
        m_s2p.PushStream((unsigned char*)pData, iLen);
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
