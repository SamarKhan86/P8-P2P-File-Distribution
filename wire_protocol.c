
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include "wire_protocol.h"

const char INFO_HASH[20] = "12345678901234567890";

int send_all(SOCKET s, const char *data, int size)
{
    int total = 0, n;

    while (total < size) {
        n = send(s, data + total, size - total, 0);
        if (n <= 0) return 0;
        total += n;
    }

    return 1;
}

int recv_all(SOCKET s, char *data, int size)
{
    int total = 0, n;

    while (total < size) {
        n = recv(s, data + total, size - total, 0);
        if (n <= 0) return 0;
        total += n;
    }

    return 1;
}

/* Handshake: 68 bytes */
int send_handshake(SOCKET s, const char *peer_id)
{
    char msg[68] = {0};

    msg[0] = 19;
    memcpy(msg + 1, "BitTorrent protocol", 19);
    memcpy(msg + 28, INFO_HASH, 20);
    memcpy(msg + 48, peer_id, 20);

    return send_all(s, msg, 68);
}

int recv_handshake(SOCKET s)
{
    char msg[68];

    if (!recv_all(s, msg, 68))
        return 0;

    if (msg[0] != 19 ||
        memcmp(msg + 1, "BitTorrent protocol", 19) != 0 ||
        memcmp(msg + 28, INFO_HASH, 20) != 0) {
        printf("Invalid handshake\n");
        return 0;
    }

    printf("Handshake successful\n");
    return 1;
}

/* Standard length-prefixed message */
int send_message(SOCKET s, int id,
                 const char *data, uint32_t size)
{
    uint32_t length = htonl(size + 1);
    char message_id = (char)id;

    if (size > WP_MAX_SIZE - 1)
        return 0;

    if (!send_all(s, (char *)&length, 4))
        return 0;

    if (!send_all(s, &message_id, 1))
        return 0;

    if (size > 0)
        return send_all(s, data, (int)size);

    return 1;
}

int send_bitfield(SOCKET s)
{
    char bits = (char)0xF0;
    return send_message(s, WP_BITFIELD, &bits, 1);
}

int send_have(SOCKET s, uint32_t index)
{
    uint32_t value = htonl(index);
    return send_message(s, WP_HAVE, (char *)&value, 4);
}

static int send_block_message(SOCKET s, int id,
                              uint32_t index, uint32_t begin,
                              uint32_t length)
{
    uint32_t data[3];

    data[0] = htonl(index);
    data[1] = htonl(begin);
    data[2] = htonl(length);

    return send_message(s, id, (char *)data, 12);
}

int send_request(SOCKET s, uint32_t index,
                 uint32_t begin, uint32_t length)
{
    return send_block_message(s, WP_REQUEST,
                              index, begin, length);
}

int send_cancel(SOCKET s, uint32_t index,
                uint32_t begin, uint32_t length)
{
    return send_block_message(s, WP_CANCEL,
                              index, begin, length);
}

int send_piece(SOCKET s, uint32_t index, uint32_t begin,
               const char *block, uint32_t size)
{
    char *data;
    uint32_t value;
    int result;

    if (size > WP_MAX_SIZE - 9)
        return 0;

    data = malloc(size + 8);
    if (data == NULL)
        return 0;

    value = htonl(index);
    memcpy(data, &value, 4);

    value = htonl(begin);
    memcpy(data + 4, &value, 4);

    memcpy(data + 8, block, size);

    result = send_message(s, WP_PIECE, data, size + 8);

    free(data);
    return result;
}

/* Receive one complete message */
int recv_message(SOCKET s, int *id,
                 char *data, uint32_t *size)
{
    uint32_t net_length, length;
    char message_id;

    if (!recv_all(s, (char *)&net_length, 4))
        return 0;

    length = ntohl(net_length);

    if (length == 0) {
        *id = -1;  /* Keep-alive */
        *size = 0;
        return 1;
    }

    if (length > WP_MAX_SIZE)
        return 0;

    if (!recv_all(s, &message_id, 1))
        return 0;

    *id = (unsigned char)message_id;
    *size = length - 1;

    if (*size > 0 &&
        !recv_all(s, data, (int)*size))
        return 0;

    return 1;
}
