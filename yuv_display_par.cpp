#include "yuv_display_par.h"

YUVDisplayPar::~YUVDisplayPar() {
	delete[] yPlane;
	delete[] uPlane;
	delete[] vPlane;
}

void YUVDisplayPar::init_YUV_display_par(int w, int h) {
	yPlaneSz = w * h;
	uvPlaneSz = w * h / 4;
	yPlane = new Uint8[yPlaneSz];
	uPlane = new Uint8[uvPlaneSz];
	vPlane = new Uint8[uvPlaneSz];

	uvPitch = w / 2;
	pict.data[0] = yPlane;
	pict.data[1] = uPlane;
	pict.data[2] = vPlane;
	pict.linesize[0] = w;
	pict.linesize[1] = uvPitch;
	pict.linesize[2] = uvPitch;
}