#include "ioDev_tb3386.h"
#include <cstring>
#include "prj.h"
#include "mp.h"
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

// 手动将缺口图像写入 db 目录，与 db.json 放在同一文件夹（.缺口）
static bool saveGapImageFile(const std::string& storeTag, const DB_TIME& stTime, const unsigned char* pImgData, size_t imgLen)
{
    if (pImgData == NULL || imgLen == 0) {
        return false;
    }

    std::string folder = db.getPath_dataFolder(storeTag, stTime);
    LOG("[diag]saveGapImageFile folder=[%s]", folder.c_str());
    if (folder.empty()) {
        return false;
    }

    std::string path = folder + "/" + stTime.toStampHMS() + ".jpg";
    LOG("[diag]saveGapImageFile path=[%s] 存在=%d", path.c_str(), (int)TDB::fileExist(path));

    if (TDB::fileExist(path)) {
        return false;
    }

    DB_FS::createFolderOfPath(path);
    bool ret = DB_FS::writeFile(path, (unsigned char*)pImgData, imgLen);
    LOG("[diag]saveGapImageFile writeFile ret=%d", (int)ret);
    return ret;
}

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

    static bool s_bDumpOnce = false;
    if (!s_bDumpOnce && m_channels.size() > 0) {
        s_bDumpOnce = true;
        LOG("[diag]通道总数=%d", (int)m_channels.size());
        for (size_t i = 0; i < m_channels.size(); i++) {
            ioChannel* pCh = m_channels[i];
            LOG("[diag]通道 addr=[%s] tag=[%s]", pCh->getDevAddrStr().c_str(), pCh->m_strTagBind.c_str());
        }
    }
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

    if (len >= 15 && pData[7] == FRAME_TYPE_DATA) {
        uint8_t cmdid = pData[12];
        uint16_t sid = *(uint16_t*)(pData + 13);
        LOG("[diag][流] cmdid=0x%02X sid=%d len=%d", (int)cmdid, (int)sid, (int)len);
    }

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
        if (pBasic->cmdid == CMD_CODE_ALARM_AND_IMG) {
            StAlarmAndImgInfo* pImg = (StAlarmAndImgInfo*)data.lpdata;
            LOG("[diag]0x27帧 sid=%d time=%u imglen=%u lpimg=%s", (int)pImg->sid, (unsigned int)pImg->time, (unsigned int)pImg->imglen, (pImg->lpimg ? "有" : "无"));
        } else {
            LOG("[diag]收到数据帧 cmdid=0x%02X", (int)pBasic->cmdid);
        }

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
        case CMD_CODE_IMGINFO: {   //0x2A 图像信息，下位机上发的缺口图像
            StImgInfoRes* pImg = (StImgInfoRes*)data.lpdata;
            if (pImg->lpimg != NULL && pImg->imglen > 0) {
                saveGapImageForId(pImg->sid, pImg->time, (const unsigned char*)pImg->lpimg, pImg->imglen);
            }
            break;
        }
        case CMD_CODE_ALARM_AND_IMG: {   //0x27 报警/预警信息及缺口图像信息
            StAlarmAndImgInfo* pImg = (StAlarmAndImgInfo*)data.lpdata;
            if (pImg->lpimg != NULL && pImg->imglen > 0) {
                saveGapImageForId(pImg->sid, pImg->time, (const unsigned char*)pImg->lpimg, pImg->imglen);
            }
            break;
        }
        case CMD_CODE_LASTGAPIMG: {   //0x29 道岔缺口最新图像
            StLastGapImgRes* pImg = (StLastGapImgRes*)data.lpdata;
            if (pImg->lpimg != NULL && pImg->imglen > 0) {
                saveGapImageForId(pImg->sid, pImg->time, (const unsigned char*)pImg->lpimg, pImg->imglen);
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
        cache.pos = record.fixorinvert;

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

    std::string storeTag = zzjTag + ".缺口";
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

    // 采集类型：来自缺口帧的采集原因值，1 扳动(switch)、2 周期(cyclic)、5 过车(cross)。
    const char* acqType = "unknown";
    switch (cache.acqType) {
    case 1: acqType = "switch"; break;
    case 2: acqType = "cyclic"; break;
    case 5: acqType = "cross"; break;
    default: break;
    }
    yyjson_mut_obj_add_str(doc, root, "acqType", acqType);

    // tempZZJ 来自 0x24 命令的设备温度。
    if (cache.validTemp) {
        yyjson_mut_obj_add_real(doc, root, "tempZZJ", cache.temp);
        // 轨温协议未提供，暂用设备温度占位，后续可从其他 MP 接入。
        yyjson_mut_obj_add_real(doc, root, "tempRail", cache.temp);

        // 天气预报：优先从站点天气MP当前值读取实时温度与天气状况，缺失时沿用设备温度占位。
        double tempWeather = cache.temp;
        OBJ* pObj = prj.queryObj(m_strTagBind, "zh"); //根据 m_strTagBind 找到站点对象
        if (pObj) {
            MP* pmp = pObj->GetMPByTag("天气", "zh"); //获取站点下面的天气MP的当前值
            if (pmp) {
                const std::string& curVal = pmp->m_curVal;
                if (!curVal.empty() && curVal != "null") {
                    yyjson_doc* wdoc = yyjson_read(curVal.c_str(), curVal.size(), 0); //使用yyjson解析curVal
                    if (wdoc) {
                        yyjson_val* wroot = yyjson_doc_get_root(wdoc);
                        yyjson_val* wt = wroot ? yyjson_obj_get(wroot, "temperature") : nullptr; //实时温度
                        if (wt && yyjson_is_num(wt)) {
                            tempWeather = yyjson_get_num(wt); //将值填入到 root
                        }
                        yyjson_val* wc = wroot ? yyjson_obj_get(wroot, "condition_code") : nullptr; //天气状况代码
                        if (wc && yyjson_is_num(wc)) {
                            yyjson_mut_obj_add_int(doc, root, "weatherCondition", (int)yyjson_get_num(wc));
                        }
                        yyjson_doc_free(wdoc);
                    }
                }
            }
        }
        yyjson_mut_obj_add_real(doc, root, "tempWeather", tempWeather);
    }

    const char* json = yyjson_mut_write(doc, 0, NULL);
    std::string sDE = json ? json : "{}";
    free((void*)json);
    yyjson_mut_doc_free(doc);

    return sDE;
}

void ioDev_tb3386::saveGapImageForId(unsigned short sid, unsigned int time, const unsigned char* pImgData, size_t imgLen)
{
    if (pImgData == NULL || imgLen == 0) {
        return;
    }

    std::string zzjTag = getZZJTagByID(sid);
    LOG("[diag]saveGapImageForId sid=%d zzjTag=[%s]", (int)sid, zzjTag.c_str());
    if (zzjTag.empty()) {
        return;
    }

    // 与数据落盘保持一致的 tag：zzjTag + ".缺口"
    std::string storeTag = zzjTag + ".缺口";
    DB_TIME stTime;
    stTime.fromUnixTime(time);
    saveGapImageFile(storeTag, stTime, pImgData, imgLen);
}
