#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

	// SDL define main and we should handle it with macro
#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

#include <stdio.h>
}

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

const size_t VIDEO_PICTURE_QUEUE_SIZE = 1;

struct PacketQueue {
	AVPacketList* first_pkt;
	AVPacketList* last_pkt;
	int nb_packets;
	int size;
	SDL_mutex* mutex;
	SDL_cond* cond;
};

struct myFrame {
	AVFrame* frame;
};

struct myFrameQueue {
	myFrame queue[VIDEO_PICTURE_QUEUE_SIZE];
	size_t size;
	size_t read_index;
	size_t write_index;
	SDL_mutex* mutex;
	SDL_cond* cond;
};

struct VideoState {
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

	myFrameQueue pFrameQ;
	
	SDL_Thread* parse_tid;
	SDL_Thread* video_tid;

	char filename[1024];
	int quit;
};

SDL_Window* screen;
SDL_mutex* screen_mutex;

VideoState* global_video_state;

void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue* q, AVPacket* pkt) {
	return 0;
}

int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block) {
	AVPacketList* pkt1;
	int ret;
	
	return ret;
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque) {
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);

	// stop timer
	return 0;
}

static void schedule_refresh(VideoState* is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState* is) {

}

void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;

	if (is->video_st) {
		if (is->pFrameQ.size == 0) {
			schedule_refresh(is, 1);
		}
		else{
			schedule_refresh(is, 40);

			// show the picture
			video_display(is);

			// update queue for next picture
			if (++is->pFrameQ.read_index == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pFrameQ.read_index = 0;
			}
			SDL_LockMutex(is->pFrameQ.mutex);
			is->pFrameQ.size--;
			SDL_CondSignal(is->pFrameQ.cond);
			SDL_UnlockMutex(is->pFrameQ.mutex);
		}
	}
	else{
		schedule_refresh(is, 100);
	}
}

int video_thread(void* arg) {
	return 0;
}

int stream_component_open(VideoState* is, int stream_index) {
	return 0;
}

int decode_thread(void* arg) {
	return 0;
}

void checkInit();

const char* SRC_FILE = "test3.mp4";

int main() {
	

	return 0;
}

void checkInit() {
	if (SDL_Init(SDL_INIT_VIDEO)) {
		std::cout << "SDL init Failed." << std::endl;
	}
	else {
		std::cout << "SDL init succeed." << std::endl;
	}

	std::cout << avcodec_configuration() << std::endl;
}