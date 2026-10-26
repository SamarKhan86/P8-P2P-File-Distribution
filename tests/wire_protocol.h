#ifndef WIRE_PROTOCOL_H
#define WIRE_PROTOCOL_H
#define WP_METADATA 9

#include <stdint.h>

/* Message IDs used by the BitTorrent-style wire protocol. */
#define WP_CHOKE       0
#define WP_UNCHOKE     1
#define WP_INTERESTED  2
#define WP_NOT_INTERESTED 3
#define WP_HAVE        4
#define WP_BITFIELD    5
#define WP_REQUEST     6
#define WP_PIECE       7
#define WP_CANCEL      8

/* Maximum message payload length, including the one-byte message ID. */
#define WP_MAX_SIZE 65536
#define WP_HANDSHAKE_SIZE 68

/* A fixed demo hash for now; replace with the real torrent/file hash later. */
extern const char INFO_HASH[20];

/* Socket argument is a normal Linux/POSIX file descriptor. */
int send_all(int socket_fd, const char *data, int size);
int recv_all(int socket_fd, char *data, int size);

int send_handshake(int socket_fd, const char *peer_id);
int recv_handshake(int socket_fd);

int send_message(int socket_fd, int id, const char *data, uint32_t size);
int send_keep_alive(int socket_fd);
int send_choke(int socket_fd);
int send_unchoke(int socket_fd);
int send_interested(int socket_fd);
int send_not_interested(int socket_fd);
int send_bitfield(int socket_fd, const char *bits, uint32_t size);
int send_have(int socket_fd, uint32_t index);
int send_request(int socket_fd, uint32_t index, uint32_t begin, uint32_t length);
int send_piece(int socket_fd, uint32_t index, uint32_t begin,
               const char *block, uint32_t size);
int send_cancel(int socket_fd, uint32_t index, uint32_t begin, uint32_t length);

/*
 * Receives one complete message. Caller must provide data[WP_MAX_SIZE].
 * Returns id = -1 for keep-alive; size is the message payload size.
 */
int recv_message(int socket_fd, int *id, char *data, uint32_t *size);

#endif

int send_metadata(int socket_fd, uint64_t file_size,
                  uint32_t piece_size);

int recv_metadata(int socket_fd, uint64_t *file_size,
                  uint32_t *piece_size, uint32_t *piece_count);
