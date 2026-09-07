#ifndef TDS_IO_SERVER_IODEV_TB3386_H
#define TDS_IO_SERVER_IODEV_TB3386_H

#include "ioDev_tdsp.h"
#include "proto_tb3386.h"
#include <map>
#include <mutex>

class ioDev_tb3386 : public ioDev
{
public:
    ioDev_tb3386();
    ~ioDev_tb3386();

    void DoAcq();
    void DoCycleTask() override;
    void onEvent_online() override;
    void onRecvData_tcpClt(unsigned char* pData, size_t len, tcpSessionClt* connInfo) override;

private:
    struct StZZJCache
    {
        unsigned short id;
        float gap;
        float std;
        float offset;
        uint8_t acqType;
        unsigned char pos;
        unsigned int gapTime;
        float temp;
        float railTemp;
        float humi;
        unsigned int tempTime;
        unsigned int humiTime;
        bool validGap;
        bool validTemp;
        bool validRailTemp;
        bool validHumi;

        StZZJCache()
            : id(0), gap(0.0f), std(0.0f), offset(0.0f), pos(0),
              gapTime(0), temp(0.0f), railTemp(0.0f), humi(0.0f), tempTime(0), humiTime(0),
              validGap(false), validTemp(false), validRailTemp(false), validHumi(false)
        {
        }
    };

    bool onRecvPkt(unsigned char* pData, size_t iLen);
    std::string getZZJTagByID(unsigned short id);
    OBJ* getZZJObj(unsigned short id);
    void updateGapCache(unsigned short id, const StGapRecord& record);
    void updateStateCache(unsigned short id, const StSdataRecord& record);
    void updateRailTempCache(unsigned short id, short val);
    void storeGapData(unsigned short id);
    std::string buildGapJson(const StZZJCache& cache, const std::string& strTag, const std::string& strTime);
    void saveGapImageForId(unsigned short sid, unsigned int time, const unsigned char* pImgData, size_t imgLen);

    std::map<unsigned short, StZZJCache> m_mapZZJ;
    std::mutex m_csZZJ;
};

#endif // TDS_IO_SERVER_IODEV_TB3386_H
