#pragma once
#include "util.h"

class PacketQueue {
public:
	PacketQueue() :
		first_pkt(nullptr),
		last_pkt(nullptr),
		nb_packets(0),
		size(0),
		all_sent(false) {
		mutex = SDL_CreateMutex();
		cond = SDL_CreateCond();
	}

	AVPacketList* first_pkt;
	AVPacketList* last_pkt;
	int nb_packets;
	int size;
	SDL_mutex* mutex;
	SDL_cond* cond;
	bool all_sent;
};

int packet_queue_put(PacketQueue* q, AVPacket* pkt) {
	AVPacketList* pkt1;
	if (av_packet_make_refcounted(pkt) < 0) {
		return -1;
	}

	pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pkt1) {
		return -1;
	}

	av_packet_ref(&pkt1->pkt, pkt);
	//pkt1->pkt = *pkt;
	pkt1->next = nullptr;

	SDL_LockMutex(q->mutex);

	// insert to the end of the queue, update last_pkt
	if (!q->last_pkt) {
		// if this is the first pkt, update first_pkt also
		q->first_pkt = pkt1;
	}
	else {
		q->last_pkt->next = pkt1;
	}
	q->last_pkt = pkt1;
	++(q->nb_packets);
	q->size += pkt1->pkt.size;

	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);
	return 0;
}

int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block) {
	AVPacketList* pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {

		if (*quit_ref == 1) {
			ret = -1;
			//std::cout << "lets break at pqg" << std::endl;
			break;
		}

		// get the first element in queue, update pointers
		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt) {
				q->last_pkt = nullptr;
			}
			--(q->nb_packets);
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);

	return ret;
}