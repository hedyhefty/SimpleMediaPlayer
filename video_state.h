#pragma once
#include "util.h"

class VideoState {
public:
	VideoState() :
		filename(""),
		pFrameQ(),
		video_queue(),
		pFormatCtx(nullptr),
		video_st_index(0),
		video_st(nullptr),
		sws_ctx(nullptr),
		demux_tid(nullptr),
		video_tid(nullptr),
		quit(0) {
		video_ctx = avcodec_alloc_context3(nullptr);
	}

	~VideoState() {
		delete pFormatCtx;
		delete video_st;
		avcodec_free_context(&video_ctx);
	}

	AVFormatContext* pFormatCtx;

	/*int audio_st_index;
	AVStream* audio_st;
	AVCodecContext* audio_ctx;
	PacketQueue audio_queue;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audio_buf_size;
	unsigned int audio_buf_index;
	AVFrame audio_frame;
	AVPacket audio_pkt_data;*/

	int video_st_index;
	AVStream* video_st;
	AVCodecContext* video_ctx;
	PacketQueue video_queue;
	SwsContext* sws_ctx;
	SDL_Rect rect;  //output rectangle
	YUVDisplayPar yuv_display;

	myFrameQueue pFrameQ;

	SDL_Thread* demux_tid;
	SDL_Thread* video_tid;

	char filename[1024];
	int quit;
};