#include "util.h"

class YUVDisplayPar {
public:
	~YUVDisplayPar();
	/*~YUVDisplayPar() {
		delete[] yPlane;
		delete[] uPlane;
		delete[] vPlane;
	}*/

	size_t yPlaneSz = 0;
	size_t uvPlaneSz = 0;
	Uint8* yPlane = nullptr;
	Uint8* uPlane = nullptr;
	Uint8* vPlane = nullptr;
	int uvPitch = 0;
	AVFrame pict = {};
};



