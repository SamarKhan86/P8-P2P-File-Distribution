#ifndef TRACKER_H
#define TRACKER_H

#include "common.h"

typedef struct {
    char ip[64];
    uint16_t port;
} peer_endpoint_t;

/* Convert 32-byte hash to 64-character hex string (+ null terminator) */
void hash_to_hex(const uint8_t hash[SHA256_HASH_SIZE], char hex[65]);

/* Convert 64-character hex string to 32-byte hash */
int hex_to_hash(const char *hex, uint8_t hash[SHA256_HASH_SIZE]);

/* Client helper to announce to tracker and retrieve active peer endpoints */
int tracker_announce(const char *tracker_ip, uint16_t tracker_port,
                     const uint8_t file_id[SHA256_HASH_SIZE], uint16_t my_port,
                     peer_endpoint_t peers[], int max_peers, int *peer_count);

#endif /* TRACKER_H */
