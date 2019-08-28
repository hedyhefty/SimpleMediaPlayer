#pragma once

#include <iostream>


extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/avstring.h"
#include "libavutil/time.h"

	// SDL define main and we should handle it with macro
#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>
}

#define SDL_AUDIO_NOT_ALLOW_ANY_CHANGE 0

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)


const double SEEK_SAFE_FRACTOR = 0.99;
const double TIME_BASE = 1000000.0;
const double AV_SYNC_THRESHOLD = 0.05;

int* quit_ref = nullptr;

AVPacket flush_pkt;

#include "packet_queue.h"
#include "frame_queue.h"
#include "yuv_display_par.h"



