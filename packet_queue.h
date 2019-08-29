#pragma once
#include "util.h"

class PacketQueue {
public:
	PacketQueue();

	~PacketQueue();

	AVPacketList* first_pkt = nullptr;
	AVPacketList* last_pkt = nullptr;
	int nb_packets = 0;
	int size = 0;
	SDL_mutex* mutex;
	SDL_cond* cond;
	bool all_sent = false;
	int* quit = nullptr;
	AVPacket* flush_pkt_p = nullptr;

	int packet_queue_put(AVPacket* pkt);

	int packet_queue_get(AVPacket* pkt, int block);

	void packet_queue_flush();
};



