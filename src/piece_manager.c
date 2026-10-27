#include "piece_manager.h"
#include "sha256.h"

uint32_t piece_manager_length(const peer_state_t *state, uint32_t piece_idx)
{
    return metainfo_piece_len(&state->meta, piece_idx);
}

int piece_manager_read(const peer_state_t *state, uint32_t piece_idx,
                       uint8_t **data, uint32_t *length)
{
    *length = piece_manager_length(state, piece_idx);
    *data = malloc(*length);
    if (!*data) return -1;
    if (pread(state->file_fd, *data, *length,
              (off_t)piece_idx * state->meta.piece_size) != (ssize_t)*length) {
        free(*data);
        *data = NULL;
        return -1;
    }
    return 0;
}

void piece_manager_scan_existing(peer_state_t *state)
{
    struct stat stat_info;
    if (fstat(state->file_fd, &stat_info) != 0 || stat_info.st_size <= 0) return;

    uint8_t *data = malloc(state->meta.piece_size);
    if (!data) return;
    for (uint32_t piece = 0; piece < state->meta.piece_count; piece++) {
        uint32_t length = piece_manager_length(state, piece);
        if (pread(state->file_fd, data, length,
                  (off_t)piece * state->meta.piece_size) == (ssize_t)length) {
            uint8_t hash[SHA256_HASH_SIZE];
            sha256_buffer(data, length, hash);
            if (memcmp(hash, state->meta.piece_hashes + piece * SHA256_HASH_SIZE,
                       SHA256_HASH_SIZE) == 0) {
                BIT_SET(state->my_bitfield, piece);
                state->piece_status[piece] = PIECE_VERIFIED;
                state->verified_count++;
            }
        }
    }
    free(data);
}
