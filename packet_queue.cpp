#include "packet_queue.h"

PacketQueue::PacketQueue() {
	mutex = SDL_CreateMutex();
	cond = SDL_CreateCond();
}

PacketQueue::~PacketQueue() {
	SDL_DestroyCond(cond);
	SDL_DestroyMutex(mutex);

	if (first_pkt != nullptr) {
		AVPacketList* dpkt;
		while (first_pkt != last_pkt) {
			dpkt = first_pkt;
			first_pkt = first_pkt->next;
			av_free(dpkt);
		}
		av_free(first_pkt);
	}
}

int PacketQueue::packet_queue_put(AVPacket* pkt) {
	AVPacketList* pkt1;
	if (pkt != flush_pkt_p && av_packet_make_refcounted(pkt) < 0) {
		return -1;
	}

	pkt1 = (AVPacketList*)av_mallocz(sizeof(AVPacketList));
	if (!pkt1) {
		return -1;
	}

	//av_packet_ref(&pkt1->pkt, pkt);
	pkt1->pkt = *pkt;
	pkt1->next = nullptr;

	SDL_LockMutex(mutex);

	// insert to the end of the queue, update last_pkt
	if (!last_pkt) {
		// if this is the first pkt, update first_pkt also
		first_pkt = pkt1;
	}
	else {
		last_pkt->next = pkt1;
	}
	last_pkt = pkt1;
	++(nb_packets);
	size += pkt1->pkt.size;

	SDL_CondSignal(cond);
	SDL_UnlockMutex(mutex);
	return 0;
}

int PacketQueue::packet_queue_get(AVPacket* pkt, int block) {
	AVPacketList* pkt1;
	int ret;

	SDL_LockMutex(mutex);

	for (;;) {

		if (*(quit) == 1) {
			ret = -1;
			//std::cout << "lets break at pqg" << std::endl;
			break;
		}

		// get the first element in queue, update pointers
		pkt1 = first_pkt;
		if (pkt1) {
			first_pkt = pkt1->next;
			if (!first_pkt) {
				last_pkt = nullptr;
			}
			--(nb_packets);
			size -= pkt1->pkt.size;
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
			SDL_CondWait(cond, mutex);
		}
	}
	SDL_UnlockMutex(mutex);

	return ret;
}

void PacketQueue::packet_queue_flush() {
	AVPacketList* pkt;
	AVPacketList* pkt1;

	SDL_LockMutex(mutex);

	for (pkt = first_pkt; pkt != nullptr; pkt = pkt1) {
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}

	last_pkt = nullptr;
	first_pkt = nullptr;
	nb_packets = 0;
	size = 0;

	//std::cout << "nothing left in queue" << std::endl;
	SDL_UnlockMutex(mutex);
}