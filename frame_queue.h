#pragma once
#include "util.h"

const size_t VIDEO_PICTURE_QUEUE_SIZE = 32;

class myFrame {
public:
	myFrame();
	~myFrame();

	AVFrame* frame;
	bool lastframe_flag = false;
	int64_t pts = 0;
};

class myFrameQueue {
public:
	myFrameQueue();
	~myFrameQueue();

	myFrame queue[VIDEO_PICTURE_QUEUE_SIZE] = {};
	size_t size = 0;
	size_t max_size = VIDEO_PICTURE_QUEUE_SIZE;
	size_t read_index = 0;
	size_t write_index = 0;
	SDL_mutex* mutex;
	SDL_cond* cond;


	int* quit = nullptr;

	myFrame* frame_queue_get_last_ref();

	myFrame* frame_queue_dequeue_pri();

	// dequeue read queue
	void frame_queue_update_read_index();

	myFrame* frame_queue_dequeue();

	// check and get writable position in frame queue
	myFrame* frame_queue_writablepos_ref();

	double frame_queue_get_pts();

	int frame_queue_get_repeat_coeff();

	void frame_queue_flush();
};




