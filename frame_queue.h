#pragma once
#include "util.h"

const size_t VIDEO_PICTURE_QUEUE_SIZE = 16;

class myFrame {
public:
	myFrame() {
		frame = av_frame_alloc();
		lastframe_flag = false;
	}
	~myFrame() {
		av_frame_free(&frame);
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

	~myFrameQueue() {
		SDL_DestroyMutex(mutex);
		SDL_DestroyCond(cond);
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

myFrame* frame_queue_dequeue_pri(myFrameQueue* f) {
	if (f->size == 0) {
		return nullptr;
	}
	myFrame* ret = new myFrame;
	myFrame* lastf = frame_queue_get_last_ref(f);
	av_frame_move_ref(ret->frame, lastf->frame);
	
	ret->lastframe_flag = lastf->lastframe_flag;

	av_frame_unref(lastf->frame);
	lastf->lastframe_flag = false;
	return ret;
}

// dequeue read queue
void frame_queue_update_read_index(myFrameQueue* f) {
	if (++(f->read_index) == f->max_size) {
		f->read_index = 0;
	}
}

myFrame* frame_queue_dequeue(myFrameQueue* f) {
	SDL_LockMutex(f->mutex);
	myFrame* ret = new myFrame;
	ret = frame_queue_dequeue_pri(f);
	frame_queue_update_read_index(f);
	--(f->size);
	SDL_UnlockMutex(f->mutex);
	return ret;
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

double frame_queue_get_pts(myFrameQueue* f) {
	double pts = 0;
	pts = (double)f->queue[f->read_index].frame->best_effort_timestamp;
	return pts;
}

int frame_queue_get_repeat_coeff(myFrameQueue* f) {
	return f->queue[f->read_index].frame->repeat_pict;
}

void frame_queue_flush(myFrameQueue* f) {
	SDL_LockMutex(f->mutex);
	size_t fsize = f->size;
	for (size_t i = 0; i < fsize; ++i) {
		av_frame_unref(f->queue[f->read_index].frame);
		f->queue[f->read_index].lastframe_flag = false;
		if (++(f->read_index) == f->max_size) {
			f->read_index = 0;
		}
		--(f->size);
	}
	//std::cout << "nothing left in frame queue" << std::endl;
	SDL_UnlockMutex(f->mutex);
}