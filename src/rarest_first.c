/* Rarest-first policy, kept separate so it is easy to explain and test. */
#include "rarest_first.h"

int choose_rarest_piece(const peer_state_t *peer_state,
                        const peer_conn_t *source_peer)
{
    int best_piece = -1;
    int fewest_sources = MAX_PEERS + 1;

    for (uint32_t piece = 0; piece < peer_state->meta.piece_count; piece++) {
        /* We only choose pieces we still need and this connection can send. */
        if (peer_state->piece_status[piece] != PIECE_MISSING ||
            !BIT_CHECK(source_peer->bitfield, piece)) {
            continue;
        }

        int source_count = 0;
        for (int peer_index = 0; peer_index < MAX_PEERS; peer_index++) {
            const peer_conn_t *candidate = &peer_state->peers[peer_index];
            if (candidate->is_active && BIT_CHECK(candidate->bitfield, piece)) {
                source_count++;
            }
        }

        /* The first equal-count piece wins: a simple deterministic tie-break. */
        if (source_count > 0 && source_count < fewest_sources) {
            fewest_sources = source_count;
            best_piece = (int)piece;
        }
    }

    return best_piece;
}
