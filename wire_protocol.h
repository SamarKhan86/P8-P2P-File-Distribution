
#ifndef WIRE_PROTOCOL_H
#define WIRE_PROTOCOL_H

#include <stdint.h>
#include <winsock2.h>

#define WP_HAVE      4
#define WP_BITFIELD  5
#define WP_REQUEST   6
#define WP_PIECE     7
#define WP_CANCEL    8

#define WP_MAX_SIZE  65536

extern const char INFO_HASH[20];

int send_all(SOCKET s, const char *data, int size);
int recv_all(SOCKET s, char *data, int size);

int send_handshake(SOCKET s, const char *peer_id);
int recv_handshake(SOCKET s);

int send_message(SOCKET s, int id,
                 const char *data, uint32_t size);

int send_bitfield(SOCKET s);
int send_have(SOCKET s, uint32_t index);

int send_request(SOCKET s, uint32_t index,
                 uint32_t begin, uint32_t length);

int send_piece(SOCKET s, uint32_t index, uint32_t begin,
               const char *block, uint32_t size);

int send_cancel(SOCKET s, uint32_t index,
                uint32_t begin, uint32_t length);

int recv_message(SOCKET s, int *id,
                 char *data, uint32_t *size);

#endif
