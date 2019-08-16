#include <iostream>
#include <string>
#define SDL_MAIN_HANDLED

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

#include <stdio.h>
}

void checkInit();

const char* SRC_FILE = "test2.mp4";

int main() {
	//checkInit();
	AVFormatContext* pFormatCtx = nullptr;
	int i, videoStream;
	AVCodecContext* pCodecCtxOrig = nullptr;
	AVCodecContext* pCodecCtx = nullptr;
	AVCodec* pCodec = nullptr;
	AVFrame* pFrame = nullptr;
	AVPacket packet;
	int frameFinished;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "cannot initialize SDL:" << SDL_GetError() << std::endl;
		return -1;
	}

	// Open file, demux to AVFormatContext
	if (avformat_open_input(&pFormatCtx, SRC_FILE, nullptr, nullptr) != 0) {
		return -1;
	}

	// Find stream in AVFormatContext
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		return -1;
	}

	av_dump_format(pFormatCtx, 0, SRC_FILE, 0);

	videoStream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; ++i) {
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStream = i;
			break;
		}
	}

	if (videoStream == -1) {
		return -1;
	}

	// TODO: continue.

	pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;

	// Find decoder
	pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if (pCodec == nullptr) {
		std::cout << stderr << "Unsupported codec!" << std::endl;
		return -1;
	}

	// Copy context(must not use origin pointer)
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
		std::cout << stderr << "Couldn't copy codec context" << std::endl;
		return -1;
	}

	// Open codec
	if (avcodec_open2(pCodecCtx, pCodec, nullptr) < 0) {
		return -1;
	}

	// Allocate video frame
	pFrame = av_frame_alloc();

	// Make a screen to put video
	SDL_Window* screen = SDL_CreateWindow(
		"SimplePlayer32",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		pCodecCtx->width,
		pCodecCtx->height,
		0
	);
	
	if (!screen) {
		std::cout << stderr << "SDL: couldn't create window - exiting" << std::endl;
		return -1;
	}

	// Create Renderer 
	SDL_Renderer* renderer = SDL_CreateRenderer(screen, -1, 0);

	if (!renderer) {
		std::cout << stderr << "SDL: couldn't create renderer - exiting" << std::endl;
	}

	// Allocate a place to put YUV image on
	SDL_Texture* texture = SDL_CreateTexture(
		renderer,
		SDL_PIXELFORMAT_YV12,
		SDL_TEXTUREACCESS_STREAMING,
		pCodecCtx->width,
		pCodecCtx->height
	);

	if (!texture) {
		std::cout << stderr << "SDL: couldn't create texture - exiting" << std::endl;
		return -1;
	}

	// Initialize SWS context for software scaling
	SwsContext* sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
		pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
		AV_PIX_FMT_YUV420P,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr
	);

	// set up YV12 pixel array
	size_t yPlaneSz = pCodecCtx->width * pCodecCtx->height;
	size_t uvPlaneSz = pCodecCtx->width * pCodecCtx->height / 4;
	Uint8* yPlane = (Uint8*)malloc(yPlaneSz);
	Uint8* uPlane = (Uint8*)malloc(uvPlaneSz);
	Uint8* vPlane = (Uint8*)malloc(uvPlaneSz);

	if (!yPlane || !uPlane || !vPlane) {
		std::cout << stderr << "Could not allocate pixel buffers - exiting" << std::endl;
		return -1;
	}

	int uvPitch = pCodecCtx->width / 2;
	
	SDL_Event sdlEvent;

	while (av_read_frame(pFormatCtx, &packet) >= 0) {
		if (packet.stream_index == videoStream) {
			// Decode
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

			if (frameFinished) {
				AVPicture pict;
				pict.data[0] = yPlane;
				pict.data[1] = uPlane;
				pict.data[2] = vPlane;
				pict.linesize[0] = pCodecCtx->width;
				pict.linesize[1] = uvPitch;
				pict.linesize[2] = uvPitch;

				// Convert the image into YUV format
				sws_scale(sws_ctx, (uint8_t const* const*)pFrame->data,
					pFrame->linesize, 0, pCodecCtx->height, pict.data,
					pict.linesize);

				SDL_UpdateYUVTexture(
					texture,
					nullptr,
					yPlane,
					pCodecCtx->width,
					uPlane,
					uvPitch,
					vPlane,
					uvPitch
				);

				SDL_RenderClear(renderer);
				SDL_RenderCopy(renderer, texture, nullptr, nullptr);
				SDL_RenderPresent(renderer);
			}
		}

		//Free packet by av_read_frame
		av_free_packet(&packet);
		SDL_PollEvent(&sdlEvent);
		switch (sdlEvent.type){
		case SDL_QUIT:
			SDL_DestroyTexture(texture);
			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(screen);
			SDL_Quit();
			return 0;
			break;
		default:
			break;
		}
	}

	// Free YUV frame
	av_frame_free(&pFrame);
	free(yPlane);
	free(uPlane);
	free(vPlane);

	// Close codec
	avcodec_close(pCodecCtx);
	avcodec_close(pCodecCtxOrig);

	//Close video file
	avformat_close_input(&pFormatCtx);

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