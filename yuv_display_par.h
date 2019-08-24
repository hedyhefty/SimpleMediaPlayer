#pragma once
#include "util.h"

class YUVDisplayPar {
public:
	YUVDisplayPar() :
		pict(),
		yPlaneSz(0),
		uvPlaneSz(0),
		yPlane(nullptr),
		uPlane(nullptr),
		vPlane(nullptr),
		uvPitch(0) {
	}

	size_t yPlaneSz;
	size_t uvPlaneSz;
	Uint8* yPlane;
	Uint8* uPlane;
	Uint8* vPlane;
	int uvPitch;
	AVFrame pict;
};

#include "video_state.h"

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
