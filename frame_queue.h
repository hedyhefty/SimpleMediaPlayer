#pragma once
#include "util.h"

const size_t VIDEO_PICTURE_QUEUE_SIZE = 64;

class myFrame {
public:
	myFrame() {
		frame = av_frame_alloc();
		lastframe_flag = false;
	}
	AVFrame* frame;
	bool lastframe_flag;
};

class myFrameQueue {
public:
	myFrameQueue() :
		max_size(VIDEO_PICTURE_QUEUE_SIZE),
		write_index(0),
		read_index(0),
		size(0),
		queue() {
		mutex = SDL_CreateMutex();

		std::cout << "framequeue init called" << std::endl;
		if (!mutex) {
			std::cout << "SDL: cannot create mutex" << std::endl;
		}

		cond = SDL_CreateCond();
		if (!cond) {
			std::cout << "SDL: cannot create cond" << std::endl;
		}
	}

	myFrame queue[VIDEO_PICTURE_QUEUE_SIZE];
	size_t size;
	size_t max_size;
	size_t read_index;
	size_t write_index;
	SDL_mutex* mutex;
	SDL_cond* cond;
};

myFrame* frame_queue_get_last_ref(myFrameQueue* f) {
	return &f->queue[f->read_index];
}

// dequeue read queue
void frame_queue_dequeue(myFrameQueue* f) {
	av_frame_unref(f->queue[f->read_index].frame);
	if (++(f->read_index) == f->max_size) {
		f->read_index = 0;
	}

	SDL_LockMutex(f->mutex);
	--(f->size);
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

// check and get writable position in frame queue
myFrame* frame_queue_writablepos_ref(myFrameQueue* f) {
	if (*quit_ref == 1) {
		return nullptr;
	}

	SDL_LockMutex(f->mutex);
	while (f->size >= f->max_size) {
		//std::cout << "waiting" << std::endl;
		int ret = SDL_CondWaitTimeout(f->cond, f->mutex, 100);
		//std::cout << "signal recieve" << std::endl;
		
		if (ret == -1) {
			SDL_UnlockMutex(f->mutex);
			SDL_Delay(10);
			return frame_queue_writablepos_ref(f);
		}
	}
	SDL_UnlockMutex(f->mutex);
	return &f->queue[f->write_index];
}