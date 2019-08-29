#include "video_state.h"

VideoState::VideoState() {
	// allocate video_ctx & audio_ctx
	video_ctx = avcodec_alloc_context3(nullptr);
	audio_ctx = avcodec_alloc_context3(nullptr);
	video_queue.quit = &quit;
	audio_queue.quit = &quit;
	pFrameQ.quit = &quit;
}

VideoState::~VideoState() {
	// free video_ctx & audio_ctx
	avcodec_close(video_ctx);
	avcodec_free_context(&video_ctx);

	avcodec_close(audio_ctx);
	avcodec_free_context(&audio_ctx);

	avformat_close_input(&pFormatCtx);
}