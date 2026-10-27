#ifndef PIECE_MANAGER_H
#define PIECE_MANAGER_H

#include "peer.h"

/* File-piece helpers are introduced here as the home for future piece-only work. */
uint32_t piece_manager_length(const peer_state_t *state, uint32_t piece_idx);
int piece_manager_read(const peer_state_t *state, uint32_t piece_idx,
                       uint8_t **data, uint32_t *length);
void piece_manager_scan_existing(peer_state_t *state);

#endif
