#ifndef PEER_RUNTIME_H
#define PEER_RUNTIME_H

#include "peer.h"

/* Runtime state owned by peer.c and shared by the three peer modules. */
extern peer_state_t g_state;
extern int g_listen_fd;

#endif
