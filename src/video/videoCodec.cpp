#include "pch.h"
#ifdef ENABLE_FFMPEG
#include "videoCodec.h"
#include "video/ffmpeg.h"
#include <thread>
#include <time.h>
#include <chrono>
#include <queue>
#include "common.h"


int write_data_videoCodec(void* opaque, unsigned char* buf, int buf_size)
{
	videoCodec* pVc = (videoCodec*)opaque;
	if (pVc->bHeaderCallBack)
	{
		memcpy(pVc->headerBuff, buf, buf_size);
		pVc->iHeaderBuffLen = buf_size;
		pVc->bHeaderCallBack = false;
	}
	else
	{
		memcpy(pVc->outputBuff + pVc->iOutputLen, buf, buf_size);
		pVc->iOutputLen += buf_size;
	}
	return buf_size;
}

videoCodec::~videoCodec()
{
	if (encodeCtx)
		avcodec_free_context(&encodeCtx);
	if (outFmtCtx)
		avformat_free_context(outFmtCtx);
	if(frame_convert_ctx)
		sws_freeContext(frame_convert_ctx);
}


//use AV_CODEC_ID_VP9 or AV_CODEC_ID_H264
bool videoCodec::init_encodeCtx()
{
	if(encodeCtx)
		avcodec_free_context(&encodeCtx);
	AVCodec *pCodecH264 = avcodec_find_encoder((AVCodecID)outConf.codecID);
	if (!pCodecH264)
	{
		fprintf(stderr, "h264 codec not found\n");
		exit(1);
	}
	encodeCtx = avcodec_alloc_context3(pCodecH264);
	encodeCtx->bit_rate = outConf.bitRate;//put sample parameters
	encodeCtx->width = outConf.width;
	encodeCtx->height = outConf.height;
	encodeCtx->gop_size = 50;
	encodeCtx->time_base.num = 1;
	encodeCtx->time_base.den = outConf.frameRate;
	encodeCtx->max_b_frames = 0;
	encodeCtx->thread_count = 0;
	encodeCtx->delay = 0;
	encodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;//PIX_FMT_RGB24;
	//if(destFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
	encodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	//av_opt_set(c->priv_data, /*"preset"*/"libvpx-1080p.ffpreset", /*"slow"*/NULL, 0);
	//real-time streaming (for mpeg-4/h264 encoder)
	//av_opt_set(encodeCtx->priv_data,"preset","ultrafast",0);
	//av_opt_set(encodeCtx->priv_data, "tune", "zerolatency", 0);
	//real-time streaming (for webm vp9 encoder)  see https://developers.google.com/media/vp9/live-encoding
	av_opt_set(encodeCtx->priv_data,"lag-in-frames","0",0);
	av_opt_set(encodeCtx->priv_data,"quality","realtime",0);
	av_opt_set(encodeCtx->priv_data,"speed","8",0);
	if (avcodec_open2(encodeCtx, pCodecH264,NULL) < 0) {
		printf("avcodec_open2 failed\n");
		exit(1);
	}

	return true;
}
int videoCodec::init_outFmtCtx()
{
	if(outFmtCtx)
		avformat_free_context(outFmtCtx);
	AVStream *destStream = NULL;
	AVIOContext* o_ioctx = NULL;
	if (outConf.codecID == AV_CODEC_ID_H264)
	{
		avformat_alloc_output_context2(&outFmtCtx, NULL, "mp4", NULL); //mp4 or webm
		/* open the output file, if needed */
		if (!(outFmtCtx->flags & AVFMT_NOFILE)) {
			int ret = avio_open(&outFmtCtx->pb, outConf.filename.c_str(), AVIO_FLAG_WRITE);
			if (ret < 0) {
				char sError[80] = { 0 };
				av_strerror(ret, sError, 80);
				string strError = sError;
				fprintf(stderr, "Could not open '%s': %s\n", outConf.filename.c_str(), strError.c_str());
				return 1;
			}
		}
	}
	else if (outConf.codecID == AV_CODEC_ID_VP9)
	{
		avformat_alloc_output_context2(&outFmtCtx, NULL, "webm", NULL);
		o_ioctx = avio_alloc_context((unsigned char*)output_buf, OUTPUT_BUFF_SIZE, 1, this, NULL, &write_data_videoCodec, NULL);
		outFmtCtx->pb = o_ioctx;
		outFmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
	}

	//prepare output format context , create stream
	destStream = avformat_new_stream(outFmtCtx, NULL);
	if (!destStream)
	{
		printf("can not new stream for output!\n");
		return -1;
	}
	destStream->codecpar->width = outConf.width;
	destStream->codecpar->height = outConf.height;
	destStream->codecpar->format = AV_PIX_FMT_YUV420P;
	destStream->codecpar->bit_rate = outConf.bitRate;
	destStream->codecpar->codec_id = (AVCodecID)outConf.codecID;
	destStream->codecpar->codec_tag = 0;
	destStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	//因为mpeg的pts、dts都是以90kHz来采样的，所以采样间隔为1/90000秒
	destStream->time_base.num = 1;  //numerator 分子 
	destStream->time_base.den = 90000; //denominator 分母
	
 	//av_dump_format(outFmtCtx, 0, NULL, 1);
	return 0;
}
void videoCodec::init()
{
	avdevice_register_all();

	curPts = 0;
	iFrameIdx = 0;
}
void videoCodec::input_Bmp(char* pBMP, int nBMPLen)
{
	//fs::writeFile(fs::appPath() + "\\test.bmp", pBMP, nBMPLen);
	if (!bInit)
	{
		init();
		bInit = true;
	}

	BITMAPFILEHEADER* pFhSrc = (BITMAPFILEHEADER*)pBMP;
	BITMAPINFOHEADER* pIhSrc = (BITMAPINFOHEADER*)(pBMP + sizeof(BITMAPFILEHEADER));

	inConf.width = pIhSrc->biWidth;
	inConf.height = pIhSrc->biHeight;
	outConf.width = pIhSrc->biWidth;
	outConf.height = pIhSrc->biHeight;
	if(!encodeCtx)
		init_encodeCtx();
	if (!outFmtCtx)
		init_outFmtCtx();
	if (!frame_convert_ctx)
	{
		frame_convert_ctx = sws_getContext(inConf.width, inConf.height, inConf.pixelFmt,
			outConf.width, outConf.height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	}
	

	AVFrame* p = av_frame_alloc();
	av_image_fill_arrays(p->data, p->linesize, (const uint8_t*)pBMP + pFhSrc->bfOffBits, inConf.pixelFmt, inConf.width, inConf.height, 1);
	p->width = inConf.width;
	p->height = inConf.height;
	p->format = inConf.pixelFmt;

	//翻转数据
	//p->data[0] += p->linesize[0] * (outConf.height - 1);
	//p->linesize[0] *= -1;
	//p->data[1] += p->linesize[1] * (outConf.height / 2 - 1);
	//p->linesize[1] *= -1;
	//p->data[2] += p->linesize[2] * (outConf.height / 2 - 1);
	//p->linesize[2] *= -1;

	frameList.push(p);
}

void videoCodec::finishOutput()
{
	av_write_trailer(outFmtCtx);

	if (!(outFmtCtx->flags & AVFMT_NOFILE))
		avio_closep(&outFmtCtx->pb);
}

void videoCodec::outputHeader()
{
	bHeaderCallBack = true;
	AVDictionary* opt = NULL;
	av_dict_set_int(&opt, "video_track_timescale", 25, 0);
	if (outConf.codecID == AV_CODEC_ID_VP9)
	{
		//use 'frag_every_frame' instead of 'frag_keyframe' or it will cause latency of the frame numbers in one frag
		av_dict_set(&opt, "movflags", "frag_every_frame+default_base_moof", 0);
	}
	//avformat_write_header will set time_base to 1/1000 when codec is AV_CODEC_ID_VP9 whatever video_track_timescale value is 
	//avformat_write_header will set time_base to 1/25 when codec is  AV_CODEC_ID_H264
	int whRet = avformat_write_header(outFmtCtx, &opt);
	if (whRet < 0)
	{
	}
}

void videoCodec::output()
{
	AVFrame* p = NULL;

	if (!headWrited)
	{
		outputHeader();
		headWrited = true;
	}
	
	while (frameList.size() > 0)
	{
		p = frameList.front();
		frameList.pop();

		//init outFrame buffer
		AVFrame* outFrame = av_frame_alloc();
		// yuv size is half size of rgb24
		int size = av_image_get_buffer_size(encodeCtx->pix_fmt, encodeCtx->width, encodeCtx->height, 1);
		uint8_t* outFrameBuff = new uint8_t[size];
		av_image_fill_arrays(outFrame->data, outFrame->linesize, outFrameBuff, encodeCtx->pix_fmt, encodeCtx->width, encodeCtx->height, 1);
		outFrame->width = encodeCtx->width;
		outFrame->height = encodeCtx->height;
		outFrame->format = encodeCtx->pix_fmt;

		//转yuv
		sws_scale(frame_convert_ctx, (const uint8_t* const*)p->data, p->linesize, 0,
			encodeCtx->height, outFrame->data, outFrame->linesize);

		outFrame->pts = curPts; //Presentation Time Stamp。PTS主要用于度量解码后的视频帧什么时候被显示出来。单位是 time_base.   pts*time_base为真实显示时间，单位秒。例如 3*(1/25)
		curPts += 1;
		avcodec_send_frame(encodeCtx, outFrame);

		AVPacket pkt;
		av_init_packet(&pkt);
		pkt.data = NULL;
		pkt.size = 0;
		
		while (true)
		{
			int iret = avcodec_receive_packet(encodeCtx, &pkt);
			if (iret != 0)break;
			pkt.stream_index = 0;
			AVStream* pStream = outFmtCtx->streams[0];
			//(iFrameIdx*(1/25))/stream中的timebase
			pkt.pts = (iFrameIdx * pStream->time_base.den) / (pStream->time_base.num * 25);
			iFrameIdx++;
			//fs::appendFile(fs::appPath() +"\\test.h264", (char*)pkt.data, pkt.size);
			int ret = av_interleaved_write_frame(outFmtCtx, &pkt);
			av_packet_unref(&pkt);
		}

		av_frame_free(&p);
		av_frame_free(&outFrame);
	}
}
#endif