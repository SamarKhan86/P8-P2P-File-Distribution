#include "peer_transfer.h"
#include "peer_connections.h"
#include "peer_runtime.h"
#include "protocol.h"
#include "sha256.h"
#include "piece_manager.h"

static int peer_send_msg(peer_conn_t *peer, uint8_t msg_id, const void *payload, uint32_t payload_len)
{
    if (!peer || peer->sock_fd < 0) return -1;
    pthread_mutex_lock(&peer->send_mutex);
    int ret = send_msg(peer->sock_fd, msg_id, payload, payload_len);
    pthread_mutex_unlock(&peer->send_mutex);
    return ret;
}

static int peer_send_have(peer_conn_t *peer, uint32_t piece_idx)
{
    uint32_t net_idx = htonl(piece_idx);
    return peer_send_msg(peer, MSG_HAVE, &net_idx, sizeof(net_idx));
}

int peer_send_request(peer_conn_t *peer, uint32_t piece_idx)
{
    uint32_t net_idx = htonl(piece_idx);
    return peer_send_msg(peer, MSG_REQUEST, &net_idx, sizeof(net_idx));
}

static int peer_send_piece(peer_conn_t *peer, uint32_t piece_idx, const uint8_t *data, uint32_t len)
{
    uint32_t net_idx = htonl(piece_idx);
    size_t payload_len = 4 + len;
    uint8_t *payload = malloc(payload_len);
    if (!payload) return -1;
    memcpy(payload, &net_idx, 4);
    memcpy(payload + 4, data, len);
    int ret = peer_send_msg(peer, MSG_PIECE, payload, (uint32_t)payload_len);
    free(payload);
    return ret;
}

int peer_send_bitfield(peer_conn_t *peer, const uint8_t *bits, uint32_t bitfield_bytes)
{
    return peer_send_msg(peer, MSG_BITFIELD, bits, bitfield_bytes);
}

void peer_broadcast_have(uint32_t piece_idx)
{
    pthread_mutex_lock(&g_state.state_mutex);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_state.peers[i].is_active && g_state.peers[i].sock_fd >= 0) {
            peer_send_have(&g_state.peers[i], piece_idx);
        }
    }
    pthread_mutex_unlock(&g_state.state_mutex);
}

void *peer_worker(void *arg)
{
    int peer_idx = (int)(intptr_t)arg;
    peer_conn_t *peer = &g_state.peers[peer_idx];

    while (g_state.running && peer->is_active && peer->sock_fd >= 0) {
        uint8_t msg_id = 0;
        void *payload = NULL;
        uint32_t payload_len = 0;

        if (recv_msg(peer->sock_fd, &msg_id, &payload, &payload_len) != 0) {
            pthread_mutex_lock(&g_state.state_mutex);
            peer_disconnect_locked(peer_idx);
            pthread_mutex_unlock(&g_state.state_mutex);
            break;
        }

        switch (msg_id) {
        case MSG_BITFIELD:
            if (payload_len == g_state.bitfield_bytes && payload) {
                pthread_mutex_lock(&g_state.state_mutex);
                memcpy(peer->bitfield, payload, payload_len);
                pthread_mutex_unlock(&g_state.state_mutex);
            }
            break;

        case MSG_HAVE:
            if (payload_len == 4 && payload) {
                uint32_t piece_idx = ntohl(*(uint32_t *)payload);
                if (piece_idx < g_state.meta.piece_count) {
                    pthread_mutex_lock(&g_state.state_mutex);
                    BIT_SET(peer->bitfield, piece_idx);
                    pthread_mutex_unlock(&g_state.state_mutex);
                }
            }
            break;

        case MSG_REQUEST:
            if (payload_len == 4 && payload) {
                uint32_t piece_idx = ntohl(*(uint32_t *)payload);
                if (piece_idx < g_state.meta.piece_count) {
                    pthread_mutex_lock(&g_state.state_mutex);
                    int have = BIT_CHECK(g_state.my_bitfield, piece_idx);
                    pthread_mutex_unlock(&g_state.state_mutex);

                    if (have) {
                        uint32_t piece_len;
                        uint8_t *buf;
                        if (piece_manager_read(&g_state, piece_idx, &buf, &piece_len) == 0) {
                                if (peer_send_piece(peer, piece_idx, buf, piece_len) == 0) {
                                    printf("[peer %u] Uploaded piece %u to %s:%u (%u bytes)\n",
                                           g_state.my_port, piece_idx, peer->ip, peer->port, piece_len);
                                    logger_event(&g_state.logger, g_state.my_port, "piece_uploaded", peer->ip,
                                                 peer->port, (int)piece_idx, piece_len, "sent", "whole piece");
                                }
                            free(buf);
                        }
                    }
                }
            }
            break;

        case MSG_PIECE:
            if (payload_len >= 4 && payload) {
                uint32_t piece_idx = ntohl(*(uint32_t *)payload);
                uint8_t *data = (uint8_t *)payload + 4;
                uint32_t data_len = payload_len - 4;
                uint32_t expected_len = metainfo_piece_len(&g_state.meta, piece_idx);

                logger_event(&g_state.logger, g_state.my_port, "piece_received", peer->ip,
                             peer->port, (int)piece_idx, data_len, "received", "awaiting hash check");

                pthread_mutex_lock(&g_state.state_mutex);
                if (peer->in_flight > 0) peer->in_flight--;

                if (piece_idx < g_state.meta.piece_count) {
                    if (g_state.piece_status[piece_idx] == PIECE_VERIFIED) {
                        /* Duplicate piece, safely ignore */
                    } else if (data_len == expected_len) {
                        uint8_t hash[SHA256_HASH_SIZE];
                        sha256_buffer(data, data_len, hash);
                        const uint8_t *expected_hash = g_state.meta.piece_hashes + (piece_idx * SHA256_HASH_SIZE);

                        if (memcmp(hash, expected_hash, SHA256_HASH_SIZE) == 0) {
                            ssize_t wr = pwrite(g_state.file_fd, data, data_len, (off_t)piece_idx * g_state.meta.piece_size);
                            if (wr != (ssize_t)data_len) {
                                fprintf(stderr, "[peer %u] Write error on piece %u!\n", g_state.my_port, piece_idx);
                                g_state.piece_status[piece_idx] = PIECE_MISSING;
                                g_state.piece_assigned_peer[piece_idx] = -1;
                                pthread_mutex_unlock(&g_state.state_mutex);
                                break;
                            }
                            g_state.piece_status[piece_idx] = PIECE_VERIFIED;
                            g_state.piece_assigned_peer[piece_idx] = -1;
                            BIT_SET(g_state.my_bitfield, piece_idx);
                            g_state.verified_count++;

                            printf("[peer %u] Downloaded piece %u/%u from %s:%u (verified SHA-256)\n",
                                   g_state.my_port, g_state.verified_count, g_state.meta.piece_count, peer->ip, peer->port);

                            logger_event(&g_state.logger, g_state.my_port, "piece_verified", peer->ip,
                                         peer->port, (int)piece_idx, data_len, "valid", "SHA-256 matched");

                            pthread_mutex_unlock(&g_state.state_mutex);
                            peer_broadcast_have(piece_idx);
                            pthread_mutex_lock(&g_state.state_mutex);
                        } else {
                            fprintf(stderr, "[peer %u] Hash mismatch on piece %u from %s:%u! Discarding.\n",
                                    g_state.my_port, piece_idx, peer->ip, peer->port);
                            logger_event(&g_state.logger, g_state.my_port, "piece_hash_failed", peer->ip,
                                         peer->port, (int)piece_idx, data_len, "invalid", "SHA-256 mismatch");
                            g_state.piece_status[piece_idx] = PIECE_MISSING;
                            g_state.piece_assigned_peer[piece_idx] = -1;
                        }
                    } else {
                        g_state.piece_status[piece_idx] = PIECE_MISSING;
                        g_state.piece_assigned_peer[piece_idx] = -1;
                    }
                }
                pthread_mutex_unlock(&g_state.state_mutex);
            }
            break;

        case MSG_CANCEL:
            /* Cancel outstanding request if any */
            break;

        default:
            break;
        }

        if (payload) free(payload);
    }

    return NULL;
}

