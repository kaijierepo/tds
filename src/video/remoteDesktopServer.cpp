#include "pch.h"
#ifdef ENABLE_FFMPEG
#include "remoteDesktopServer.h"
#include "video/ffmpeg.h"
#include <thread>
#include <time.h>
#include "tdsSession.h"
#include <chrono>
#include "common.h"
#include "logger.h"
#include <queue>

#define OUTPUT_BUFF_SIZE 6005535

/**
实时转码注意事项
. 关闭sync-lookahead
 . 降低rc-lookahead，但别小于10,默认是-1
 . 降低threads(比如从12降到6)
 . 禁用rc-lookahead
 . 禁用b-frames
 . 缩小GOP，if gop is too small,the decode load can be too heavy
 . 开启x264的 -preset fast/faster/verfast/superfast/ultrafast参数
 . 使用-tune zerolatency 参数
*/

remoteDesktopServer rds;

AVFormatContext	*srcFmtCtx = NULL, *destFmtCtx = NULL;
AVCodecContext	*decodeSrcCtx = NULL;
AVCodecContext  *encodeDestCtx = NULL;
AVCodec			*pCodec_Video;
int VideoIndex, AudioIndex;

std::queue<AVPacket*> toEncodeList;
mutex lockToEncode;
 
SwsContext *img_convert_ctx;
int frame_size = 0;
unsigned char  output_buf[OUTPUT_BUFF_SIZE];
uint8_t  *frame_buf = NULL;
 
bool bCap = true;



int remoteDesktopServer::OpenVideoCapture()
{
	// prepare format context
	// see https://ffmpeg.org/ffmpeg-devices.html#gdigrab for options
	AVInputFormat *ifmt=av_find_input_format("gdigrab");
	AVDictionary *options = NULL;
	av_dict_set(&options, "framerate", (char*)str::fromInt(conf.frameRate).c_str(), NULL);
	//av_dict_set(&options,"offset_x","20",0);
	//The distance from the top edge of the screen or desktop
	//av_dict_set(&options,"offset_y","40",0);
	//Video frame size. The default is to capture the full screen
	//av_dict_set(&options,"video_size","320x240",0);
	if(avformat_open_input(&srcFmtCtx, "desktop", ifmt, &options)!=0)
	{
        printf("Couldn't open input stream.\n");
		return -1;
	}
	if(avformat_find_stream_info(srcFmtCtx,NULL)<0)
	{
        printf("Couldn't find stream information.\n");
		return -1;
	}
	if (srcFmtCtx->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
	{
        printf("Couldn't find video stream information.\n");
		return -1;
	}

	// prepare codec context
	decodeSrcCtx = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(decodeSrcCtx, srcFmtCtx->streams[0]->codecpar);
	pCodec_Video = avcodec_find_decoder(decodeSrcCtx->codec_id);
	if(pCodec_Video == NULL)
	{
        printf("Codec not found.\n");
		return -1;
	}
	if(avcodec_open2(decodeSrcCtx, pCodec_Video, NULL) < 0)
	{
		printf("Could not open codec.\n");
		return -1;
	}
 
	
	//prepare scale context
	img_convert_ctx = sws_getContext(decodeSrcCtx->width, decodeSrcCtx->height, decodeSrcCtx->pix_fmt, 
		decodeSrcCtx->width, decodeSrcCtx->height,AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
 
	frame_size = av_image_get_buffer_size(decodeSrcCtx->pix_fmt, decodeSrcCtx->width, decodeSrcCtx->height,1);
 
	return 0;
}

int write_data(void* opaque, unsigned char* buf, int buf_size)
{
	int iSend = rds.m_tdsSession->send((char*)buf,buf_size);
	//fs::appendFile("./test.mp4",(char*)buf,buf_size);
	if(iSend!=buf_size)
		LOG("[warn ][rds] write_data to socket not complete,try to:" + str::fromInt(buf_size) + ",writed:" + str::fromInt(iSend)); 
	return iSend;
}

bool remoteDesktopServer::prepareOutCodecCtx()
{
	if(encodeDestCtx)
		avcodec_free_context(&encodeDestCtx);
	//prepare codec fmt ctx
	//AVCodec *pCodecH264 = avcodec_find_encoder(AV_CODEC_ID_MPEG4);   //no encode delay but can't display on mse.don't know which mime to set
	AVCodec *pCodecH264 = avcodec_find_encoder(AV_CODEC_ID_VP9);
	if (!pCodecH264)
	{
		fprintf(stderr, "h264 codec not found\n");
		exit(1);
	}
	encodeDestCtx = avcodec_alloc_context3(pCodecH264);
	encodeDestCtx->bit_rate = conf.bitRate;//put sample parameters
	encodeDestCtx->width = conf.width;
	encodeDestCtx->height = conf.height;
	encodeDestCtx->gop_size = 50;
	encodeDestCtx->time_base.num = 1;
	encodeDestCtx->time_base.den = conf.frameRate;
	encodeDestCtx->max_b_frames = 0;
	encodeDestCtx->thread_count = 0;
	encodeDestCtx->delay = 0;
	encodeDestCtx->pix_fmt = AV_PIX_FMT_YUV420P;//PIX_FMT_RGB24;
	//if(destFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
	encodeDestCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	//av_opt_set(c->priv_data, /*"preset"*/"libvpx-1080p.ffpreset", /*"slow"*/NULL, 0);
	//real-time streaming (for mpeg-4/h264 encoder)
	//av_opt_set(encodeDestCtx->priv_data,"preset","ultrafast",0);
	//av_opt_set(encodeDestCtx->priv_data, "tune", "zerolatency", 0);
	//real-time streaming (for webm vp9 encoder)  see https://developers.google.com/media/vp9/live-encoding
	av_opt_set(encodeDestCtx->priv_data,"lag-in-frames","0",0);
	av_opt_set(encodeDestCtx->priv_data,"quality","realtime",0);
	av_opt_set(encodeDestCtx->priv_data,"speed","8",0);
	if (avcodec_open2(encodeDestCtx, pCodecH264,NULL) < 0) {
		printf("avcodec_open2 failed\n");
		exit(1);
	}

	return true;
}

int remoteDesktopServer::prepareOutFmtCtx()
{
	if(destFmtCtx)
		avformat_free_context(destFmtCtx);
	AVStream *destStream = NULL;
	avformat_alloc_output_context2(&destFmtCtx, NULL, "webm", NULL);

	AVIOContext* o_ioctx = avio_alloc_context(output_buf, OUTPUT_BUFF_SIZE, 1, this, NULL, &write_data, NULL);
 	destFmtCtx->pb = o_ioctx;
	destFmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
	if (srcFmtCtx->streams[0]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
	{

		//prepare output format context , create stream
		destStream = avformat_new_stream(destFmtCtx, NULL);
		if (!destStream)
		{
			printf("can not new stream for output!\n");
			return -1;
		}
		destStream->codecpar->width = conf.width;
		destStream->codecpar->height = conf.height;
		destStream->codecpar->format = AV_PIX_FMT_YUV420P;
		destStream->codecpar->bit_rate = conf.bitRate;
		destStream->codecpar->codec_id = AV_CODEC_ID_VP9;
		destStream->codecpar->codec_tag = 0;
		destStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	}
 	//av_dump_format(destFmtCtx, 0, NULL, 1);
 
    
	return 0;
}

int remoteDesktopServer::captureThread()
{
	while(1)
	{
		if(!rds.m_bStart)
		{
			timeopt::sleepMilli(100);
			continue;
		}
		if(rds.m_tdsSession == nullptr || rds.m_tdsSession->pTcpSession == nullptr)
		{
			timeopt::sleepMilli(100);
			continue;
		}

		AVPacket* ppkt = av_packet_alloc();
		av_init_packet(ppkt);
		ppkt->data = NULL;
		ppkt->size = 0;
		time_t  ts = timeopt::getTick();
		if (av_read_frame(srcFmtCtx, ppkt) < 0)
		{
			continue;
		}
		time_t  te = timeopt::getTick();
		time_t timeCost_readFrame = te-ts;
		printf("timeCost_readFrame %I64d\n",timeCost_readFrame);

		time_t  tCaptureSrcFmt = timeopt::getTick();
		printf("src fmt captured %I64d\n",tCaptureSrcFmt);

		lockToEncode.lock();
		toEncodeList.push(ppkt);
		lockToEncode.unlock();
	}
}

int remoteDesktopServer::encodeThread()
{
	int got_picture;
	AVFrame	*srcFrame = av_frame_alloc();
	AVFrame *destFrame = av_frame_alloc();
	int size = av_image_get_buffer_size(encodeDestCtx->pix_fmt,encodeDestCtx->width, encodeDestCtx->height,1);
	uint8_t* destFrameBuff = new uint8_t[size];
	av_image_fill_arrays(destFrame->data, destFrame->linesize, destFrameBuff, encodeDestCtx->pix_fmt, encodeDestCtx->width, encodeDestCtx->height, 1);
	destFrame->width = encodeDestCtx->width;
	destFrame->height = encodeDestCtx->height;
	destFrame->format = encodeDestCtx->pix_fmt;

	
	int height = destFmtCtx->streams[VideoIndex]->codecpar->height;
	int width = destFmtCtx->streams[VideoIndex]->codecpar->width;
	int y_size=height*width;
	int64_t curPts = 0;
	clock_t start = clock();
	int64_t curPktPts = 0;
	while(1)
	{
		bool bInitSigment =false;
		if(!rds.m_bStart)
		{
			timeopt::sleepMilli(100);
			continue;
		}
		if(rds.m_tdsSession == nullptr || rds.m_tdsSession->pTcpSession == nullptr)
		{
			timeopt::sleepMilli(100);
			continue;
		}

		if(rds.m_tdsSession->bInitSegSended == false)
		{
			string szLog = str::format("[debug][rds]stream initial start,send initial segment,session %s:%d\n",rds.m_tdsSession->pTcpSession->remoteIP,rds.m_tdsSession->pTcpSession->remotePort);
			LOG(szLog);
			prepareOutFmtCtx();
			prepareOutCodecCtx();
			curPts = 0;
			curPktPts =  0;
			AVDictionary *opt = NULL;
			//设置媒体文件的视频帧率信息
			//use 'frag_every_frame' instead of 'frag_keyframe' or it will cause latency of the frame numbers in one frag
			av_dict_set(&opt, "movflags", "frag_every_frame+default_base_moof", 0);
			int whRet = avformat_write_header(destFmtCtx, &opt);
			if(whRet < 0)
			{
				printf("can not write the header of the output file!\n");
				rds.m_bStart = false;
				continue;
			}
			rds.m_tdsSession->bInitSegSended = true;
			bInitSigment = true;
		}

		AVPacket* ppkt = NULL;
		lockToEncode.lock();
		int s = toEncodeList.size();
		if(toEncodeList.size()>0)
		{
			ppkt = toEncodeList.front();
			toEncodeList.pop();
		}
		lockToEncode.unlock();

		if(ppkt==NULL)
			continue;

		time_t ts, te;
		//time_t ts = timeopt::getTick();
		avcodec_send_packet(decodeSrcCtx, ppkt);
		
		while (0 == avcodec_receive_frame(decodeSrcCtx, srcFrame))
		{
			ts = timeopt::getTick();
			sws_scale(img_convert_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0,
				encodeDestCtx->height, destFrame->data, destFrame->linesize);
			te = timeopt::getTick();
			time_t timeCost_scale = te - ts;
			printf("timeCost_scale %I64d\n", timeCost_scale);

			//pts = n * (（1 / timbase）/ fps);
			destFrame->pts = curPts;
			curPts += ((srcFmtCtx->streams[0]->time_base.den / srcFmtCtx->streams[0]->time_base.num) / conf.frameRate);

			if (curPts > 0x1FFFFFFFF) //pts is a 33bit value. if it is overflow ,use the 33bit part,the encoder will deal with that
				curPts &= 0x1FFFFFFFF;

			if (bInitSigment)
				destFrame->pict_type = AV_PICTURE_TYPE_I;

			int got_picture = 0;
			AVPacket pkt;
			av_init_packet(&pkt);

			pkt.data = NULL;
			pkt.size = 0;
			avcodec_send_frame(encodeDestCtx, destFrame);
			while (true)
			{
				int iret = avcodec_receive_packet(encodeDestCtx, &pkt);
				if (iret != 0)break;
				pkt.stream_index = VideoIndex;
				//pkt.pts = av_rescale_q_rnd(pkt.pts, srcFmtCtx->streams[0]->time_base, 
				//	destFmtCtx->streams[VideoIndex]->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));  
				pkt.pts = curPktPts;
				//pkt.dts = av_rescale_q_rnd(pkt.dts,  srcFmtCtx->streams[0]->time_base, 
				//	destFmtCtx->streams[VideoIndex]->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));  
				pkt.dts = curPktPts;
				//pkt.duration = ((destFmtCtx->streams[0]->time_base.den / destFmtCtx->streams[0]->time_base.num) / conf.frameRate);
				pkt.duration = 1000 / conf.frameRate;
				//printf("pts=%I64d,dts=%I64d,duration=%I64d\r\n",pkt.pts,pkt.dts,pkt.duration);
				curPktPts += 1000 / conf.frameRate;
				if (curPktPts > 0x1FFFFFFFF)
				{
					rds.restartStream();
				}
				int64_t cur_pts_v = pkt.pts;

				if (bInitSigment)
					LOG("[debug][rds]packet with init segment sended");
				ts = timeopt::getTick();
				int ret = av_interleaved_write_frame(destFmtCtx, &pkt);
				te = timeopt::getTick();
				time_t timeCost_writeFrame = te - ts;
				printf("timeCost_writeFrame %I64d\n", timeCost_writeFrame);
				time_t tWriteDestFmt = timeopt::getTick();
				printf("dest fmt writed %I64d\n", tWriteDestFmt);
				av_packet_unref(&pkt);
			}
		}

		//time_t te = timeopt::getTick();
		//time_t timeCost_decode = te-ts;
		//printf("timeCost_decode %I64d\n",timeCost_decode);

		
		av_packet_free(&ppkt);
	}
	av_frame_free(&srcFrame);
	av_frame_free(&destFrame);
	return 0;
}



int captureThread_s(remoteDesktopServer* pRDS)
{
	return pRDS->captureThread();
}

int encodeThread_s(remoteDesktopServer* pRDS)
{
	return pRDS->encodeThread();
}

 
void remoteDesktopServer::run()
{
	//av_register_all();
	avdevice_register_all();
	

	OpenVideoCapture();
	prepareOutCodecCtx();
	prepareOutFmtCtx();
	std::thread t(captureThread_s,this);
	std::thread t1(encodeThread_s,this);
	t.detach();
	t1.detach();
}


void remoteDesktopServer::startStream(shared_ptr<TDS_SESSION> tdsSession){
	std::lock_guard<mutex> g(m_sessionLock);
	m_tdsSession = tdsSession;
	m_tdsSession->bInitSegSended = false;
	m_bStart = true;
}

void remoteDesktopServer::restartStream()
{
	std::lock_guard<mutex> g(m_sessionLock);
	m_tdsSession->bInitSegSended = false;
}

void remoteDesktopServer::stopStream(){
	m_bStart = false;
}

#endif