#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

/* Protocol Message IDs */
#define MSG_BITFIELD 1
#define MSG_HAVE     2
#define MSG_REQUEST  3
#define MSG_PIECE    4
#define MSG_CANCEL   5

/* Network I/O helper functions handling partial reads/writes and EINTR */
int send_all(int fd, const void *buf, size_t len);
int recv_all(int fd, void *buf, size_t len);

/* Handshake exchange (60 bytes) */
int send_handshake(int fd, const uint8_t file_id[SHA256_HASH_SIZE], const char peer_id[PEER_ID_LEN]);
int recv_handshake(int fd, uint8_t file_id[SHA256_HASH_SIZE], char peer_id[PEER_ID_LEN]);

/* Generic wire message sending and receiving */
int send_msg(int fd, uint8_t msg_id, const void *payload, uint32_t payload_len);
int recv_msg(int fd, uint8_t *msg_id, void **payload, uint32_t *payload_len);

/* Specific helper message senders */
int send_bitfield(int fd, const uint8_t *bits, uint32_t bitfield_bytes);
int send_have(int fd, uint32_t piece_idx);
int send_request(int fd, uint32_t piece_idx);
int send_piece(int fd, uint32_t piece_idx, const uint8_t *data, uint32_t len);

#endif /* PROTOCOL_H */
