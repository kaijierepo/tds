#ifdef ENABLE_FFMPEG
#pragma once
#include <memory>
#include <mutex>
#include "ffmpeg.h"
#include <queue>
#include <string>

//keep reduce latency . see https://blog.csdn.net/yuanrxdu/article/details/78584284?utm_medium=distribute.pc_relevant.none-task-blog-BlogCommendFromBaidu-16.control&depth_1-utm_source=distribute.pc_relevant.none-task-blog-BlogCommendFromBaidu-16.control

struct IN_CONF {
    AVPixelFormat pixelFmt;
    int width;
    int height;
    IN_CONF()
    {
        width = 1920;
        height = 1080;
        pixelFmt = AV_PIX_FMT_RGB24;
    }
};


struct OUT_CONF{
    OUT_CONF()
    {
        bitRate = 500000;
        frameRate = 25;
        width = 1920;
        height = 1080;
        codecID = AV_CODEC_ID_H264;
        filename = "";
    }
    long bitRate;
    AVCodecID codecID;
    int frameRate;
    int width;
    int height;
    std::string filename;
};

#define OUTPUT_BUFF_SIZE 6005535

class videoCodec{
public:
    videoCodec(){
        srcFmtCtx = NULL;
        decodeCtx = NULL;
        frame_convert_ctx = NULL;
        encodeCtx = NULL;
        outFmtCtx = NULL;
        headWrited = false;
        iOutputLen = 0;
        headerBuff = new char[1024 * 1024];
        outputBuff = new char[2 * 1024 * 1024];
        bHeaderCallBack = false;
        iFrameIdx = 0;
        bInit = false;
        output_buf = new char[OUTPUT_BUFF_SIZE];
    };
    ~videoCodec();
    void init();

    void input_Bmp(char* pBMP, int nBMPLen); //including bitmapFileheader and bitmapInfoHeader
    void outputHeader();
    void output();
    void finishOutput();
    char* headerBuff;
    int iHeaderBuffLen;
    char* outputBuff;
    int iOutputLen;
    bool bHeaderCallBack;
    bool bInit;
    IN_CONF inConf;
    OUT_CONF outConf;
    int curPts;
    int64_t iFrameIdx;
    bool headWrited;
    AVFormatContext* srcFmtCtx;
    AVCodecContext* decodeCtx;
    std::queue<AVFrame*> frameList;  //YUV frame buffer decoded or to be encoded
    SwsContext* frame_convert_ctx;
    AVCodecContext* encodeCtx;
    bool init_encodeCtx();
    AVFormatContext* outFmtCtx;
    int init_outFmtCtx();

    char*  output_buf;
};

#else
#endif