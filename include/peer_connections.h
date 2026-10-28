#ifndef PEER_CONNECTIONS_H
#define PEER_CONNECTIONS_H

#include "peer.h"

void peer_disconnect_locked(int peer_idx);
void *peer_listener_thread(void *arg);
int peer_connect_to_remote_peer(const char *ip, uint16_t port);

#endif
