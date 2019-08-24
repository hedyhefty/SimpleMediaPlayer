#include "util.h"


SDL_Window* screen;
static SDL_Renderer* renderer;
SDL_Texture* texture;

VideoState* global_video_state;

void init_calculate_rect(VideoState* is);

Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque);

void schedule_refresh(VideoState* is, int delay);

void video_display(VideoState* is);

void video_refresh_timer(void* userdata);

int queue_picture(VideoState* is, AVFrame* pFrame);

int video_thread(void* arg);

int stream_component_open(VideoState* is, int stream_index);

int demux_thread(void* arg);

static void event_loop(VideoState* is);

const char* SRC_FILE = "test4.mp4";

int main() {
	VideoState* is = new VideoState;

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
		std::cout << "SDL: cannot create window" << std::endl;
		return -1;
	}

	renderer = SDL_CreateRenderer(screen, -1, 0);

	if (!renderer) {
		std::cout << "SDL: cannot create renderer" << std::endl;
		return -1;
	}

	av_strlcpy(is->filename, SRC_FILE, sizeof(is->filename));

	std::cout << is->filename << std::endl;

	schedule_refresh(is, 40);

	is->demux_tid = SDL_CreateThread(demux_thread, "demux_t", is);
	if (!is->demux_tid) {
		av_free(is);
		return -1;
	}

	if (!renderer) {
		std::cout << "SDL: cannot create renderer" << std::endl;
	}

	event_loop(is);	

	int a;
	std::cin >> a;

	return 0;
}

void init_calculate_rect(VideoState* is) {
	double aspect_ratio;
	int ww;
	int wh;
	int w;
	int h;
	int x;
	int y;

	if (is->video_ctx->sample_aspect_ratio.num == 0) {
		aspect_ratio = 0;
	}
	else {
		aspect_ratio = av_q2d(is->video_ctx->sample_aspect_ratio)
			* is->video_ctx->width / is->video_ctx->height;
	}
	if (aspect_ratio <= 0.0) {
		aspect_ratio = (double)is->video_ctx->width
			/ (double)is->video_ctx->height;
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

	is->rect.x = x;
	is->rect.y = y;
	is->rect.w = w;
	is->rect.h = h;
}

Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque) {
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);

	// stop timer
	return 0;
}

void schedule_refresh(VideoState* is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void video_display(VideoState* is) {
	myFrame* vp;
	//AVFrame* frame;
	//frame = av_frame_alloc();

	vp = frame_queue_get_last_ref(&is->pFrameQ);
	//av_frame_move_ref(frame, vp->frame);
	//av_frame_unref(vp->frame);

	// convert to YUV
	sws_scale(
		is->sws_ctx,
		(uint8_t const* const*)vp->frame->data,
		vp->frame->linesize,
		0,
		is->video_ctx->height,
		is->yuv_display.pict.data,
		is->yuv_display.pict.linesize
	);

	// play with rect
	SDL_UpdateYUVTexture(
		texture,
		nullptr,
		is->yuv_display.yPlane,
		is->video_ctx->width,
		is->yuv_display.uPlane,
		is->yuv_display.uvPitch,
		is->yuv_display.vPlane,
		is->yuv_display.uvPitch
	);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, nullptr, &is->rect);
	SDL_RenderPresent(renderer);

	if (vp->lastframe_flag) {
		SDL_Event sdl_event;
		sdl_event.type = FF_QUIT_EVENT;
		sdl_event.user.data1 = is;
		SDL_Delay(100);
		SDL_PushEvent(&sdl_event);
	}
	//av_frame_free(&frame);
}

void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;

	if (is->video_st) {
		if (is->pFrameQ.size == 0) {
			schedule_refresh(is, 1);
		}
		else {
			schedule_refresh(is, 20);

			// show the picture
			video_display(is);

			// update queue for next picture
			frame_queue_dequeue(&is->pFrameQ);
		}
	}
	else {
		schedule_refresh(is, 100);
	}
}

int queue_picture(VideoState* is, AVFrame* pFrame) {
	myFrame* vp;

	// wait until have place to write
	vp = frame_queue_writablepos_ref(&is->pFrameQ);

	if (!vp || is->quit == 1) {
		return -1;
	}

	av_frame_move_ref(vp->frame, pFrame);

	if (is->video_queue.all_sent && is->video_queue.nb_packets == 0) {
		vp->lastframe_flag = true;
	}

	// push frame queue
	if (++(is->pFrameQ.write_index) == is->pFrameQ.max_size) {
		is->pFrameQ.write_index = 0;
	}
	SDL_LockMutex(is->pFrameQ.mutex);
	++is->pFrameQ.size;
	SDL_CondSignal(is->pFrameQ.cond);
	SDL_UnlockMutex(is->pFrameQ.mutex);

	if (is->quit == 1) {
		return -1;
	}

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
		if (is->quit == 1) {
			break;
		}

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

		if (is->video_queue.all_sent && is->video_queue.nb_packets == 0) {
			break;
		}
	}

	av_frame_free(&pFrame);

	std::cout << "video thread return" << std::endl;

	return 0;
}

int stream_component_open(VideoState* is, int stream_index) {
	AVFormatContext* pFormatCtx = is->pFormatCtx;
	AVCodecContext* codecCtx = nullptr;
	AVCodec* codec = nullptr;

	if (stream_index < 0 || stream_index >= (int)pFormatCtx->nb_streams) {
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
		//frame_queue_init(&is->pFrameQ);
		//packet_queue_init(&is->video_queue);
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

		// init parameters for yuv image display
		init_YUV_display_par(is);

		// init display rectangle size
		init_calculate_rect(is);
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

int demux_thread(void* arg) {
	VideoState* is = (VideoState*)arg;
	AVFormatContext* pFormatCtx = nullptr;
	AVPacket pkt1;
	AVPacket* packet = &pkt1;

	int video_index = -1;

	is->video_st_index = -1;

	global_video_state = is;
	quit_ref = &global_video_state->quit;

	//int i = 0;

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
	for (size_t i = 0; i < pFormatCtx->nb_streams; ++i) {
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
		if (is->quit == 1) {
			break;
		}

		

		if (is->video_queue.size > MAX_VIDEOQ_SIZE) {
			SDL_Delay(10);
			continue;
		}
		//std::cout << i++ << std::endl;
		int ret = av_read_frame(is->pFormatCtx, packet);
		if ( ret < 0) {
			std::cout << ret << std::endl;
			//if (is->pFormatCtx->pb->error == 0) {
			//	// no error, wait for input
			//	SDL_Delay(1);
			//	continue;
			//}
			//else {
			//	break;
			//}
			is->video_queue.all_sent = true;
			break;
		}
		
		// send packet to queue
		if (packet->stream_index == is->video_st_index) {
			packet_queue_put(&is->video_queue, packet);
		}

		av_packet_unref(packet);
	}

	goto straight_return;

fail:
	SDL_Event sdl_event;
	sdl_event.type = FF_QUIT_EVENT;
	sdl_event.user.data1 = is;
	SDL_PushEvent(&sdl_event);

straight_return:
	std::cout << "demux thread return" << std::endl;
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
			SDL_CondSignal(is->pFrameQ.cond);
			std::cout << "signal sent" << std::endl;

			SDL_WaitThread(is->demux_tid, nullptr);
			SDL_WaitThread(is->video_tid, nullptr);
			
			SDL_DestroyTexture(texture);
			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(screen);
			SDL_Quit();

			global_video_state = nullptr;
			delete is;

			std::cout << "exit" << std::endl;

			goto exit;
			break;
		case FF_REFRESH_EVENT:
			video_refresh_timer(sdl_event.user.data1);
			break;
		default:
			break;
		}
	}

exit:
	return;
}