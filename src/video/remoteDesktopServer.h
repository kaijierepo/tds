#ifdef ENABLE_FFMPEG
#pragma once
#include "tdsSession.h"
#include <memory>
#include <mutex>

//keep reduce latency . see https://blog.csdn.net/yuanrxdu/article/details/78584284?utm_medium=distribute.pc_relevant.none-task-blog-BlogCommendFromBaidu-16.control&depth_1-utm_source=distribute.pc_relevant.none-task-blog-BlogCommendFromBaidu-16.control

struct RDS_CONF{
    RDS_CONF()
    {
        bitRate = 500000;
        frameRate = 20;
        width = 1920;
        height = 1080;
    }
    long bitRate;
    int codecID;
    int frameRate;
    int width;
    int height;
};

class remoteDesktopServer{
public:
    remoteDesktopServer(){
        m_bStart = false;
    };
    void run();
    void startStream(shared_ptr<TDS_SESSION> tdsSession);
    void stopStream();
    void restartStream();
    int OpenVideoCapture();
    int prepareOutFmtCtx();
    int encodeThread();
    int captureThread();
    bool prepareOutCodecCtx();
    bool m_bStart;
    RDS_CONF conf;
    shared_ptr<TDS_SESSION> m_tdsSession;
    std::mutex m_sessionLock;
};

extern remoteDesktopServer rds;
#endif