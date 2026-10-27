#ifndef RAREST_FIRST_H
#define RAREST_FIRST_H

#include "peer.h"

/*
 * Returns a missing piece offered by source_peer that has the fewest active
 * sources in the swarm. Returns -1 when this peer cannot supply any missing
 * piece. Call while peer_state->state_mutex is locked.
 */
int choose_rarest_piece(const peer_state_t *peer_state,
                        const peer_conn_t *source_peer);

#endif /* RAREST_FIRST_H */
