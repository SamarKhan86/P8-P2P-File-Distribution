#ifndef PEER_TRANSFER_H
#define PEER_TRANSFER_H

#include "peer.h"

int peer_send_request(peer_conn_t *peer, uint32_t piece_idx);
int peer_send_bitfield(peer_conn_t *peer, const uint8_t *bits, uint32_t bytes);
void peer_broadcast_have(uint32_t piece_idx);
void *peer_worker(void *arg);

#endif
