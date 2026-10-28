#ifndef PEER_H
#define PEER_H

#include "common.h"
#include "metainfo.h"
#include "logger.h"

typedef enum {
    PIECE_MISSING = 0,
    PIECE_IN_FLIGHT = 1,
    PIECE_VERIFIED = 2
} piece_status_t;

typedef struct {
    int sock_fd;
    char peer_id[PEER_ID_LEN + 1];
    char ip[64];
    uint16_t port;
    uint8_t *bitfield;      /* Bitfield of pieces possessed by remote peer */
    int in_flight;          /* Number of pipelined requests currently pending */
    int is_active;          /* 1 if connected and handshake completed, 0 otherwise */
    pthread_mutex_t send_mutex;
    pthread_t thread;
} peer_conn_t;

typedef struct {
    char peer_id[PEER_ID_LEN + 1];
    uint16_t my_port;
    int file_fd;
    metainfo_t meta;

    uint8_t *my_bitfield;          /* Local bitfield array */
    piece_status_t *piece_status;  /* Status array for each piece */
    int *piece_assigned_peer;      /* Index of peer requested from (-1 if none) */
    uint32_t verified_count;       /* Total number of verified pieces */
    uint32_t bitfield_bytes;       /* Byte length of bitfield */

    peer_conn_t peers[MAX_PEERS];
    int peer_count;

    pthread_mutex_t state_mutex;   /* Protects shared state between listener and downloader */
    logger_t logger;               /* Optional per-peer CSV event log */
    int running;                   /* Flag for main loop */
} peer_state_t;

#endif /* PEER_H */
