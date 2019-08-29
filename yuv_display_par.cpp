#include "yuv_display_par.h"

YUVDisplayPar::~YUVDisplayPar() {
	delete[] yPlane;
	delete[] uPlane;
	delete[] vPlane;
}