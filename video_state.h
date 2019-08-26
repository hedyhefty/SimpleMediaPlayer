#pragma once
#include "util.h"

class VideoState {
public:
	VideoState() :
		filename(""),
		pFrameQ(),
		video_queue(),
		audio_queue(),
		pFormatCtx(nullptr),
		video_st_index(-1),
		audio_st_index(-1),
		audio_buf_size(0),
		audio_buf_index(0),
		audio_pkt_size(0),
		audio_pkt_data(nullptr),
		audio_frame(),
		audio_pkt(),
		video_st(nullptr),
		audio_st(nullptr),
		sws_ctx(nullptr),
		demux_tid(nullptr),
		video_tid(nullptr),
		quit(0) {
		// allocate video_ctx & audio_ctx
		video_ctx = avcodec_alloc_context3(nullptr);
		audio_ctx = avcodec_alloc_context3(nullptr);

	}

	~VideoState() {
		// free video_ctx & audio_ctx
		avcodec_close(video_ctx);
		avcodec_free_context(&video_ctx);

		avcodec_close(audio_ctx);
		avcodec_free_context(&audio_ctx);

		avformat_close_input(&pFormatCtx);
	}

	AVFormatContext* pFormatCtx;

	int audio_st_index;
	AVStream* audio_st;
	AVCodecContext* audio_ctx;
	PacketQueue audio_queue;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	size_t audio_buf_size;
	size_t audio_buf_index;
	AVFrame audio_frame;
	AVPacket audio_pkt;
	uint8_t* audio_pkt_data;
	int audio_pkt_size;


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