#include "video_state.h"

VideoState::VideoState() {
	// allocate video_ctx & audio_ctx
	video_ctx = avcodec_alloc_context3(nullptr);
	audio_ctx = avcodec_alloc_context3(nullptr);
	video_queue.quit = &quit;
	audio_queue.quit = &quit;
	pFrameQ.quit = &quit;
	swr_ctx = swr_alloc();
	audio_frame = av_frame_alloc();
}

VideoState::~VideoState() {
	// free video_ctx & audio_ctx
	avcodec_close(video_ctx);
	avcodec_free_context(&video_ctx);

	avcodec_close(audio_ctx);
	avcodec_free_context(&audio_ctx);

	avformat_close_input(&pFormatCtx);

	sws_freeContext(sws_ctx);
	swr_free(&swr_ctx);

	av_frame_free(&audio_frame);
}