#include "util.h"
#include <iostream>


SDL_Window* screen;
static SDL_Renderer* renderer;
SDL_Texture* texture;

VideoState* global_video_state;

void init_calculate_rect(VideoState* is);

double get_master_clock(VideoState* is);

double get_external_clock();

Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque);

void schedule_refresh(VideoState* is, int delay);

int audio_decode_frame(VideoState* is, uint8_t* audio_buf, int buf_size);

void audio_callback(void* userdata, Uint8* stream, int len);

void video_display(VideoState* is);

void video_refresh_timer(void* userdata);

int queue_picture(VideoState* is, AVFrame* pFrame);

int video_thread(void* arg);

int stream_component_open(VideoState* is, int stream_index);

int demux_thread(void* arg);

static void event_loop(VideoState* is);

const char* SRC_FILE = "test4.mpg";

int main() {
	VideoState* is = new VideoState;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "SDL init failed" << std::endl;
		return -1;
	}
	const char* dr = SDL_GetCurrentAudioDriver();
	std::cout << "Current SDL driver is: " << dr << std::endl;

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

	std::cout << "cin out signal...";
	int a;
	std::cin >> a;

	return 0;
}

void program_fail(VideoState* is) {
	SDL_Event sdl_event;
	sdl_event.type = FF_QUIT_EVENT;
	sdl_event.user.data1 = is;
	SDL_PushEvent(&sdl_event);
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

double get_master_clock(VideoState* is) {
	return get_external_clock();
}

double get_external_clock() {
	static double start_time = av_gettime();

	return (av_gettime() - start_time) / TIME_BASE;
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

int audio_decode_frame(VideoState* is, uint8_t* audio_buf, int buf_size) {

	int len1 = 0;
	int data_size = 0;
	AVPacket* pkt = &is->audio_pkt;

	for (;;) {
		int got_frame = avcodec_receive_frame(is->audio_ctx, &is->audio_frame);

		if (is->quit == 1) {
			return -1;
		}

		if (got_frame == 0) {
			data_size = av_samples_get_buffer_size(
				nullptr,
				is->audio_ctx->channels,
				is->audio_frame.nb_samples,
				is->audio_ctx->sample_fmt,
				1
			);
			assert(data_size <= buf_size);

			
			double pts = is->audio_frame.best_effort_timestamp;
			if (pts != AV_NOPTS_VALUE) {
				int delta_size;
				pts *= av_q2d(is->audio_st->time_base);
				double ref_time = get_master_clock(is);
				double diff = pts - ref_time;
				//std::cout << "diff: " << diff << std::endl;

				if (fabs(diff) > AV_SYNC_THRESHOLD) {
					std::cout << "tunning" << std::endl;
					int n = 2 * is->audio_ctx->channels;
					delta_size = int(diff * is->audio_ctx->sample_rate) * n;

					if (delta_size > 0) {
						if (delta_size > buf_size / 2) {
							delta_size = buf_size / 2 - data_size;
						}
						memcpy(audio_buf, is->audio_frame.data[0], data_size);
						for (size_t i = 0; i < delta_size; ++i) {
							memcpy(audio_buf + data_size + i, &is->audio_frame.data[0][data_size], 1);
						}

					}
					else {
						if (delta_size + data_size < data_size / 2) {
							delta_size = -data_size;
						}
						memcpy(audio_buf, is->audio_frame.data[0], data_size + delta_size);
					}

					return data_size + delta_size;
				}
			}

			memcpy(audio_buf, is->audio_frame.data[0], data_size);

			//is->audio_pkt_data += pkt->size;
			//is->audio_pkt_size -= pkt->size;

			/* We have data, return it and come back for more later */
			return data_size;
		}

		if (pkt->data) {
			av_packet_unref(pkt);
		}

		if (packet_queue_get(&is->audio_queue, pkt, 1) < 0) {
			return -1;
		}

		//is->audio_pkt_data = pkt->data;
		//is->audio_pkt_size = pkt->size;
		int decode_succeed = avcodec_send_packet(is->audio_ctx, pkt);
		if (decode_succeed < 0) {
			return -1;
		}

	}
}

void audio_callback(void* userdata, Uint8* stream, int len) {
	VideoState* is = (VideoState*)userdata;
	int len1 = 0;
	int audio_size = 0;

	while (len > 0) {
		if (is->quit == 1) {
			return;
		}

		if (is->audio_buf_index >= is->audio_buf_size) {
			audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));
			//std::wcout << audio_size << std::endl;

			// play silence when error
			if (audio_size < 0) {
				is->audio_buf_size = 1024;
				memset(is->audio_buf, 0, is->audio_buf_size);
			}
			else {
				is->audio_buf_size = audio_size;
			}

			is->audio_buf_index = 0;
		}

		len1 = is->audio_buf_size - is->audio_buf_index;
		if (len1 > len) {
			len1 = len;
		}
		auto temp = (uint8_t*)is->audio_buf + is->audio_buf_index;
		memcpy(stream, temp, len1);
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}

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
		std::cout << "play the last frame" << std::endl;
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
	static double previous_pts = 0;
	static double previous_time_delta = 0;
	double delta_time = 0;
	double pts = 0;

	if (is->video_st) {
		if (is->pFrameQ.size == 0) {
			schedule_refresh(is, 1);
		}
		else {
			/* pts here stand for the x'th frame of the video,
			not the time of it should be present */
			pts = frame_queue_get_pts(&is->pFrameQ);
			if (pts == AV_NOPTS_VALUE) {
				pts = previous_pts + previous_time_delta;
			}
			else {
				// turn pts into second form
				pts *= av_q2d(is->video_st->time_base);
				//std::cout << "pts in second's form: " << pts << std::endl;
			}

			delta_time = pts - previous_pts;
			if (delta_time <= 0 || delta_time >= 1.0) {
				delta_time = previous_time_delta;
			}

			previous_pts = pts;
			previous_time_delta = delta_time;

			// ref time in seconds
			double ref_time = get_master_clock(is);
			double diff = pts - ref_time;

			double time_per_frame = av_q2d(is->video_ctx->time_base);
			double repeat_coeff = frame_queue_get_repeat_coeff(&is->pFrameQ);
			double predict_delta = time_per_frame + repeat_coeff * (double)time_per_frame / 2 + diff;

			int predict_result = predict_delta * 1000;
			/*std::cout << "master clock: " << ref_time << std::endl;
			std::cout << "diff: " << diff << std::endl;
			std::cout << "predict delta: " << predict_result << std::endl;*/

			int tunned_result = predict_result < 0 ? 0 : predict_result;

			//std::cout << "tunned delta:" << tunned_result << std::endl;

			schedule_refresh(is, tunned_result);

			// show the picture
			video_display(is);
			//std::cout << "frame queue size:" << is->pFrameQ.size << std::endl;
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
		std::cout << "lastframe, queued." << std::endl;
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
		//std::cout << is->video_queue.nb_packets << std::endl;
		if (is->video_queue.all_sent && is->video_queue.nb_packets == 0) {
			std::cout << "???" << std::endl;
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
	SDL_AudioSpec wanted_spec;
	SDL_AudioSpec spec;


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

	// audio settings
	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
		// Reset codec to solve audio problem
		if (codecCtx->sample_fmt == AV_SAMPLE_FMT_S16P) {
			codecCtx->request_sample_fmt = AV_SAMPLE_FMT_S16;
		}

		//SDL_zero(wanted_spec);
		wanted_spec.freq = codecCtx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = codecCtx->channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;

		if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
			std::cout << "cannot open audio" << std::endl;
			return -1;
		}

		is->dev_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, SDL_AUDIO_NOT_ALLOW_ANY_CHANGE);
		if (is->dev_id == 0) {
			return -1;
		}
		if (wanted_spec.format != spec.format) {
			std::cout << "format missmatch" << std::endl;
			std::cout << wanted_spec.format << std::endl;
			std::cout << spec.format << std::endl;
		}
		//std::cout << dev << std::endl;
	}

	// open codec
	if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
		std::cout << "cannot open codec" << std::endl;
		return -1;
	}

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->audio_st_index = stream_index;
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_ctx = codecCtx;
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;

		//memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		//SDL_PauseAudio(0);
		SDL_PauseAudioDevice(is->dev_id, 0);
		break;

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
	int audio_index = -1;

	global_video_state = is;
	quit_ref = &global_video_state->quit;

	//int i = 0;

	// open file
	if (avformat_open_input(&pFormatCtx, is->filename, nullptr, nullptr) != 0) {
		std::cout << "Av open input failed" << std::endl;
		program_fail(is);
		return -1;
	}

	is->pFormatCtx = pFormatCtx;

	// retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		program_fail(is);
		return -1;
	}

	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// find first stream available
	for (size_t i = 0; i < pFormatCtx->nb_streams; ++i) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
			&& video_index < 0) {
			video_index = i;
		}

		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
			&& audio_index < 0) {
			audio_index = i;
		}
	}

	if (audio_index >= 0) {
		int flag = stream_component_open(is, audio_index);
		if (flag == -1) {
			program_fail(is);
			return -1;
		}
	}

	if (video_index >= 0) {
		int flag = stream_component_open(is, video_index);
		if (flag == -1) {
			program_fail(is);
			return -1;
		}
	}

	if (is->video_st_index < 0 || is->audio_st_index < 0) {
		std::cout << "FFmpeg: cannot open codec" << std::endl;
		program_fail(is);
		return -1;
	}

	for (;;) {
		if (is->quit == 1) {
			break;
		}

		if (is->video_queue.size > MAX_VIDEOQ_SIZE
			|| is->audio_queue.size > MAX_AUDIOQ_SIZE) {
			SDL_Delay(10);
			//std::cout << "delaying" << std::endl;
			continue;
		}

		//std::cout << i++ << std::endl;
		int ret = av_read_frame(is->pFormatCtx, packet);
		if (ret < 0) {
			//std::cout << ret << std::endl;
			//if (is->pFormatCtx->pb->error == 0) {
			//	// no error, wait for input
			//	SDL_Delay(1);
			//	continue;
			//}
			//else {
			//	break;
			//}

			//std::cout << "set allsent true" << std::endl;
			break;
		}

		// send packet to queue
		if (packet->stream_index == is->video_st_index) {
			packet_queue_put(&is->video_queue, packet);
		}
		else if (packet->stream_index == is->audio_st_index) {
			packet_queue_put(&is->audio_queue, packet);
		}


		av_packet_unref(packet);
	}

	is->video_queue.all_sent = true;
	is->audio_queue.all_sent = true;
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
			SDL_CondSignal(is->audio_queue.cond);
			SDL_CondSignal(is->pFrameQ.cond);
			std::cout << "signal sent" << std::endl;

			//SDL_PauseAudioDevice(is->dev_id, 1);

			SDL_WaitThread(is->demux_tid, nullptr);
			std::cout << "demux closed" << std::endl;
			SDL_WaitThread(is->video_tid, nullptr);
			std::cout << "video closed" << std::endl;
			//SDL_PauseAudioDevice(is->dev_id, 1);

			//SDL_LockAudioDevice(is->dev_id);
			//std::cout << "lock" << std::endl;
			SDL_CloseAudioDevice(is->dev_id);
			std::cout << "audio closed" << std::endl;

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