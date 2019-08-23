#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/avstring.h"

	// SDL define main and we should handle it with macro
#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

#include <stdio.h>
#include <math.h>
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
	size_t max_size;
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
static SDL_Renderer* renderer;
SDL_Texture* texture;

VideoState* global_video_state;

// set up YV12 pixel array
size_t yPlaneSz;
size_t uvPlaneSz;
Uint8* yPlane;
Uint8* uPlane;
Uint8* vPlane;
int uvPitch;
AVFrame pict;


void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));

	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue* q, AVPacket* pkt) {
	AVPacketList* pkt1;
	if (av_packet_make_refcounted(pkt) < 0) {
		return -1;
	}

	pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pkt1) {
		return -1;
	}

	av_packet_ref(&pkt1->pkt, pkt);
	//pkt1->pkt = *pkt;
	pkt1->next = nullptr;

	SDL_LockMutex(q->mutex);

	// insert to the end of the queue, update last_pkt
	if (!q->last_pkt) {
		// if this is the first pkt, update first_pkt also
		q->first_pkt = pkt1;
	}
	else {
		q->last_pkt->next = pkt1;
	}
	q->last_pkt = pkt1;
	++(q->nb_packets);
	q->size += pkt1->pkt.size;

	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
	return 0;
}

int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block) {
	AVPacketList* pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {
		if (global_video_state->quit) {
			ret = -1;
			break;
		}

		// get the first element in queue, update pointers
		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt) {
				q->last_pkt = nullptr;
			}
			--(q->nb_packets);
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);

	return ret;
}

static int frame_queue_init(myFrameQueue* fq) {
	memset(fq, 0, sizeof(myFrameQueue));
	fq->mutex = SDL_CreateMutex();
	if (!fq->mutex) {
		std::cout << "cannot create mutex" << std::endl;
		return -1;
	}

	fq->cond = SDL_CreateCond();
	if (!fq->cond) {
		std::cout << "cannot create cond" << std::endl;
		return -1;
	}

	fq->max_size = VIDEO_PICTURE_QUEUE_SIZE;
	fq->write_index = 0;
	fq->read_index = 0;
	fq->size = 0;

	std::cout << "max size: " << fq->max_size << std::endl;

	for (size_t i = 0; i < fq->max_size; ++i) {
		fq->queue[i].frame = av_frame_alloc();
		if (!fq->queue[i].frame) {
			std::cout << "cannot allocate frame at frame_queue_init" << std::endl;
			return -1;
		}
	}

	return 0;
}

myFrame* frame_queue_peek_last(myFrameQueue* f) {
	return &f->queue[f->read_index];
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
	myFrame* vp;
	//AVFrame* frame;
	SDL_Rect rect;
	float aspect_ratio;
	int ww;
	int wh;
	int w;
	int h;
	int x;
	int y;

	//frame = av_frame_alloc();

	vp = frame_queue_peek_last(&is->pFrameQ);
	//av_frame_move_ref(frame, vp->frame);
	//av_frame_unref(vp->frame);

	if (is->video_ctx->sample_aspect_ratio.num == 0) {
		aspect_ratio = 0;
	}
	else {
		aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio)
			* is->video_ctx->width / is->video_ctx->height;
	}
	if (aspect_ratio <= 0.0) {
		aspect_ratio = (float)is->video_ctx->width
			/ (float)is->video_ctx->height;
	}

	SDL_GetWindowSize(screen, &ww, &wh);
	h = wh;
	w = ((int)rint(h * aspect_ratio)) & -3;

	if (w > ww) {
		w = ww;
		h = ((int)rint(w / aspect_ratio)) & -3;
	}

	x = (ww - w) / 2;
	y = (wh - h) / 2;

	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;

	// convert to YUV
	sws_scale(
		is->sws_ctx,
		(uint8_t const* const*)vp->frame->data,
		vp->frame->linesize,
		0,
		is->video_ctx->height,
		pict.data,
		pict.linesize
	);

	// play with rect
	SDL_UpdateYUVTexture(
		texture,
		nullptr,
		yPlane,
		is->video_ctx->width,
		uPlane,
		uvPitch,
		vPlane,
		uvPitch
	);
	SDL_LockMutex(screen_mutex);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, nullptr, &rect);
	SDL_RenderPresent(renderer);
	//av_frame_free(&frame);
}

void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;

	if (is->video_st) {
		if (is->pFrameQ.size == 0) {
			schedule_refresh(is, 1);
		}
		else {
			schedule_refresh(is, 40);

			// show the picture
			video_display(is);

			// update queue for next picture


			av_frame_unref(is->pFrameQ.queue[is->pFrameQ.read_index].frame);
			if (++(is->pFrameQ.read_index) == is->pFrameQ.max_size) {
				is->pFrameQ.read_index = 0;
			}
			SDL_LockMutex(is->pFrameQ.mutex);
			is->pFrameQ.size--;
			SDL_CondSignal(is->pFrameQ.cond);
			SDL_UnlockMutex(is->pFrameQ.mutex);
		}
	}
	else {
		schedule_refresh(is, 100);
	}
}

int queue_picture(VideoState* is, AVFrame* pFrame) {
	myFrame* vp;

	// wait until have place to write
	SDL_LockMutex(is->pFrameQ.mutex);
	while (is->pFrameQ.size >= is->pFrameQ.max_size
		&& !is->quit) {
		SDL_CondWait(is->pFrameQ.cond, is->pFrameQ.mutex);
	}
	SDL_UnlockMutex(is->pFrameQ.mutex);

	vp = &is->pFrameQ.queue[is->pFrameQ.write_index];

	if (is->quit) {
		return -1;
	}

	av_frame_move_ref(vp->frame, pFrame);

	// push frame queue
	if (++(is->pFrameQ.write_index) == is->pFrameQ.max_size) {
		is->pFrameQ.write_index = 0;
	}
	SDL_LockMutex(is->pFrameQ.mutex);
	++is->pFrameQ.size;
	SDL_UnlockMutex(is->pFrameQ.mutex);

	return 0;
}

int video_thread(void* arg) {
	VideoState* is = (VideoState*)arg;
	AVPacket pkt1;
	AVPacket* packet = &pkt1;
	int frameFinished;
	AVFrame* pFrame;

	pFrame = av_frame_alloc();

	int ret = 0;

	for (;;) {
		ret = packet_queue_get(&is->video_queue, packet, 1);
		if (ret < 0) {
			// quit getting packets
			break;
		}
		// decode video frame
		avcodec_send_packet(is->video_ctx, packet);
		frameFinished = avcodec_receive_frame(is->video_ctx, pFrame);
		if (frameFinished == 0) {
			//queue frame to frame queue
			queue_picture(is, pFrame);
			
		}
		av_frame_unref(pFrame);

		av_packet_unref(packet);
	}

	av_frame_free(&pFrame);

	return 0;
}

int stream_component_open(VideoState* is, int stream_index) {
	AVFormatContext* pFormatCtx = is->pFormatCtx;
	AVCodecContext* codecCtx = nullptr;
	AVCodec* codec = nullptr;

	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) {
		std::cout << "stream index out of range" << std::endl;
		return -1;
	}

	codecCtx = avcodec_alloc_context3(nullptr);
	if (codecCtx == nullptr) {
		std::cout << "cannot allocate codec" << std::endl;
		return -1;
	}

	avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar);

	// find decoder
	codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec) {
		std::cout << "cannot find codec" << std::endl;
		return -1;
	}

	// open codec
	if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
		std::cout << "cannot open codec" << std::endl;
		return -1;
	}

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		is->video_st_index = stream_index;
		is->video_st = pFormatCtx->streams[stream_index];
		is->video_ctx = codecCtx;
		frame_queue_init(&is->pFrameQ);
		packet_queue_init(&is->video_queue);
		is->video_tid = SDL_CreateThread(video_thread, "video_t", is);
		is->sws_ctx = sws_getContext(
			is->video_ctx->width,
			is->video_ctx->height,
			is->video_ctx->pix_fmt,
			is->video_ctx->width,
			is->video_ctx->height,
			AV_PIX_FMT_YUV420P,
			SWS_BILINEAR,
			nullptr,
			nullptr,
			nullptr
		);
		texture = SDL_CreateTexture(
			renderer,
			SDL_PIXELFORMAT_YV12,
			SDL_TEXTUREACCESS_STREAMING,
			is->video_ctx->width,
			is->video_ctx->height
		);
		// set up YV12 pixel array
		yPlaneSz = is->video_ctx->width * is->video_ctx->height;
		uvPlaneSz = is->video_ctx->width * is->video_ctx->height / 4;
		yPlane = new Uint8[yPlaneSz];
		uPlane = new Uint8[uvPlaneSz];
		vPlane = new Uint8[uvPlaneSz];

		uvPitch = is->video_ctx->width / 2;

		pict.data[0] = yPlane;
		pict.data[1] = uPlane;
		pict.data[2] = vPlane;
		pict.linesize[0] = is->video_ctx->width;
		pict.linesize[1] = uvPitch;
		pict.linesize[2] = uvPitch;
		if (!texture) {
			std::cout << "SDL: cannot create texture" << std::endl;
			return -1;
		}
		break;
	default:
		break;
	}

	return 0;
}

int decode_thread(void* arg) {
	VideoState* is = (VideoState*)arg;
	AVFormatContext* pFormatCtx = nullptr;
	AVPacket pkt1;
	AVPacket* packet = &pkt1;

	int video_index = -1;

	is->video_st_index = -1;

	global_video_state = is;

	// open file
	if (avformat_open_input(&pFormatCtx, is->filename, nullptr, nullptr) != 0) {
		std::cout << "Av open input failed" << std::endl;
		goto fail;
	}

	is->pFormatCtx = pFormatCtx;

	// retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		return -1;
	}

	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// find first stream available
	for (int i = 0; i < pFormatCtx->nb_streams; ++i) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
			&& video_index < 0) {
			video_index = i;
		}
	}

	if (video_index >= 0) {
		int flag = stream_component_open(is, video_index);
		if (flag == -1) {
			goto fail;
		}
	}

	if (is->video_st_index < 0) {
		std::cout << "FFmpeg: cannot open codec" << std::endl;
		goto fail;
	}

	for (;;) {
		if (is->quit) {
			break;
		}

		if (is->video_queue.size > MAX_VIDEOQ_SIZE) {
			SDL_Delay(10);
			continue;
		}

		if (av_read_frame(is->pFormatCtx, packet) < 0) {
			if (is->pFormatCtx->pb->error == 0) {
				// no error, wait for input
				SDL_Delay(100);
				continue;
			}
			else {
				break;
			}
		}

		// send packet to queue
		if (packet->stream_index == is->video_st_index) {
			packet_queue_put(&is->video_queue, packet);
		}
		else {
			av_packet_unref(packet);
		}
	}

	while (!is->quit) {
		SDL_Delay(100);
	}

fail:
	SDL_Event sdl_event;
	sdl_event.type = FF_QUIT_EVENT;
	sdl_event.user.data1 = is;
	SDL_PushEvent(&sdl_event);

	return 0;
}

static void event_loop(VideoState* is) {
	SDL_Event sdl_event;

	for (;;) {
		SDL_WaitEvent(&sdl_event);
		switch (sdl_event.type) {
		case FF_QUIT_EVENT:
		case SDL_QUIT:
			is->quit = 1;
			SDL_CondSignal(is->video_queue.cond);
			SDL_Quit();
			break;
		case FF_REFRESH_EVENT:
			video_refresh_timer(sdl_event.user.data1);
			break;
		default:
			break;
		}
	}
}

void checkInit();

const char* SRC_FILE = "test3.mpg";

int main() {
	VideoState* is;
	is = (VideoState*)av_mallocz(sizeof(VideoState));

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "SDL init failed" << std::endl;
		return -1;
	}

	screen = SDL_CreateWindow(
		"SimplePlayer32",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		640,
		480,
		0
	);

	if (!screen) {
		std::cout << "SDL: couldn't create window" << std::endl;
		return -1;
	}

	renderer = SDL_CreateRenderer(screen, -1, 0);

	if (!renderer) {
		std::cout << "SDL: couldn't create renderer" << std::endl;
		return -1;
	}

	screen_mutex = SDL_CreateMutex();

	av_strlcpy(is->filename, SRC_FILE, sizeof(is->filename));

	std::cout << is->filename << std::endl;

	schedule_refresh(is, 40);

	is->parse_tid = SDL_CreateThread(decode_thread, "decode_t", is);
	if (!is->parse_tid) {
		av_free(is);
		return -1;
	}

	if (!renderer) {
		std::cout << "SDL: couldn't create renderer" << std::endl;
	}

	event_loop(is);

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