#include "ioDev_tb3386.h"
#include <cstring>
#include "prj.h"
#include "database/tDatabase.h"
#include "common.h"
#include "yyjson.h"
#include "logger.h"
#include "ioChan.h"

namespace ns_ioDev_tb3386
{
    ioDev* createDev()
    {
        return new ioDev_tb3386();
    }

    class createReg
    {
    public:
        createReg()
        {
            mapDevCreateFunc["dcqk-sys-device"] = createDev;
            mapDevTypeLabel["dcqk-sys-device"] = "TB3386道岔缺口";
        }
    };

    createReg reg;

    static size_t IsValidPkt_315(uint8_t* pData, size_t iLen)
    {
        if (iLen < 16) {
            return 0;
        }

        if (pData[0] != 0x71 || pData[1] != 0x6B || pData[2] != 0x6E || pData[3] != 0x65 || pData[4] != 0x74) {
            return 0;
        }

        if (pData[5] != 0x02 && pData[5] != 0x80) {
            return 0;
        }

        uint8_t frameType = pData[7];
        if (frameType != FRAME_TYPE_JSON && frameType != FRAME_TYPE_DATA && frameType != FRAME_TYPE_HEARTBEAT) {
            return 0;
        }

        uint32_t frameLen = *(uint32_t*)(pData + 8);
        if (frameLen + 16 > iLen) {
            return 0;
        }

        uint8_t* pFrameEnd = pData + frameLen + 12;
        if (pFrameEnd[0] != 0xFF || pFrameEnd[1] != 0xFF || pFrameEnd[2] != 0xFF || pFrameEnd[3] != 0xFF) {
            return 0;
        }

        return frameLen + 16;
    }
}

using namespace ns_ioDev_tb3386;

ioDev_tb3386::ioDev_tb3386()
{
}

ioDev_tb3386::~ioDev_tb3386()
{
}

void ioDev_tb3386::DoAcq()
{
    // TB3386 下位机主动上发 0x24/0x26 数据帧，上位机无需主动轮询。
    timeopt::now(&m_stLastAcqTime);
}

void ioDev_tb3386::DoCycleTask()
{
    // 下位机主动上发模式，只保留离线检测，不执行基类 TDSP 的 JSON 轮询。
    if (m_bEnableOfflineTimeout && m_offlineTimeout > 0) {
        long long inactiveTime = timeopt::CalcTimePassMilliSecond(m_stLastActiveTime);
        if (inactiveTime > m_offlineTimeout) {
            std::string s = str::format("%dms未收到数据,超时时间%dms", inactiveTime, m_offlineTimeout);
            setOffline(false, s);
        }
    }
}

void ioDev_tb3386::onEvent_online()
{
    ioDev::onEvent_online();
    // 下位机上线后会主动上发数据，无需额外请求。
}

void ioDev_tb3386::onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo)
{
    if (connInfo == NULL || connInfo->bEnable == false) {
        return;
    }

    timeopt::now(&m_stLastActiveTime);
    setOnline();

    stream2pkt* pab = &m_pab;
    pab->PushStream(pData, len);

    while (pab->PopPkt(IsValidPkt_315, false)) {
        if (pab->iAbandonLen > 0) {
            std::string remoteAddr = getDevAddrStr();
            LOG("[warn]地址 " + remoteAddr + " 已提取正确包,丢弃包前面错误数据:" + pab->abandonData);
            m_abandonLen += pab->iAbandonLen;
        }
        onRecvPkt(pab->pkt, pab->iPktLen);
    }
}

bool ioDev_tb3386::onRecvPkt(unsigned char* pData, size_t iLen)
{
    StFrame data;
    memset(&data, 0,  sizeof(StFrame));

    if (!Parse315Protocol::Parse(data, (void*)pData, (int)iLen)) {
        return false;
    }

    if (data.ftype == FRAME_TYPE_DATA && data.lpdata != NULL) {
        StDataBasic* pBasic = (StDataBasic*)data.lpdata;

        switch (pBasic->cmdid) {
        case CMD_CODE_GAPVAL: {
            StGapValue* pGapValue = (StGapValue*)data.lpdata;
            if (pGapValue->lpdata != NULL) {
                for (int i = 0; i < pGapValue->cnt; i++) {
                    StGapRecord* pRecord = &((StGapRecord*)pGapValue->lpdata)[i];
                    updateGapCache(pRecord->sid, *pRecord);
                }
            }
            break;
        }
        case CMD_CODE_YWINFO: {
            StOilLevelInfo* pState = (StOilLevelInfo*)data.lpdata;
            if (pState->lpdata != NULL) {
                for (int i = 0; i < pState->cnt; i++) {
                    StSdataRecord* pRecord = &((StSdataRecord*)pState->lpdata)[i];
                    updateStateCache(pRecord->sid, *pRecord);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    Parse315Protocol::Release(data, 1);
    return true;
}

std::string ioDev_tb3386::getZZJTagByID(unsigned short id)
{
    std::string idStr = str::fromInt((int)id);
    for (size_t i = 0; i < m_channels.size(); i++) {
        ioChannel* pCh = m_channels[i];

        if (pCh->getDevAddrStr() == idStr) {
            return m_strTagBind + "." + pCh->m_strTagBind;
        }
    }

    return "";
}

OBJ* ioDev_tb3386::getZZJObj(unsigned short id)
{
    std::string zzjTag = getZZJTagByID(id);
    if (zzjTag.empty()) {
        return NULL;
    }

    return prj.queryObj(zzjTag, "zh");
}

void ioDev_tb3386::updateGapCache(unsigned short id, const StGapRecord& record)
{
    bool needStore = false;

    {
        std::lock_guard<std::mutex> lock(m_csZZJ);
        StZZJCache& cache = m_mapZZJ[id];
        cache.id = id;
        cache.gap = (float)record.gap / 100.0f;
        cache.std = (float)record.std / 100.0f;
        cache.offset = (float)record.offset / 100.0f;
        cache.acqType = record.gaptype;

        if (record.time != cache.gapTime) {
            cache.gapTime = record.time;
            cache.validGap = true;
            needStore = true;
        }
    }

    if (needStore) {
        storeGapData(id);
    }
}

void ioDev_tb3386::updateStateCache(unsigned short id, const StSdataRecord& record)
{
    std::lock_guard<std::mutex> lock(m_csZZJ);
    StZZJCache& cache = m_mapZZJ[id];
    cache.id = id;

    if (record.temptime != 0) {
        cache.temp = (float)record.temperature / 100.0f;
        cache.tempTime = record.temptime;
        cache.validTemp = true;
    }

    if (record.humtime != 0) {
        cache.humi = (float)record.humidity / 100.0f;
        cache.humiTime = record.humtime;
        cache.validHumi = true;
    }
}

void ioDev_tb3386::storeGapData(unsigned short id)
{
    std::string zzjTag = getZZJTagByID(id);
    if (zzjTag.empty()) {
        return;
    }

    std::string storeTag = zzjTag + ".缺口";

    StZZJCache cache;
    {
        std::lock_guard<std::mutex> lock(m_csZZJ);
        std::map<unsigned short, StZZJCache>::iterator it = m_mapZZJ.find(id);
        if (it == m_mapZZJ.end()) {
            return;
        }
        cache = it->second;
    }

    if (!cache.validGap) {
        return;
    }

    DB_TIME stTime;
    stTime.fromUnixTime(cache.gapTime);
    std::string strTime = stTime.toStr(false);
    std::string sDE = buildGapJson(cache, storeTag, strTime);

    RPC_RESP rr;
    tds->call("input", sDE, rr);
}

std::string ioDev_tb3386::buildGapJson(const StZZJCache& cache, const std::string& strTag,const std::string& strTime)
{
    yyjson_mut_doc* doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "tag", strTag.c_str());
    yyjson_mut_obj_add_str(doc, root, "time", strTime.c_str());
    yyjson_mut_obj_add_real(doc, root, "val", cache.gap);
    yyjson_mut_obj_add_real(doc, root, "std", cache.std);
    yyjson_mut_obj_add_str(doc, root, "pos", cache.pos == 0 ? "fix" : "invert");

    // tempZZJ 来自 0x24 命令的设备温度。
    if (cache.validTemp) {
        yyjson_mut_obj_add_real(doc, root, "tempZZJ", cache.temp);
        // 轨温与天气预报温度协议未提供，暂用同一温度占位，后续可从其他 MP 接入。
        yyjson_mut_obj_add_real(doc, root, "tempRail", cache.temp);
        yyjson_mut_obj_add_real(doc, root, "tempWeather", cache.temp);
    }

    const char* json = yyjson_mut_write(doc, 0, NULL);
    std::string sDE = json ? json : "{}";
    free((void*)json);
    yyjson_mut_doc_free(doc);

    return sDE;
}
