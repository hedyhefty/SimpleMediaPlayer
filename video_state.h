#pragma once
#include "frame_queue.h"
#include "packet_queue.h"
#include "yuv_display_par.h"

constexpr auto MAX_AUDIO_FRAME_SIZE = 192000;

class VideoState {
public:
	VideoState();

	~VideoState();

	AVFormatContext* pFormatCtx = nullptr;

	// clock
	double base_time = 0;
	double time_stamp = 0;
	double speed_factor = 1;

	// for seek
	bool seek_req = false;
	int64_t seek_pos = 0;
	int64_t seek_rel = 0;
	int seek_flag = 0;
	bool video_pkt_flush = false;
	bool audio_pkt_flush = false;

	bool pframe_queue_flush = false;

	int audio_st_index = -1;
	SDL_AudioDeviceID dev_id = {};
	AVStream* audio_st = nullptr;
	AVCodecContext* audio_ctx = nullptr;
	PacketQueue audio_queue = {};
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2] = {};
	uint8_t audio_temp_buf[(MAX_AUDIO_FRAME_SIZE) * 3 / 2] = {};
	size_t audio_buf_size = 0;
	size_t audio_buf_index = 0;
	AVPacket audio_pkt = {};
	uint8_t* audio_pkt_data = nullptr;
	int audio_pkt_size = 0;
	SwrContext* swr_ctx;
	AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;
	AVFrame* audio_frame = nullptr;

	int video_st_index = 0;
	AVStream* video_st = nullptr;
	AVCodecContext* video_ctx = nullptr;
	PacketQueue video_queue = {};
	SwsContext* sws_ctx = nullptr;
	SDL_Rect rect = {};  //output rectangle
	YUVDisplayPar yuv_display = {};

	myFrameQueue pFrameQ = {};

	SDL_Thread* demux_tid = nullptr;
	SDL_Thread* video_tid = nullptr;

	char filename[1024] = {};
	int quit = 0;
};