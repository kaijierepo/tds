/*
basic usage of ffmpeg api
imaging every Context as a data processor,it changes the input data format to another data format
a complete usage of ffmpeg fuctions will be like this:
+------------------------+-----------------------+--------------+---------------------------+-------------+
|   Creator              |        Init           |  Processor   |       Proc Method         |  Proc Out   |
+========================+=======================+==============|===========================+=============|
|           avio_alloc_context/avio_open         | in avioCtx   | user define read hook     |  rawBuffer  | 
|------------------------|-----------------------|--------------|---------------------------+-------------|
|avformat_alloc_context()| avformat_open_input() | in formatCtx |     av_read_frame         |  AVPacket   |  avformat_free_context()
|------------------------|-----------------------|--------------|---------------------------+-------------|
|avcodec_alloc_context3()|    avcodec_open2      | de codecCtx  | avcodec_send_packet()     | src AVFrame |
|                        |                       |              | avcodec_receive_frame()   |             |
|------------------------------------------------|--------------|---------------------------+-------------|
|                 sws_getContext                 |   scaleCtx   |     sws_scale             | dest AVFrame|
|------------------------------------------------|--------------|---------------------------+-------------|
|avcodec_alloc_context3()|   avcodec_open2       | en codecCtx  |  avcodec_send_frame()     |  AVPacket   |
|                        |                       |              |  avcodec_receive_packet() |             |
|------------------------------------------------|--------------|---------------------------+-------------|
|      avformat_alloc_output_context2            |out formatCtx |av_interleaved_write_frame |  rawBuffer  |
|------------------------------------------------|--------------|---------------------------+-------------|
|          avio_alloc_context/avio_open          |out avioCtx   | user define write hook    |to user buff |
|------------------------------------------------|--------------+---------------------------+-------------+


*/
#ifdef ENABLE_FFMPEG

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/imgutils.h"
}
 
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avdevice.lib")
#pragma comment(lib, "avfilter.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "libcharset.lib")
#pragma comment(lib, "libiconv.lib")
#pragma comment(lib, "lzma.lib")
#pragma comment(lib, "opus.lib")
#pragma comment(lib, "swresample.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "vpxmd.lib")
#pragma comment(lib, "zlib.lib")

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#endif