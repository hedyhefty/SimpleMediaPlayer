#include "util.h"
#include "video_state.h"
#include <iostream>

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

const int SDL_AUDIO_NOT_ALLOW_ANY_CHANGE = 0;
const uint16_t SDL_AUDIO_BUFFER_SIZE = 1024;
const int MAX_AUDIOQ_SIZE = (5 * 16 * 1024);
const int MAX_VIDEOQ_SIZE = (5 * 256 * 1024);
const double SEEK_SAFE_FRACTOR = 0.99;
const double TIME_BASE = 1000000.0;
const double AV_SYNC_THRESHOLD = 0.05;
const bool SPEED_UP_FLAG = false;
const bool SLOW_DOWN_FLAG = true;

const char* SRC_FILE = "test2.mp4";

SDL_Window* screen;
static SDL_Renderer* renderer;
SDL_Texture* texture;

VideoState* global_video_state;

AVPacket flush_pkt;

void program_fail(VideoState* is);

void init_calculate_rect(VideoState* is);

void init_YUV_display_par(VideoState* is);

double get_master_clock(VideoState* is);

double get_external_clock(VideoState* is);

void reset_clock(VideoState* is);

void tun_clock(VideoState* is, bool flag);

void stream_seek(VideoState* is, double pos, double rel);

Uint32 sdl_refresh_timer_cb(Uint32 interval, void* opaque);

void schedule_refresh(VideoState* is, int delay);

int audio_decode_frame(VideoState* is, uint8_t* audio_buf, int buf_size);

void audio_callback(void* userdata, Uint8* stream, int len);

AVFrame* nv12_to_yuv420p(AVFrame* nv12_frame);

void video_display(VideoState* is);

void video_refresh_timer(void* userdata);

int queue_picture(VideoState* is, AVFrame* pFrame);

int video_thread(void* arg);

int hw_video_thread(void* arg);

int stream_component_open(VideoState* is, int stream_index);

AVPixelFormat get_format(AVCodecContext* avctx, const AVPixelFormat* pix_fmt);

int hw_video_open(VideoState* is, int stream_index);

int demux_thread(void* arg);

static void event_loop(VideoState* is);

int main() {
	VideoState* is = new VideoState;

	std::cout << avcodec_configuration() << std::endl;

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

	schedule_refresh(is, 40);

	is->demux_tid = SDL_CreateThread(demux_thread, "demux_t", is);
	if (!is->demux_tid) {
		av_free(is);
		return -1;
	}

	if (!renderer) {
		std::cout << "SDL: cannot create renderer" << std::endl;
	}

	av_init_packet(&flush_pkt);
	flush_pkt.data = (unsigned char*)"FLUSH";

	event_loop(is);

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(screen);
	SDL_Quit();

	global_video_state = nullptr;
	delete is;

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
			* is->video_st->codecpar->width / is->video_st->codecpar->height;
	}
	if (aspect_ratio <= 0.0) {
		aspect_ratio = (double)is->video_st->codecpar->width
			/ (double)is->video_st->codecpar->height;
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

void init_YUV_display_par(VideoState* is) {
	// set up YV12 pixel array
	is->yuv_display.yPlaneSz = is->video_ctx->width * is->video_ctx->height;
	is->yuv_display.uvPlaneSz = is->video_ctx->width * is->video_ctx->height / 4;
	is->yuv_display.yPlane = new Uint8[is->yuv_display.yPlaneSz];
	is->yuv_display.uPlane = new Uint8[is->yuv_display.uvPlaneSz];
	is->yuv_display.vPlane = new Uint8[is->yuv_display.uvPlaneSz];

	is->yuv_display.uvPitch = is->video_ctx->width / 2;

	is->yuv_display.pict.data[0] = is->yuv_display.yPlane;
	is->yuv_display.pict.data[1] = is->yuv_display.uPlane;
	is->yuv_display.pict.data[2] = is->yuv_display.vPlane;
	is->yuv_display.pict.linesize[0] = is->video_ctx->width;
	is->yuv_display.pict.linesize[1] = is->yuv_display.uvPitch;
	is->yuv_display.pict.linesize[2] = is->yuv_display.uvPitch;
}

double get_master_clock(VideoState* is) {
	return get_external_clock(is);
}

double get_external_clock(VideoState* is) {
	double real_time = (is->speed_factor * ((av_gettime() - is->base_time) / TIME_BASE)) + is->time_stamp;
	//std::cout << real_time << std::endl;
	//std::cout << is->speed_factor << std::endl;
	return real_time;
}

void reset_clock(VideoState* is) {
	is->base_time -= (int64_t)(double(is->seek_rel) / is->speed_factor);
}

void tun_clock(VideoState* is, bool flag) {
	double factor = is->speed_factor;
	double time = get_master_clock(is);

	is->time_stamp = time;
	is->base_time = static_cast<double>(av_gettime());
}

void stream_seek(VideoState* is, double pos, double rel) {
	if (!is->seek_req) {
		is->seek_pos = (int64_t)(pos * AV_TIME_BASE);
		is->seek_rel = (int64_t)(rel * AV_TIME_BASE);
		is->seek_flag = ((rel < 0) ? AVSEEK_FLAG_BACKWARD : 0);

		if (is->seek_pos > (int64_t)((double)is->pFormatCtx->duration * SEEK_SAFE_FRACTOR)) {
			return;
		}

		is->seek_req = true;

		/*std::cout << "pos: " << is->seek_pos << std::endl;
		std::cout << "dur: " << is->pFormatCtx->duration << std::endl;
		std::cout << "save dur: " << (int64_t)((double)is->pFormatCtx->duration 
		* SEEK_SAFE_FRACTOR) << std::endl;*/
		//std::cout << "flag: " << is->seek_flag << std::endl;
	}
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
	AVFrame* frame = is->audio_frame;

	for (;;) {
		int got_frame = avcodec_receive_frame(is->audio_ctx, frame);

		if (is->quit == 1) {
			return -1;
		}

		if (got_frame == 0) {
			int64_t dst_nb_samples = av_rescale_rnd(swr_get_delay(is->swr_ctx, frame->sample_rate) + frame->nb_samples, frame->sample_rate, frame->sample_rate, AVRounding(1));
			Uint8* p = is->audio_temp_buf;
			int nb = swr_convert(is->swr_ctx, &p, static_cast<int>(dst_nb_samples), (const uint8_t * *)frame->data, frame->nb_samples);
			data_size = frame->channels * nb * av_get_bytes_per_sample(is->dst_format);

			assert(data_size <= buf_size);
			//return data_size;

			// sync audio code start here
			double pts = static_cast<double>(frame->best_effort_timestamp);
			if (pts != AV_NOPTS_VALUE) {
				int delta_size;
				pts *= av_q2d(is->audio_st->time_base);
				
				double ref_time = get_master_clock(is);
				double diff = pts - ref_time;

				/*std::cout << "diff: " << diff << std::endl;
				std::cout << "master clock: " << get_master_clock(is) << std::endl;
				std::cout << "pts: " << pts << std::endl;*/

				if (fabs(diff) > AV_SYNC_THRESHOLD) {
					//std::cout << "tunning" << std::endl;
					int n = 2 * is->audio_ctx->channels;
					delta_size = int(diff * is->audio_ctx->sample_rate) * n;

					if (delta_size > 0) {
						if (delta_size > buf_size / 2) {
							delta_size = buf_size / 2 - data_size;
						}
						memcpy(audio_buf, is->audio_temp_buf, data_size);
						for (size_t i = 0; i < static_cast<size_t>(delta_size); ++i) {
							memcpy(audio_buf + data_size + i, &is->audio_temp_buf[data_size], 1);
						}

					}
					else {
						if (delta_size + data_size < data_size / 2) {
							delta_size = -data_size;
						}
						memcpy(audio_buf, is->audio_temp_buf, data_size + delta_size);
					}

					return data_size + delta_size;
				}
			}
			// sync audio code end
			
			memcpy(audio_buf, is->audio_temp_buf, data_size);

			//is->audio_pkt_data += pkt->size;
			//is->audio_pkt_size -= pkt->size;

			/* We have data, return it and come back for more later */
			return data_size;
		}

		/*if (is->audio_pkt_flush) {
			avcodec_flush_buffers(is->audio_ctx);
			is->audio_pkt_flush = false;
		}*/

		//if (pkt->data) {
		//	av_packet_unref(pkt);
		//}

		/*if (packet_queue_get(&is->audio_queue, pkt, 1) < 0) {
			return -1;
		}*/

		if (is->audio_queue.packet_queue_get(pkt, 1) < 0) {
			return -1;
		}

		if (pkt->data == flush_pkt.data) {
			avcodec_flush_buffers(is->audio_ctx);
			is->audio_pkt_flush = false;
			//std::cout << "audio codec flushed" << std::endl;
			continue;
		}

		//is->audio_pkt_data = pkt->data;
		//is->audio_pkt_size = pkt->size;
		int decode_succeed = avcodec_send_packet(is->audio_ctx, pkt);
		if (decode_succeed < 0) {
			return -1;
		}

		av_packet_unref(pkt);
		av_freep(pkt);
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
		//SDL_MixAudio(stream, temp, len1, SDL_MIX_MAXVOLUME);
		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}

}

AVFrame* nv12_to_yuv420p(AVFrame* nv12_frame) {
	int x;
	int y;
	AVFrame* frame = av_frame_alloc();
	if (!frame) {
		printf("cannot allow frame\n");
		return NULL;
	}

	frame->format = AV_PIX_FMT_YUV420P;
	frame->width = nv12_frame->width;
	frame->height = nv12_frame->height;

	int ret = av_frame_get_buffer(frame, 32);
	if (ret < 0) {
		printf("cannot alloc frame data\n");
		return 0;
	}

	ret = av_frame_make_writable(frame);
	if (ret < 0) {
		return 0;
	}

	if (nv12_frame->linesize[0] == nv12_frame->width) {
		memcpy(frame->data[0], nv12_frame->data[0], nv12_frame->height * nv12_frame->linesize[0]);
	}
	else {
		for (y = 0; y < frame->height; y++) {
			for (x = 0; x < frame->width; x++) {
				frame->data[0][y * frame->linesize[0] + x] = nv12_frame->data[0][y * nv12_frame->linesize[0] + x];
			}
		}
	}

	for (y = 0; y < frame->height / 2; y++) {
		for (x = 0; x < frame->width / 2; x++) {
			frame->data[1][y * frame->linesize[1] + x] = nv12_frame->data[1][y * nv12_frame->linesize[1] + 2 * x];
			frame->data[2][y * frame->linesize[2] + x] = nv12_frame->data[1][y * nv12_frame->linesize[1] + 2 * x + 1];
		}
	}

	return frame;
}

void video_display(VideoState* is) {
	myFrame* vp;
	//AVFrame* frame;
	//frame = av_frame_alloc();

	//vp = frame_queue_dequeue(&is->pFrameQ);
	vp = is->pFrameQ.frame_queue_dequeue();
	
	//av_frame_move_ref(frame, vp->frame);
	//av_frame_unref(vp->frame);

	// convert to YUV
	sws_scale(
		is->sws_ctx,
		(uint8_t const* const*)vp->frame->data,
		vp->frame->linesize,
		0,
		is->video_st->codecpar->height,
		is->yuv_display.pict.data,
		is->yuv_display.pict.linesize
	);

	// play with rect
	SDL_UpdateYUVTexture(
		texture,
		nullptr,
		is->yuv_display.yPlane,
		is->video_st->codecpar->width,
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

	
	delete vp;
	//av_frame_free(&frame);
}

void video_refresh_timer(void* userdata) {
	VideoState* is = (VideoState*)userdata;
	static double previous_pts = 0;
	static double previous_time_delta = 0;
	double delta_time = 0;
	double pts = 0;

	

	if (is->pframe_queue_flush) {
		//frame_queue_flush(&is->pFrameQ);
		is->pFrameQ.frame_queue_flush();
		//std::cout << "size: " << is->pFrameQ.size << std::endl;
		reset_clock(is);
		is->pframe_queue_flush = false;
		schedule_refresh(is, 10);
		return;
	}

	if (is->video_st) {
		if (is->pFrameQ.size == 0) {
			schedule_refresh(is, 1);
		}
		else {
			/* pts here stand for the x'th frame of the video,
			not the time of it should be present */
			//pts = frame_queue_get_pts(&is->pFrameQ);
			pts = is->pFrameQ.frame_queue_get_pts();
			//std::cout << "pts: " << pts << std::endl;
			if (pts == AV_NOPTS_VALUE) {
				//return;
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
			//double repeat_coeff = frame_queue_get_repeat_coeff(&is->pFrameQ);
			double repeat_coeff = is->pFrameQ.frame_queue_get_repeat_coeff();
			double predict_delta = time_per_frame + repeat_coeff * (double)time_per_frame / 2 + diff;

			int predict_result = static_cast<int>(predict_delta * 1000);
			//std::cout << "pts:" << pts << std::endl;
			//std::cout << "master clock: " << ref_time << std::endl;
			//std::cout << "diff: " << diff << std::endl;
			//std::cout << "predict delta: " << predict_result << std::endl;

			int tunned_result = predict_result < 0 ? 0 : predict_result;

			if (tunned_result >= 1000) {
				tunned_result = 20;
			}

			//std::cout << "tunned delta:" << tunned_result << std::endl;

			schedule_refresh(is, 20);

			// show the picture
			video_display(is);
			//std::cout << "frame queue size:" << is->pFrameQ.size << std::endl;
			// update queue for next picture
			//frame_queue_dequeue(&is->pFrameQ);
		}
	}
	else {
		schedule_refresh(is, 100);
	}
}

int queue_picture(VideoState* is, AVFrame* pFrame) {
	myFrame* vp;

	// wait until have place to write
	//vp = frame_queue_writablepos_ref(&is->pFrameQ);
	vp = is->pFrameQ.frame_queue_writablepos_ref();

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

		//ret = packet_queue_get(&is->video_queue, packet, 1);
		ret = is->video_queue.packet_queue_get(packet, 1);
		if (ret < 0) {
			// quit getting packets
			break;
		}

		if (packet->data == flush_pkt.data) {
			avcodec_flush_buffers(is->video_ctx);
			is->video_pkt_flush = false;
			//std::cout << "video codec flushed" << std::endl;
			continue;
		}

		// decode video frame
		ret = avcodec_send_packet(is->video_ctx, packet);
		//frameFinished = avcodec_receive_frame(is->video_ctx, pFrame);
		//if (frameFinished == 0) {
		//	//queue frame to frame queue
		//	queue_picture(is, pFrame);

		//}
		//else {
		//	std::cout << "not finished yet: "<< frameFinished << std::endl;
		//}
		//av_frame_unref(pFrame);

		if (ret < 0) {
			break;
		}

		while (ret >= 0) {
			ret = avcodec_receive_frame(is->video_ctx, pFrame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			else if (ret < 0) {
				std::cout << "error during decoding" << std::endl;
				break;
			}

			queue_picture(is, pFrame);
			av_frame_unref(pFrame);
		}

		av_packet_unref(packet);
		av_freep(packet);

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			continue;
		}
		else if (ret < 0) {
			break;
		}

		//av_packet_unref(packet);
		//std::cout << is->video_queue.nb_packets << std::endl;
		if (is->video_queue.all_sent && is->video_queue.nb_packets == 0) {
			//std::cout << "???" << std::endl;
			break;
		}

	}

	av_frame_free(&pFrame);

	std::cout << "video thread return" << std::endl;

	return 0;
}

int hw_video_thread(void* arg) {
	VideoState* is = (VideoState*)arg;
	AVPacket pkt1;
	AVPacket* packet = &pkt1;

	AVFrame* hw_frame = nullptr;
	AVFrame* sw_frame = nullptr;
	AVFrame* y420p_frame = nullptr;

	hw_frame = av_frame_alloc();
	sw_frame = av_frame_alloc();

	int ret = 0;

	for (;;) {
		if (is->quit == 1) {
			break;
		}

		ret = is->video_queue.packet_queue_get(packet, 1);
		if (ret < 0) {
			// quit getting packets
			break;
		}

		if (packet->data == flush_pkt.data) {
			avcodec_flush_buffers(is->video_ctx);
			is->video_pkt_flush = false;
			//std::cout << "video codec flushed" << std::endl;
			continue;
		}

		ret = avcodec_send_packet(is->video_ctx, packet);
		if (ret < 0) {
			std::cout << "error during decode" << std::endl;
		}

		std::cout << "decode by hw" << std::endl;

		while (ret >= 0) {
			if (is->quit == 1) {
				break;
			}
			ret = avcodec_receive_frame(is->video_ctx, hw_frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			else if (ret < 0) {
				std::cout << "error during decoding" << std::endl;
				break;
			}

			ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);
			if (ret < 0) {
				std::cout << "error while tranferring data" << std::endl;
				break;
			}

			y420p_frame = nv12_to_yuv420p(sw_frame);
			y420p_frame->pts = packet->pts;
			if (!y420p_frame) {
				break;
			}
			
			queue_picture(is, y420p_frame);
			av_frame_unref(hw_frame);
			av_frame_unref(sw_frame);
			av_frame_unref(y420p_frame);
		}

		av_packet_unref(packet);

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			continue;
		}
		else if (ret < 0) {
			break;
		}
	}

	av_frame_free(&hw_frame);
	av_frame_free(&sw_frame);

	std::cout << "hw video thread return" << std::endl;
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
		SDL_zero(wanted_spec);
		wanted_spec.freq = codecCtx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;
		wanted_spec.channels = codecCtx->channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;

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

	int index;
	int channels;
	Uint64 channel_layout;
	Uint64 dst_layout;

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		is->audio_st_index = stream_index;
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_ctx = codecCtx;
		is->audio_buf_size = 0;
		is->audio_buf_index = 0;
		is->audio_queue.flush_pkt_p = &flush_pkt;
		
		index = av_get_channel_layout_channel_index(av_get_default_channel_layout(4), AV_CH_FRONT_CENTER);

		channels = is->audio_st->codecpar->channels;
		channel_layout = is->audio_st->codecpar->channel_layout;

		if (channels > 0 && channel_layout == 0) {
			channel_layout = av_get_default_channel_layout(channels);
		}
		else if (channels == 0 && channel_layout > 0) {
			channels = av_get_channel_layout_nb_channels(channel_layout);
		}

		dst_layout = av_get_default_channel_layout(channels);

		swr_alloc_set_opts(
			is->swr_ctx,
			dst_layout,
			is->dst_format,
			is->audio_st->codecpar->sample_rate,
			channel_layout,
			(AVSampleFormat)is->audio_st->codecpar->format,
			is->audio_st->codecpar->sample_rate,
			0,
			nullptr
		);

		if (!is->swr_ctx || swr_init(is->swr_ctx) < 0) {
			program_fail(is);
			return -1;
		}
		//memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		//SDL_PauseAudio(0);
		
		break;

	case AVMEDIA_TYPE_VIDEO:
		is->video_st_index = stream_index;
		is->video_st = pFormatCtx->streams[stream_index];
		is->video_ctx = codecCtx;

		//frame_queue_init(&is->pFrameQ);
		//packet_queue_init(&is->video_queue);
		
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
		is->yuv_display.init_YUV_display_par(is->video_ctx->width, is->video_ctx->height);

		// init display rectangle size
		init_calculate_rect(is);
		if (!texture) {
			std::cout << "SDL: cannot create texture" << std::endl;
			return -1;
		}

		is->video_queue.flush_pkt_p = &flush_pkt;
		break;
	default:
		break;
	}

	return 0;
}

AVPixelFormat get_format(AVCodecContext* avctx, const AVPixelFormat* pix_fmts) {
	while (*pix_fmts != AV_PIX_FMT_NONE) {
		if (*pix_fmts == AV_PIX_FMT_QSV) {
			AVBufferRef* bref = (AVBufferRef*)avctx->opaque;
			AVHWFramesContext* frames_ctx;
			AVQSVFramesContext* frames_hwctx;
			int ret;

			avctx->hw_frames_ctx = av_hwframe_ctx_alloc(bref);
			if (!avctx->hw_frames_ctx) {
				return AV_PIX_FMT_NONE;
			}

			frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
			frames_hwctx = (AVQSVFramesContext*)frames_ctx->hwctx;

			frames_ctx->format = AV_PIX_FMT_QSV;
			frames_ctx->sw_format = avctx->sw_pix_fmt;
			frames_ctx->width = FFALIGN(avctx->coded_width, 32);
			frames_ctx->height = FFALIGN(avctx->coded_height, 32);
			frames_ctx->initial_pool_size = 32;

			frames_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

			ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
			if (ret < 0) {
				return AV_PIX_FMT_NONE;
			}

			return AV_PIX_FMT_QSV;
		}

		pix_fmts++;
	}

	std::cout << "QSV format not offered" << std::endl;
	return AV_PIX_FMT_NONE;
}

int hw_video_open(VideoState* is, int stream_index) {
	AVFormatContext* formatCtx = is->pFormatCtx;
	AVCodecContext* codecCtx = nullptr;
	AVCodec* codec = nullptr;
	AVStream* video_st = formatCtx->streams[stream_index];
	int ret;

	if (stream_index < 0 || stream_index >= (int)formatCtx->nb_streams) {
		std::cout << "stream index out of range" << std::endl;
		return -1;
	}

	ret = av_hwdevice_ctx_create(&is->hw_device_ref, AV_HWDEVICE_TYPE_QSV, "auto", nullptr, 0);
	if (ret < 0) {
		std::cout << "cannot create hwdevice" << std::endl;
		return -1;
	}

	codec = avcodec_find_decoder_by_name("h264_qsv");
	if (!codec) {
		std::cout << "qsv decoder unsupport" << std::endl;
		return -1;
	}

	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx) {
		std::cout << "codecCtx init failed" << std::endl;
	}
	codecCtx->codec_id = AV_CODEC_ID_H264;
	
	if (video_st->codecpar->extradata_size) {
		codecCtx->extradata = (uint8_t*)av_mallocz(
			video_st->codecpar->extradata_size
			+ AV_INPUT_BUFFER_PADDING_SIZE
		);

		if (!codecCtx->extradata) {
			std::cout << "codecCtx set failed" << std::endl;
			return -1;
		}
		memcpy(
			codecCtx->extradata,
			video_st->codecpar->extradata,
			video_st->codecpar->extradata_size
		);
		codecCtx->extradata_size = video_st->codecpar->extradata_size;
	}

	codecCtx->opaque = is->hw_device_ref;
	codecCtx->get_format = get_format;

	ret = avcodec_open2(codecCtx, nullptr, nullptr);
	if (ret < 0) {
		std::cout << "cannot open codec" << std::endl;
		return -1;
	}

	is->video_ctx = codecCtx;
	is->video_st = video_st;
	is->video_st_index = stream_index;

	int w = video_st->codecpar->width;
	int h = video_st->codecpar->height;

	is->video_ctx->height = h;
	is->video_ctx->width = w;
	
	is->sws_ctx = sws_getContext(
		w, h,
		AV_PIX_FMT_YUV420P,
		w, h,
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
		w, h
	);

	if (!texture) {
		std::cout << "cannot create texture" << std::endl;
		return -1;
	}

	is->yuv_display.init_YUV_display_par(w, h);
	init_calculate_rect(is);
	
	is->video_queue.flush_pkt_p = &flush_pkt;

	std::cout << "hardware acceleration enable." << std::endl;

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
		int ret = 0;
		if (is->hw_decode_flag) {
			ret = hw_video_open(is, video_index);
		}
		else {
			ret = stream_component_open(is, video_index);
		}
		
		if (ret == -1) {
			program_fail(is);
			return -1;
		}
	}

	if(audio_index >= 0){
		SDL_PauseAudioDevice(is->dev_id, 0);
	}

	if (video_index >= 0) {
		if (is->hw_decode_flag) {
			is->video_tid = SDL_CreateThread(hw_video_thread, "hw_video_t", is);
		}
		else {
			is->video_tid = SDL_CreateThread(video_thread, "video_t", is);
		}
	}

	if (is->video_st_index < 0 || is->audio_st_index < 0) {
		std::cout << "FFmpeg: cannot open codec" << std::endl;
		program_fail(is);
		return -1;
	}

	is->base_time = static_cast<double>(av_gettime());

	// std::cout << "dur: " << is->pFormatCtx->duration << std::endl;

	for (;;) {
		if (is->quit == 1) {
			break;
		}

		// seek code start
		if (is->seek_req) {
			int64_t seek_target = is->seek_pos;

			int ret = av_seek_frame(is->pFormatCtx, -1, seek_target, is->seek_flag);
			if (ret < 0) {
				std::cout << "seek failed" << std::endl;
			}
			else {
				if (is->audio_st_index >= 0) {
					//packet_queue_flush(&is->audio_queue);
					//packet_queue_put(&is->audio_queue, &flush_pkt);
					is->audio_queue.packet_queue_flush();
					is->audio_queue.packet_queue_put(&flush_pkt);
					is->audio_pkt_flush = true;
				}

				if (is->video_st_index >= 0) {
					//packet_queue_flush(&is->video_queue);
					//packet_queue_put(&is->video_queue, &flush_pkt);
					is->video_queue.packet_queue_flush();
					is->video_queue.packet_queue_put(&flush_pkt);
					is->video_pkt_flush = true;
					is->pframe_queue_flush = true;
				}
			}

			is->seek_req = false;
		}
		// seek code end

		// wait until frame queue flush
		if (is->pframe_queue_flush||is->video_pkt_flush||is->audio_pkt_flush) {
			SDL_Delay(10);
			continue;
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
			//packet_queue_put(&is->video_queue, packet);
			is->video_queue.packet_queue_put(packet);
		}
		else if (packet->stream_index == is->audio_st_index) {
			//packet_queue_put(&is->audio_queue, packet);
			is->audio_queue.packet_queue_put(packet);
		}


		//av_packet_unref(packet);
	}

	is->video_queue.all_sent = true;
	is->audio_queue.all_sent = true;
	std::cout << "demux thread return" << std::endl;
	return 0;
}

static void event_loop(VideoState* is) {
	SDL_Event sdl_event;
	double incr = 0;
	double pos = 0;
	bool jump = false;
	bool speed_up = false;
	bool slow_down = false;

	for (;;) {
		SDL_WaitEvent(&sdl_event);
		switch (sdl_event.type) {
		case SDL_KEYDOWN:
			switch (sdl_event.key.keysym.sym) {
			case SDLK_ESCAPE:
				program_fail(is);
				break;
			case SDLK_LEFT:
				incr = -5;
				jump = true;
				break;
			case SDLK_RIGHT:
				incr = 5;
				jump = true;
				break;
			case SDLK_UP:
				speed_up = true;
				break;
			case SDLK_DOWN:
				slow_down = true;
				break;
			default:
				break;
			}
			if (speed_up && is->speed_factor < 7) {
				//std::cout << "speed up called" << std::endl;
				speed_up = false;
				tun_clock(is, SPEED_UP_FLAG);
				is->speed_factor *= 2;
			}

			if (slow_down && is->speed_factor > 0.3) {
				//std::cout << "speed down called" << std::endl;
				slow_down = false;
				tun_clock(is, SLOW_DOWN_FLAG);
				is->speed_factor *= 0.5;
			}

			if (global_video_state && jump) {
				jump = false;
				pos = get_master_clock(global_video_state);
				pos += incr;
				if (pos < 0) {
					incr = - get_master_clock(global_video_state);
					pos = 0;
				}
				//std::cout << "incr: " << incr << std::endl;
				//std::cout << "pos: " << pos << std::endl;
				stream_seek(global_video_state, pos, incr);
			}
			break;

		case FF_QUIT_EVENT:
		case SDL_QUIT:
			is->quit = 1;

			SDL_CondSignal(is->video_queue.cond);
			SDL_CondSignal(is->audio_queue.cond);
			SDL_CondSignal(is->pFrameQ.cond);
			std::cout << "signal sent" << std::endl;

			SDL_WaitThread(is->demux_tid, nullptr);
			std::cout << "demux closed" << std::endl;
			SDL_WaitThread(is->video_tid, nullptr);
			std::cout << "video closed" << std::endl;

			SDL_CloseAudioDevice(is->dev_id);
			std::cout << "audio closed" << std::endl;

			

			std::cout << "exit" << std::endl;

			return;
		case FF_REFRESH_EVENT:
			video_refresh_timer(sdl_event.user.data1);
			
			break;
		default:
			break;
		}
	}
}