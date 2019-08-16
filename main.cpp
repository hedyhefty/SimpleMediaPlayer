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
	AVFrame* pFrame = nullptr;
	AVPacket packet;
	int frameFinished;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "cannot initialize SDL:" << SDL_GetError() << std::endl;
		return -1;
	}

	if (avformat_open_input(&pFormatCtx, SRC_FILE, nullptr, nullptr) != 0) {
		return -1;
	}

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

	//TODO: continue.

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