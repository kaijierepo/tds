#include "ioDev_tb3386.h"
#include <cstring>
#include <cstdlib>
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
    // 超时未收到数据说明链路已死。常见为半开连接：对端异常断开未发FIN，
    // 本端socket始终处于已连接状态，永远收不到数据，仅标记离线无法自愈。
    // 故超时后主动stop+run重建TCP连接，让下位机重新建立链路（参照 ioSrv 周期重连的做法）。
    const long long OFFLINE_TIMEOUT_MS = 90000;   // 90秒无数据判定链路已死（正常周期上发约60秒一次，留有余量）
    const int RECONNECT_INTERVAL_SEC = 60;        // 重连最小间隔，避免死循环狂刷

    long long inactiveTime = timeopt::CalcTimePassMilliSecond(m_stLastActiveTime);
    if (inactiveTime > OFFLINE_TIMEOUT_MS) {
        std::string s = str::format("%dms未收到数据,超时时间%lldms", (int)inactiveTime, OFFLINE_TIMEOUT_MS);
        setOffline(false, s);

        if (timeopt::CalcTimePassSecond(m_stLastReconnectTime) > RECONNECT_INTERVAL_SEC) {
            LOG("[warn]tb3386超时无数据,主动重连,ioAddr=%s,tag=%s,inactive=%lldms", getIOAddrStr().c_str(), m_strTagBind.c_str(), inactiveTime);
            timeopt::now(&m_stLastReconnectTime);
            stop();
            run();
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
        case (uint8_t)E_315_PROTOCOL_TYPE::GONGKUANG_REAL_VAL_0x81: {   //0x81 工况参数实时值更新，valType=0x20 为轨温
            StWorkingConditionValRes* pRes = (StWorkingConditionValRes*)data.lpdata;
            if (pRes->valType == 0x20) {   //0x20 轨温（挂在对应道岔下）
                updateRailTempCache(pRes->zzjid, pRes->value);
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
        switch (record.gaptype)
        {
        case 1:cache.acqType = 1; break;
        case 2:cache.acqType = 5; break;
        default:cache.acqType = 2; break;
        }
        
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

void ioDev_tb3386::updateRailTempCache(unsigned short id, short val)
{
    if (val == 0x7FFF) {   //0x7FFF 表示无效
        return;
    }

    //0x81 工况实时值精度0.1℃。轨温属于整组道岔，但下位机只在某一台转辙机(J1)上上报。
    //此处按“道岔”定位：把轨温写入与上报转辙机同处一个道岔下的所有转辙机缓存(id)，同步给 J2/J3/X1。
    std::string srcTag = getZZJTagByID(id);
    if (srcTag.empty()) {
        return;
    }
    size_t dot = srcTag.rfind('.');
    std::string group = (dot == std::string::npos) ? srcTag : srcTag.substr(0, dot);   //道岔位号
    float fRailTemp = (float)val / 10.0f;

    std::lock_guard<std::mutex> lock(m_csZZJ);
    for (size_t i = 0; i < m_channels.size(); i++) {
        ioChannel* pCh = m_channels[i];

        //同一道岔下的兄弟转辙机通道
        std::string chTag = m_strTagBind + "." + pCh->m_strTagBind;
        size_t dot2 = chTag.rfind('.');
        std::string chGroup = (dot2 == std::string::npos) ? chTag : chTag.substr(0, dot2);
        if (chGroup != group) {
            continue;
        }

        int sid = atoi(pCh->getDevAddrStr().c_str());
        if (sid <= 0) {
            continue;
        }
        StZZJCache& cache = m_mapZZJ[sid];
        cache.id = (unsigned short)sid;
        cache.railTemp = fRailTemp;
        cache.validRailTemp = true;
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

        // tempRail 来自 0x81 工况实时值(valType=0x20 轨温)，0x81收到时已按道岔同步进本转辙机缓存。
        // 尚未收到轨温时回退为设备温度占位。
        yyjson_mut_obj_add_real(doc, root, "tempRail", cache.validRailTemp ? cache.railTemp : cache.temp);

        // 天气预报：优先从站点天气MP当前值读取实时温度与天气状况，缺失时沿用设备温度占位。
        double tempWeather = cache.temp;
        OBJ* pObj = prj.queryObj(m_strTagBind, "zh"); //根据 m_strTagBind 找到站点对象
        if (pObj) {
            // 不能直接用 pObj->GetMPByTag("天气","zh")：queryObj 精确查找要求传入以站点自身位号为前缀的
            // 完整相对位号(如"站.天气")，单传叶子名"天气"会因前缀不匹配直接返回空。改用按名字在站点子树内查找。
            MP* pmp = pObj->GetDescendantMPByName("天气"); //获取站点下面的天气MP的当前值
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
