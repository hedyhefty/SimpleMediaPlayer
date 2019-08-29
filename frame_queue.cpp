#include "frame_queue.h"

myFrame::myFrame() {
	frame = av_frame_alloc();
	lastframe_flag = false;
}

myFrame::~myFrame() {
	av_frame_free(&frame);
	//std::cout << "myFrame freed" << std::endl;
}

myFrameQueue::myFrameQueue() {
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

myFrameQueue::~myFrameQueue() {
	SDL_DestroyMutex(mutex);
	SDL_DestroyCond(cond);
	for (size_t i = 0; i < VIDEO_PICTURE_QUEUE_SIZE; ++i) {
		queue[i].~myFrame();
	}
}

myFrame* myFrameQueue::frame_queue_get_last_ref() {
	return &queue[read_index];
}

myFrame* myFrameQueue::frame_queue_dequeue_pri() {
	if (size == 0) {
		return nullptr;
	}
	myFrame* ret = new myFrame;
	myFrame* lastf = frame_queue_get_last_ref();
	av_frame_move_ref(ret->frame, lastf->frame);

	ret->lastframe_flag = lastf->lastframe_flag;

	av_frame_unref(lastf->frame);
	lastf->lastframe_flag = false;
	return ret;
}

// dequeue read queue
void myFrameQueue::frame_queue_update_read_index() {
	if (++(read_index) == max_size) {
		read_index = 0;
	}
}

myFrame* myFrameQueue::frame_queue_dequeue() {
	SDL_LockMutex(mutex);
	myFrame* ret = new myFrame;
	ret = frame_queue_dequeue_pri();
	frame_queue_update_read_index();
	--(size);
	SDL_UnlockMutex(mutex);
	return ret;
}

// check and get writable position in frame queue
myFrame* myFrameQueue::frame_queue_writablepos_ref() {
	if (*quit == 1) {
		return nullptr;
	}

	SDL_LockMutex(mutex);
	while (size >= max_size) {
		//std::cout << "waiting" << std::endl;
		int ret = SDL_CondWaitTimeout(cond, mutex, 100);
		//std::cout << "signal recieve" << std::endl;

		if (ret == -1) {
			SDL_UnlockMutex(mutex);
			SDL_Delay(10);
			return frame_queue_writablepos_ref();
		}
	}
	SDL_UnlockMutex(mutex);
	return &queue[write_index];
}

double myFrameQueue::frame_queue_get_pts() {
	double pts = 0;
	pts = (double)queue[read_index].frame->best_effort_timestamp;
	return pts;
}

int myFrameQueue::frame_queue_get_repeat_coeff() {
	return queue[read_index].frame->repeat_pict;
}

void myFrameQueue::frame_queue_flush() {
	SDL_LockMutex(mutex);
	size_t fsize = size;
	for (size_t i = 0; i < fsize; ++i) {
		av_frame_unref(queue[read_index].frame);
		queue[read_index].lastframe_flag = false;
		if (++(read_index) == max_size) {
			read_index = 0;
		}
		--(size);
	}
	//std::cout << "nothing left in frame queue" << std::endl;
	SDL_UnlockMutex(mutex);
}