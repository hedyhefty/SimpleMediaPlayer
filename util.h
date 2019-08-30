#pragma once

#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/avstring.h"
#include "libavutil/time.h"
#include "libswresample/swresample.h"

	// SDL define main and we should handle it with macro
#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>
}















